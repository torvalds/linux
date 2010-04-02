/* avermedia-dvbt.h - Keytable for avermedia_dvbt Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Matt Jesson <dvb@jesson.eclipse.co.uk */

#ifdef IR_KEYMAPS
static struct ir_scancode avermedia_dvbt[] = {
	{ 0x28, KEY_0 },		/* '0' / 'enter' */
	{ 0x22, KEY_1 },		/* '1' */
	{ 0x12, KEY_2 },		/* '2' / 'up arrow' */
	{ 0x32, KEY_3 },		/* '3' */
	{ 0x24, KEY_4 },		/* '4' / 'left arrow' */
	{ 0x14, KEY_5 },		/* '5' */
	{ 0x34, KEY_6 },		/* '6' / 'right arrow' */
	{ 0x26, KEY_7 },		/* '7' */
	{ 0x16, KEY_8 },		/* '8' / 'down arrow' */
	{ 0x36, KEY_9 },		/* '9' */

	{ 0x20, KEY_LIST },		/* 'source' */
	{ 0x10, KEY_TEXT },		/* 'teletext' */
	{ 0x00, KEY_POWER },		/* 'power' */
	{ 0x04, KEY_AUDIO },		/* 'audio' */
	{ 0x06, KEY_ZOOM },		/* 'full screen' */
	{ 0x18, KEY_VIDEO },		/* 'display' */
	{ 0x38, KEY_SEARCH },		/* 'loop' */
	{ 0x08, KEY_INFO },		/* 'preview' */
	{ 0x2a, KEY_REWIND },		/* 'backward <<' */
	{ 0x1a, KEY_FASTFORWARD },	/* 'forward >>' */
	{ 0x3a, KEY_RECORD },		/* 'capture' */
	{ 0x0a, KEY_MUTE },		/* 'mute' */
	{ 0x2c, KEY_RECORD },		/* 'record' */
	{ 0x1c, KEY_PAUSE },		/* 'pause' */
	{ 0x3c, KEY_STOP },		/* 'stop' */
	{ 0x0c, KEY_PLAY },		/* 'play' */
	{ 0x2e, KEY_RED },		/* 'red' */
	{ 0x01, KEY_BLUE },		/* 'blue' / 'cancel' */
	{ 0x0e, KEY_YELLOW },		/* 'yellow' / 'ok' */
	{ 0x21, KEY_GREEN },		/* 'green' */
	{ 0x11, KEY_CHANNELDOWN },	/* 'channel -' */
	{ 0x31, KEY_CHANNELUP },	/* 'channel +' */
	{ 0x1e, KEY_VOLUMEDOWN },	/* 'volume -' */
	{ 0x3e, KEY_VOLUMEUP },		/* 'volume +' */
};
DEFINE_LEGACY_IR_KEYTABLE(avermedia_dvbt);
#else
DECLARE_IR_KEYTABLE(avermedia_dvbt);
#endif
