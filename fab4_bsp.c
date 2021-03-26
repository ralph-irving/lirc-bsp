/*
** Copyright 2021 Ralph Irving. All Rights Reserved.
**
** This file is licensed under BSD. Please see the LICENSE file for details.
*/
#define RUNTIME_DEBUG 0

#include <lirc/lirc_client.h>

/* Avoid name conflicts from syslog.h/curl.h inclusion in lirc client header */
#ifdef LOG_DEBUG
#undef LOG_DEBUG
#endif
#ifdef LOG_INFO
#undef LOG_INFO
#endif
#ifdef PACKAGE
#undef PACKAGE
#endif
#ifdef PACKAGE_STRING
#undef PACKAGE_STRING
#endif
#ifdef PACKAGE_BUGREPORT
#undef PACKAGE_BUGREPORT
#endif
#ifdef PACKAGE_NAME
#undef PACKAGE_NAME
#endif
#ifdef PACKAGE_TARNAME
#undef PACKAGE_TARNAME
#endif
#ifdef PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif
#ifdef VERSION
#undef VERSION
#endif

#include "jive.h"

#define LIRC_CLIENT_ID "ir_bsp"

static int ir_event_fd = -1;
static struct lirc_config *ir_config;

LOG_CATEGORY *log_ui;

/* cmds based on entries in Slim_Devices_Remote.ir these may appear as config entries in lircrc. */

static struct {
	char  *cmd;
	Uint32 code;
} cmdmap[] = {
	{ "voldown",  0x768900ff },
	{ "volup",    0x7689807f },
	{ "rew",      0x7689c03f },
	{ "fwd",      0x7689a05f },
	{ "pause",    0x768920df },
	{ "play",     0x768910ef },
	{ "power",    0x768940bf },
	{ "muting",   0x7689c43b },
	{ "power_on", 0x76898f70 },
	{ "power_off",0x76898778 },
	{ "preset_1", 0x76898a75 },
	{ "preset_2", 0x76894ab5 },
	{ "preset_3", 0x7689ca35 },
	{ "preset_4", 0x76892ad5 },
	{ "preset_5", 0x7689aa55 },
	{ "preset_6", 0x76896a95 },
	{ NULL,       0          },
};

/* selected lirc namespace button names as defaults, some support repeat. */

static struct {
	char  *lirc;
	Uint32 code;
	bool  repeat;
} keymap[] = {
	{ "KEY_VOLUMEDOWN", 0x768900ff, true  },
	{ "KEY_VOLUMEUP",   0x7689807f, true  },
	{ "KEY_PREVIOUS",   0x7689c03f, true  },
	{ "KEY_REWIND",     0x7689c03f, true  },
	{ "KEY_NEXT",       0x7689a05f, true  },
	{ "KEY_FORWARD",    0x7689a05f, true  },
	{ "KEY_PAUSE",      0x768920df, true  },
	{ "KEY_PLAY",       0x768910ef, false },
	{ "KEY_POWER",      0x768940bf, false },
	{ "KEY_MUTE",       0x7689c43b, false },
	{ "KEY_0",          0x76899867, true  },
	{ "KEY_1",          0x7689f00f, true  },
	{ "KEY_2",          0x768908f7, true  },
	{ "KEY_3",          0x76898877, true  },
	{ "KEY_4",          0x768948b7, true  },
	{ "KEY_5",          0x7689c837, true  },
	{ "KEY_6",          0x768928d7, true  },
	{ "KEY_7",          0x7689a857, true  },
	{ "KEY_8",          0x76896897, true  },
	{ "KEY_9",          0x7689e817, true  },
	{ "KEY_FAVORITES",  0x768918e7, false },
	{ "KEY_FAVORITES",  0x7689e21d, false },
	{ "KEY_SEARCH",     0x768958a7, false },
	{ "KEY_SEARCH",     0x7689629d, false },
	{ "KEY_SHUFFLE",    0x7689d827, false },
	{ "KEY_SLEEP",      0x7689b847, false },
	{ "KEY_INSERT",     0x7689609f, false }, // Add
	{ "KEY_UP",         0x7689e01f, true  },
	{ "KEY_LEFT",       0x7689906f, true  },
	{ "KEY_RIGHT",      0x7689d02f, true  },
	{ "KEY_DOWN",       0x7689b04f, true  },
	{ "KEY_HOME",       0x768922dd, false },
	{ "KEY_MEDIA_REPEAT", 0x768938c7, false },
	{ "KEY_TITLE",      0x76897887, false }, // Now Playing
	{ "KEY_TITLE",      0x7689a25d, false }, // Now Playing
	{ "KEY_TEXT",       0x7689f807, false }, // Size 
	{ "KEY_BRIGHTNESS_CYCLE", 0x768904fb, false },
	{ NULL,             0         , false },
};

