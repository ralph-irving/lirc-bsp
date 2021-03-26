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
	{ "0", 0x76899867 },
	{ "1", 0x7689f00f },
	{ "2", 0x768908f7 },
	{ "3", 0x76898877 },
	{ "4", 0x768948b7 },
	{ "5", 0x7689c837 },
	{ "6", 0x768928d7 },
	{ "7", 0x7689a857 },
	{ "8", 0x76896897 },
	{ "9", 0x7689e817 },
	{ "add", 0x7689609f },
	{ "analog_input_line_in", 0x76890ef1 },
	{ "arrow_down", 0x7689b04f },
	{ "arrow_left", 0x7689906f },
	{ "arrow_right", 0x7689d02f },
	{ "arrow_up", 0x7689e01f },
	{ "brightness", 0x768904fb },
	{ "browse", 0x7689708f },
	{ "digital_input_aes-ebu", 0x768906f9 },
	{ "digital_input_bnc-spdif", 0x76898679 },
	{ "digital_input_rca-spdif", 0x768946b9 },
	{ "digital_input_toslink", 0x7689c639 },
	{ "favorites", 0x768918e7 },
	{ "favorites_2", 0x7689e21d },
	{ "fwd", 0x7689a05f },
	{ "home", 0x768922dd },
	{ "menu_browse_album", 0x76897c83 },
	{ "menu_browse_artist", 0x7689748b },
	{ "menu_browse_music", 0x7689728d },
	{ "menu_browse_playlists", 0x76897a85 },
	{ "menu_search_album", 0x76895ca3 },
	{ "menu_search_artist", 0x768954ab },
	{ "menu_search_song", 0x768952ad },
	{ "muting", 0x7689c43b },
	{ "now_playing", 0x76897887 },
	{ "now_playing_2", 0x7689a25d },
	{ "pause", 0x768920df },
	{ "play", 0x768910ef },
	{ "power", 0x768940bf },
	{ "power_off", 0x76898778 },
	{ "power_on", 0x76898f70 },
	{ "preset_1", 0x76898a75 },
	{ "preset_2", 0x76894ab5 },
	{ "preset_3", 0x7689ca35 },
	{ "preset_4", 0x76892ad5 },
	{ "preset_5", 0x7689aa55 },
	{ "preset_6", 0x76896a95 },
	{ "repeat", 0x768938c7 },
	{ "rew", 0x7689c03f },
	{ "search", 0x768958a7 },
	{ "search_2", 0x7689629d },
	{ "shuffle", 0x7689d827 },
	{ "size", 0x7689f807 },
	{ "sleep", 0x7689b847 },
	{ "voldown", 0x768900ff },
	{ "volup", 0x7689807f },
	{ NULL, 0 },
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

/* Take a hex string and convert it to a 32bit number (max 8 hex digits) */
static Uint32 xtoi(const char *hex) {
	Uint32 val = 0;

	while (*hex) {

		/* get current character then increment */
	        Uint8 byte = *hex++; 

		/* transform hex character to the 4bit equivalent number, using the ascii table indexes */
		if (byte >= '0' && byte <= '9') byte = byte - '0';
		else if (byte >= 'a' && byte <='f') byte = byte - 'a' + 10;
		else if (byte >= 'A' && byte <='F') byte = byte - 'A' + 10;

		/* shift 4 to make space for new digit, and add the 4 bits of the new digit */
		val = (val << 4) | (byte & 0xF);
	}

	return val;
}

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
	int count = 0;

	for (i = 0; keymap[i].lirc; i++) {
		if (!strcmp(c, keymap[i].lirc)) {
			if (r) count = xtoi(r);
			if (keymap[i].repeat || !count) {
				return keymap[i].code;
			}
			LOG_WARN(log_ui,"repeat suppressed, count:%i", count);
			break;
		}
	}

	return 0;
}

#if 0
static Uint32 bsp_get_realtime_millis() {
	Uint32 millis;
	struct timespec now;
	clock_gettime(CLOCK_REALTIME,&now);
	millis=now.tv_sec*1000+now.tv_nsec/1000000;
	return(millis);
}
#endif

static int handle_ir_events(int fd) {
	char *code;
	
	if (fd > 0 && lirc_nextcode(&code) == 0) {
		
		Uint32 input_time;
		Uint32 ir_code = 0;
		
		if (code == NULL) return -1;

		input_time = jive_jiffies();
		
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
			// try to match on lirc button name if it is from the standard namespace
			// this allows use of non slim remotes without a specific entry in .lircrc
			char *b, *r;
			strtok(code, " \n");     // discard
			r = strtok(NULL, " \n"); // repeat count
			b = strtok(NULL, " \n"); // key name
			if (r && b) {
				ir_code = ir_key_map(b, r);
				LOG_WARN(log_ui,"ir lirc: %s [%s] -> %x", b, r, ir_code);
			}
		}
		if (ir_code) {
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
                if (lirc_readconfig_only(lircrc, &ir_config, NULL) != 0) {
			LOG_INFO(log_ui,"lirc client config: %s not found - cmd mapping disabled", lircrc);
                }
                else {
			LOG_INFO(log_ui,"loaded lirc client config: %s", lircrc);
                }

        } else {
		LOG_ERROR(log_ui,"failed to connect to lircd - ir processing disabled");
        }

	return (ir_event_fd != -1);
}

static int event_pump(lua_State *L) {
	fd_set fds;
	struct timeval timeout;
#if 0
	Uint32 now;
#endif	
	FD_ZERO(&fds);
	memset(&timeout, 0, sizeof(timeout));

	if (ir_event_fd != -1) {
		FD_SET(ir_event_fd, &fds);
	}

	if (select(FD_SETSIZE, &fds, NULL, NULL, &timeout) < 0) {
		LOG_ERROR(log_ui,"ir_bsp: select failed %d", errno);
		return -1;
	}
#if 0
	now = jive_jiffies();
#endif
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
