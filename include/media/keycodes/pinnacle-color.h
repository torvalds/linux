/* pinnacle-color.h - Keytable for pinnacle_color Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifdef IR_KEYMAPS
static struct ir_scancode pinnacle_color[] = {
	{ 0x59, KEY_MUTE },
	{ 0x4a, KEY_POWER },

	{ 0x18, KEY_TEXT },
	{ 0x26, KEY_TV },
	{ 0x3d, KEY_PRINT },

	{ 0x48, KEY_RED },
	{ 0x04, KEY_GREEN },
	{ 0x11, KEY_YELLOW },
	{ 0x00, KEY_BLUE },

	{ 0x2d, KEY_VOLUMEUP },
	{ 0x1e, KEY_VOLUMEDOWN },

	{ 0x49, KEY_MENU },

	{ 0x16, KEY_CHANNELUP },
	{ 0x17, KEY_CHANNELDOWN },

	{ 0x20, KEY_UP },
	{ 0x21, KEY_DOWN },
	{ 0x22, KEY_LEFT },
	{ 0x23, KEY_RIGHT },
	{ 0x0d, KEY_SELECT },

	{ 0x08, KEY_BACK },
	{ 0x07, KEY_REFRESH },

	{ 0x2f, KEY_ZOOM },
	{ 0x29, KEY_RECORD },

	{ 0x4b, KEY_PAUSE },
	{ 0x4d, KEY_REWIND },
	{ 0x2e, KEY_PLAY },
	{ 0x4e, KEY_FORWARD },
	{ 0x53, KEY_PREVIOUS },
	{ 0x4c, KEY_STOP },
	{ 0x54, KEY_NEXT },

	{ 0x69, KEY_0 },
	{ 0x6a, KEY_1 },
	{ 0x6b, KEY_2 },
	{ 0x6c, KEY_3 },
	{ 0x6d, KEY_4 },
	{ 0x6e, KEY_5 },
	{ 0x6f, KEY_6 },
	{ 0x70, KEY_7 },
	{ 0x71, KEY_8 },
	{ 0x72, KEY_9 },

	{ 0x74, KEY_CHANNEL },
	{ 0x0a, KEY_BACKSPACE },
};
DEFINE_LEGACY_IR_KEYTABLE(pinnacle_color);
#else
DECLARE_IR_KEYTABLE(pinnacle_color);
#endif
