/*
** Copyright 2010 Logitech. All Rights Reserved.
**
** This file is licensed under BSD. Please see the LICENSE file for details.
*/
#define RUNTIME_DEBUG 1

#include "common.h"
#include "ui/jive.h"

#include <linux/input.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>

#include <netinet/in.h>
#include <linux/types.h>
#include <linux/netlink.h>

static int ir_event_fd = -1;
static int uevent_fd = -1;

#define TIMEVAL_TO_TICKS(tv) ((tv.tv_sec * 1000) + (tv.tv_usec / 1000))

/* in ir.c */
void ir_input_code(Uint32 code, Uint32 time);
void ir_input_complete(Uint32 time);


static Uint32 bsp_get_realtime_millis() {
	Uint32 millis;
	struct timespec now;
	clock_gettime(CLOCK_REALTIME,&now);
	millis=now.tv_sec*1000+now.tv_nsec/1000000;
	return(millis);
}

static Uint32 queue_sw_event(Uint32 ticks, Uint32 code, Uint32 value) {
	JiveEvent event;

	memset(&event, 0, sizeof(JiveEvent));
	event.type = JIVE_EVENT_SWITCH;
	event.u.sw.code = code;
	event.u.sw.value = value;
	event.ticks = ticks;
	jive_queue_event(&event);

	return 0;
}

static int handle_ir_events(int fd) {
	struct input_event ev[64];
	size_t rd;
	int i;

	rd = read(fd, ev, sizeof(struct input_event) * 64);

	if (rd < (int) sizeof(struct input_event)) {
		perror("read error");
		return -1;
	}

	for (i = 0; i <= rd / sizeof(struct input_event); i++) {	
		if (ev[i].type == EV_MSC) {
			//TIMEVAL_TO_TICKS doesn't not really return ticks since these ev times are jiffies, but we won't be comparing against real ticks.
			Uint32 input_time = TIMEVAL_TO_TICKS(ev[i].time);
			Uint32 ir_code = ev[i].value;

			ir_input_code(ir_code, input_time);

		} else if(ev[i].type == EV_SW) {	
			// Pass along all switch events to jive
			Uint32 input_time = TIMEVAL_TO_TICKS(ev[i].time);
			queue_sw_event(input_time, ev[i].code, ev[i].value);
		}
		// ignore EV_SYN
	}

	return 0;
}

static int open_input_devices(void) {
	char path[PATH_MAX];
	struct stat sbuf;
	char name[254];
	int n, fd;

	for (n=0; n<10; n++) {
		snprintf(path, sizeof(path), "/dev/input/event%d", n);

		if ((stat(path, &sbuf) != 0) | !S_ISCHR(sbuf.st_mode)) {
			continue;
		}

		if ((fd = open(path, O_RDONLY, 0)) < 0) {
			perror("open");
			continue;
		}

		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
			perror("ioctrl");
			close(fd);
			continue;
		}
		else if (strstr(name, "FAB4 IR")) {
			ir_event_fd = fd;
		}
		else {
			fprintf(stderr, "input device (%d) (%s)\n", n, name);
			close(fd);
		}
	}

	return (ir_event_fd != -1);
}


static int open_uevent_fd(void)
{
        struct sockaddr_nl saddr;
        const int buffersize = 16 * 1024 * 1024;
        int retval;

        memset(&saddr, 0x00, sizeof(struct sockaddr_nl));
        saddr.nl_family = AF_NETLINK;
        saddr.nl_pid = getpid();
        saddr.nl_groups = 1;

        uevent_fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
        if (uevent_fd == -1) {
                fprintf(stderr, "error getting socket: %s", strerror(errno));
                return -1;
        }

        /* set receive buffersize */
        setsockopt(uevent_fd, SOL_SOCKET, SO_RCVBUFFORCE, &buffersize, sizeof(buffersize));

        retval = bind(uevent_fd, (struct sockaddr *) &saddr, sizeof(struct sockaddr_nl));
        if (retval < 0) {
                fprintf(stderr, "bind failed: %s", strerror(errno));
                close(uevent_fd);
                uevent_fd = -1;
                return -1;
        }
        return 0;
}


static void handle_uevent(lua_State *L, int sock)
{
        char *ptr, *end, buffer[2048];
        ssize_t size;

        size = recv(uevent_fd, &buffer, sizeof(buffer), 0);
        if (size <  0) {
                if (errno != EINTR)
                        printf("unable to receive kernel netlink message: %s", strerror(errno));
                return;
        }

	lua_getglobal(L, "ueventHandler");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		fprintf(stderr, "No ueventHandler\n");
		return;
	}

	ptr = buffer;
	end = strchr(ptr, '\0');

	/* evt */
	lua_pushlstring(L, ptr, end-ptr);

	/* msg */
	lua_newtable(L);
	while (end + 1 < buffer + size) {
		ptr = end + 1;
		end = strchr(ptr, '=');

		lua_pushlstring(L, ptr, end-ptr);

		ptr = end + 1;
		end = strchr(ptr, '\0');

		lua_pushlstring(L, ptr, end-ptr);

		lua_settable(L, -3);
	}

	if (lua_pcall(L, 2, 0, 0) != 0) {
		fprintf(stderr, "error calling ueventHandler %s\n", lua_tostring(L, -1));
		lua_pop(L, 1);
	}
}


static int event_pump(lua_State *L) {
	fd_set fds;
	struct timeval timeout;

	Uint32 now;
	
	FD_ZERO(&fds);
	memset(&timeout, 0, sizeof(timeout));

	if (ir_event_fd != -1) {
		FD_SET(ir_event_fd, &fds);
	}
	if (uevent_fd != -1) {
		FD_SET(uevent_fd, &fds);
	}

	if (select(FD_SETSIZE, &fds, NULL, NULL, &timeout) < 0) {
		perror("jivebsp:");
		return -1;
	}

	if (uevent_fd != -1 && FD_ISSET(uevent_fd, &fds)) {
		handle_uevent(L, uevent_fd);
	}

	now = bsp_get_realtime_millis();

	if (ir_event_fd != -1 && FD_ISSET(ir_event_fd, &fds)) {
		handle_ir_events(ir_event_fd);
	}
	ir_input_complete(now);
	
	return 0;
}


int luaopen_fab4_bsp(lua_State *L) {
	if (open_input_devices()) {
		jive_sdlevent_pump = event_pump;
	}

	open_uevent_fd();

	return 1;
}
