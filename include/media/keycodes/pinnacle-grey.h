/* pinnacle-grey.h - Keytable for pinnacle_grey Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifdef IR_KEYMAPS
static struct ir_scancode pinnacle_grey[] = {
	{ 0x3a, KEY_0 },
	{ 0x31, KEY_1 },
	{ 0x32, KEY_2 },
	{ 0x33, KEY_3 },
	{ 0x34, KEY_4 },
	{ 0x35, KEY_5 },
	{ 0x36, KEY_6 },
	{ 0x37, KEY_7 },
	{ 0x38, KEY_8 },
	{ 0x39, KEY_9 },

	{ 0x2f, KEY_POWER },

	{ 0x2e, KEY_P },
	{ 0x1f, KEY_L },
	{ 0x2b, KEY_I },

	{ 0x2d, KEY_SCREEN },
	{ 0x1e, KEY_ZOOM },
	{ 0x1b, KEY_VOLUMEUP },
	{ 0x0f, KEY_VOLUMEDOWN },
	{ 0x17, KEY_CHANNELUP },
	{ 0x1c, KEY_CHANNELDOWN },
	{ 0x25, KEY_INFO },

	{ 0x3c, KEY_MUTE },

	{ 0x3d, KEY_LEFT },
	{ 0x3b, KEY_RIGHT },

	{ 0x3f, KEY_UP },
	{ 0x3e, KEY_DOWN },
	{ 0x1a, KEY_ENTER },

	{ 0x1d, KEY_MENU },
	{ 0x19, KEY_AGAIN },
	{ 0x16, KEY_PREVIOUSSONG },
	{ 0x13, KEY_NEXTSONG },
	{ 0x15, KEY_PAUSE },
	{ 0x0e, KEY_REWIND },
	{ 0x0d, KEY_PLAY },
	{ 0x0b, KEY_STOP },
	{ 0x07, KEY_FORWARD },
	{ 0x27, KEY_RECORD },
	{ 0x26, KEY_TUNER },
	{ 0x29, KEY_TEXT },
	{ 0x2a, KEY_MEDIA },
	{ 0x18, KEY_EPG },
};
DEFINE_LEGACY_IR_KEYTABLE(pinnacle_grey);
#else
DECLARE_IR_KEYTABLE(pinnacle_grey);
#endif