/* in ir.c */
void ir_input_code(Uint32 code, Uint32 time);
void ir_input_complete(Uint32 time);


static Uint32 ir_cmd_map(const char *c) {
	int i;
	for (i = 0; cmdmap[i].cmd; i++) {
		if (!strcmp(c, cmdmap[i].cmd)) {
			return cmdmap[i].code;
		}
	}
	return 0;
}


static Uint32 ir_key_map(const char *c, const char *r) {
	int i;
	for (i = 0; keymap[i].lirc; i++) {
		if (!strcmp(c, keymap[i].lirc)) {
			// inputlirc issues "0", while LIRC uses "00"
			if (keymap[i].repeat || !strcmp(r, "0") || !strcmp(r,"00")) {
				return keymap[i].code;
			}
			LOG_WARN(log_ui,"repeat suppressed");
			break;
		}
	}
	return 0;
}


static int handle_ir_events(int fd) {
	char *code;
	
	if (fd > 0 && lirc_nextcode(&code) == 0) {
		
		Uint32 input_time = jive_jiffies();
		Uint32 ir_code = 0;
		Uint32 count = 0;
		
		if (code == NULL) return -1;
		
		if (ir_config) {
			/* allow lirc_client to decode then lookup cmd in our table
			   we can only send one IR event to slimproto so break after first one. */
			char *c;
			while (lirc_code2char(ir_config, code, &c) == 0 && c != NULL) {
				ir_code = ir_cmd_map(c);
				if (ir_code) {
					LOG_WARN(log_ui,"ir cmd: %s -> %x", c, ir_code);
				}
			}
		}

		if (!ir_code) {
			char *b, *r;
			// try to match on lirc button name if it is from the standard namespace
			// this allows use of non slim remotes without a specific entry in .lircrc
			strtok(code, " \n");     // discard
			r = strtok(NULL, " \n"); // repeat count
			if (r) count = atoi(r);
			b = strtok(NULL, " \n"); // key name
			if (r && b) {
				ir_code = ir_key_map(b, r);
				if ( count > 1) ir_code = 0;
				LOG_WARN(log_ui,"ir lirc: %s [%s] -> %x", b, r, ir_code);
			}
		}

		if (ir_code || count > 1) {
			ir_input_code(ir_code, input_time);
		}
		
		free(code);
	}
	
	return 0;
}

static int open_input_devices(void) {
	char lircrc[PATH_MAX];
	ir_event_fd = lirc_init(LIRC_CLIENT_ID, 0);

	snprintf(lircrc, sizeof(lircrc), "%s%cuserpath%csettings%clircrc.conf", platform_get_home_dir(), DIR_SEPARATOR_CHAR, DIR_SEPARATOR_CHAR, DIR_SEPARATOR_CHAR);

        if (ir_event_fd > 0) {
                if (lirc_readconfig(lircrc, &ir_config, NULL) != 0) {
			LOG_INFO(log_ui,"error reading config: %s", lircrc);
                }
                else {
			LOG_INFO(log_ui,"loaded lircrc config: %s", lircrc);
                }

        } else {
		LOG_ERROR(log_ui,"failed to connect to lircd - ir processing disabled");
        }

	return (ir_event_fd != -1);
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

	if (select(FD_SETSIZE, &fds, NULL, NULL, &timeout) < 0) {
		perror("ir_bsp:");
		return -1;
	}

	now = jive_jiffies();

	if (ir_event_fd != -1 && FD_ISSET(ir_event_fd, &fds)) {
		handle_ir_events(ir_event_fd);
	}

	ir_input_complete(jive_jiffies());
	
	return 0;
}


int luaopen_ir_bsp(lua_State *L) {
 	log_ui = LOG_CATEGORY_GET("squeezeplay.ui");

	if (open_input_devices()) {
		jive_sdlevent_pump = event_pump;
	}

	return 1;
}
