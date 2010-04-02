/* avermedia.h - Keytable for avermedia Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Alex Hermann <gaaf@gmx.net> */

#ifdef IR_KEYMAPS
static struct ir_scancode avermedia[] = {
	{ 0x28, KEY_1 },
	{ 0x18, KEY_2 },
	{ 0x38, KEY_3 },
	{ 0x24, KEY_4 },
	{ 0x14, KEY_5 },
	{ 0x34, KEY_6 },
	{ 0x2c, KEY_7 },
	{ 0x1c, KEY_8 },
	{ 0x3c, KEY_9 },
	{ 0x22, KEY_0 },

	{ 0x20, KEY_TV },		/* TV/FM */
	{ 0x10, KEY_CD },		/* CD */
	{ 0x30, KEY_TEXT },		/* TELETEXT */
	{ 0x00, KEY_POWER },		/* POWER */

	{ 0x08, KEY_VIDEO },		/* VIDEO */
	{ 0x04, KEY_AUDIO },		/* AUDIO */
	{ 0x0c, KEY_ZOOM },		/* FULL SCREEN */

	{ 0x12, KEY_SUBTITLE },		/* DISPLAY */
	{ 0x32, KEY_REWIND },		/* LOOP	*/
	{ 0x02, KEY_PRINT },		/* PREVIEW */

	{ 0x2a, KEY_SEARCH },		/* AUTOSCAN */
	{ 0x1a, KEY_SLEEP },		/* FREEZE */
	{ 0x3a, KEY_CAMERA },		/* SNAPSHOT */
	{ 0x0a, KEY_MUTE },		/* MUTE */

	{ 0x26, KEY_RECORD },		/* RECORD */
	{ 0x16, KEY_PAUSE },		/* PAUSE */
	{ 0x36, KEY_STOP },		/* STOP */
	{ 0x06, KEY_PLAY },		/* PLAY */

	{ 0x2e, KEY_RED },		/* RED */
	{ 0x21, KEY_GREEN },		/* GREEN */
	{ 0x0e, KEY_YELLOW },		/* YELLOW */
	{ 0x01, KEY_BLUE },		/* BLUE */

	{ 0x1e, KEY_VOLUMEDOWN },	/* VOLUME- */
	{ 0x3e, KEY_VOLUMEUP },		/* VOLUME+ */
	{ 0x11, KEY_CHANNELDOWN },	/* CHANNEL/PAGE- */
	{ 0x31, KEY_CHANNELUP }		/* CHANNEL/PAGE+ */
};
DEFINE_LEGACY_IR_KEYTABLE(avermedia);
#else
DECLARE_IR_KEYTABLE(avermedia);
#endif
