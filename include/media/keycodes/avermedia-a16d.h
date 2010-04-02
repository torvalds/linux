/* avermedia-a16d.h - Keytable for avermedia_a16d Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifdef IR_KEYMAPS
static struct ir_scancode avermedia_a16d[] = {
	{ 0x20, KEY_LIST},
	{ 0x00, KEY_POWER},
	{ 0x28, KEY_1},
	{ 0x18, KEY_2},
	{ 0x38, KEY_3},
	{ 0x24, KEY_4},
	{ 0x14, KEY_5},
	{ 0x34, KEY_6},
	{ 0x2c, KEY_7},
	{ 0x1c, KEY_8},
	{ 0x3c, KEY_9},
	{ 0x12, KEY_SUBTITLE},
	{ 0x22, KEY_0},
	{ 0x32, KEY_REWIND},
	{ 0x3a, KEY_SHUFFLE},
	{ 0x02, KEY_PRINT},
	{ 0x11, KEY_CHANNELDOWN},
	{ 0x31, KEY_CHANNELUP},
	{ 0x0c, KEY_ZOOM},
	{ 0x1e, KEY_VOLUMEDOWN},
	{ 0x3e, KEY_VOLUMEUP},
	{ 0x0a, KEY_MUTE},
	{ 0x04, KEY_AUDIO},
	{ 0x26, KEY_RECORD},
	{ 0x06, KEY_PLAY},
	{ 0x36, KEY_STOP},
	{ 0x16, KEY_PAUSE},
	{ 0x2e, KEY_REWIND},
	{ 0x0e, KEY_FASTFORWARD},
	{ 0x30, KEY_TEXT},
	{ 0x21, KEY_GREEN},
	{ 0x01, KEY_BLUE},
	{ 0x08, KEY_EPG},
	{ 0x2a, KEY_MENU},
};
DEFINE_LEGACY_IR_KEYTABLE(avermedia_a16d);
#else
DECLARE_IR_KEYTABLE(avermedia_a16d);
#endif
