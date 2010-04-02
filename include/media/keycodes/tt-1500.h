/* tt-1500.h - Keytable for tt_1500 Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* for the Technotrend 1500 bundled remotes (grey and black): */

#ifdef IR_KEYMAPS
static struct ir_scancode tt_1500[] = {
	{ 0x01, KEY_POWER },
	{ 0x02, KEY_SHUFFLE },		/* ? double-arrow key */
	{ 0x03, KEY_1 },
	{ 0x04, KEY_2 },
	{ 0x05, KEY_3 },
	{ 0x06, KEY_4 },
	{ 0x07, KEY_5 },
	{ 0x08, KEY_6 },
	{ 0x09, KEY_7 },
	{ 0x0a, KEY_8 },
	{ 0x0b, KEY_9 },
	{ 0x0c, KEY_0 },
	{ 0x0d, KEY_UP },
	{ 0x0e, KEY_LEFT },
	{ 0x0f, KEY_OK },
	{ 0x10, KEY_RIGHT },
	{ 0x11, KEY_DOWN },
	{ 0x12, KEY_INFO },
	{ 0x13, KEY_EXIT },
	{ 0x14, KEY_RED },
	{ 0x15, KEY_GREEN },
	{ 0x16, KEY_YELLOW },
	{ 0x17, KEY_BLUE },
	{ 0x18, KEY_MUTE },
	{ 0x19, KEY_TEXT },
	{ 0x1a, KEY_MODE },		/* ? TV/Radio */
	{ 0x21, KEY_OPTION },
	{ 0x22, KEY_EPG },
	{ 0x23, KEY_CHANNELUP },
	{ 0x24, KEY_CHANNELDOWN },
	{ 0x25, KEY_VOLUMEUP },
	{ 0x26, KEY_VOLUMEDOWN },
	{ 0x27, KEY_SETUP },
	{ 0x3a, KEY_RECORD },		/* these keys are only in the black remote */
	{ 0x3b, KEY_PLAY },
	{ 0x3c, KEY_STOP },
	{ 0x3d, KEY_REWIND },
	{ 0x3e, KEY_PAUSE },
	{ 0x3f, KEY_FORWARD },
};
DEFINE_LEGACY_IR_KEYTABLE(tt_1500);
#else
DECLARE_IR_KEYTABLE(tt_1500);
#endif
