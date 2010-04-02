/* purpletv.h - Keytable for purpletv Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifdef IR_KEYMAPS
static struct ir_scancode purpletv[] = {
	{ 0x03, KEY_POWER },
	{ 0x6f, KEY_MUTE },
	{ 0x10, KEY_BACKSPACE },	/* Recall */

	{ 0x11, KEY_0 },
	{ 0x04, KEY_1 },
	{ 0x05, KEY_2 },
	{ 0x06, KEY_3 },
	{ 0x08, KEY_4 },
	{ 0x09, KEY_5 },
	{ 0x0a, KEY_6 },
	{ 0x0c, KEY_7 },
	{ 0x0d, KEY_8 },
	{ 0x0e, KEY_9 },
	{ 0x12, KEY_DOT },	/* 100+ */

	{ 0x07, KEY_VOLUMEUP },
	{ 0x0b, KEY_VOLUMEDOWN },
	{ 0x1a, KEY_KPPLUS },
	{ 0x18, KEY_KPMINUS },
	{ 0x15, KEY_UP },
	{ 0x1d, KEY_DOWN },
	{ 0x0f, KEY_CHANNELUP },
	{ 0x13, KEY_CHANNELDOWN },
	{ 0x48, KEY_ZOOM },

	{ 0x1b, KEY_VIDEO },	/* Video source */
	{ 0x1f, KEY_CAMERA },	/* Snapshot */
	{ 0x49, KEY_LANGUAGE },	/* MTS Select */
	{ 0x19, KEY_SEARCH },	/* Auto Scan */

	{ 0x4b, KEY_RECORD },
	{ 0x46, KEY_PLAY },
	{ 0x45, KEY_PAUSE },	/* Pause */
	{ 0x44, KEY_STOP },
	{ 0x43, KEY_TIME },	/* Time Shift */
	{ 0x17, KEY_CHANNEL },	/* SURF CH */
	{ 0x40, KEY_FORWARD },	/* Forward ? */
	{ 0x42, KEY_REWIND },	/* Backward ? */

};
DEFINE_LEGACY_IR_KEYTABLE(purpletv);
#else
DECLARE_IR_KEYTABLE(purpletv);
#endif
