/* npgtech.h - Keytable for npgtech Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifdef IR_KEYMAPS
static struct ir_scancode npgtech[] = {
	{ 0x1d, KEY_SWITCHVIDEOMODE },	/* switch inputs */
	{ 0x2a, KEY_FRONT },

	{ 0x3e, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x06, KEY_3 },
	{ 0x0a, KEY_4 },
	{ 0x0e, KEY_5 },
	{ 0x12, KEY_6 },
	{ 0x16, KEY_7 },
	{ 0x1a, KEY_8 },
	{ 0x1e, KEY_9 },
	{ 0x3a, KEY_0 },
	{ 0x22, KEY_NUMLOCK },		/* -/-- */
	{ 0x20, KEY_REFRESH },

	{ 0x03, KEY_BRIGHTNESSDOWN },
	{ 0x28, KEY_AUDIO },
	{ 0x3c, KEY_CHANNELUP },
	{ 0x3f, KEY_VOLUMEDOWN },
	{ 0x2e, KEY_MUTE },
	{ 0x3b, KEY_VOLUMEUP },
	{ 0x00, KEY_CHANNELDOWN },
	{ 0x07, KEY_BRIGHTNESSUP },
	{ 0x2c, KEY_TEXT },

	{ 0x37, KEY_RECORD },
	{ 0x17, KEY_PLAY },
	{ 0x13, KEY_PAUSE },
	{ 0x26, KEY_STOP },
	{ 0x18, KEY_FASTFORWARD },
	{ 0x14, KEY_REWIND },
	{ 0x33, KEY_ZOOM },
	{ 0x32, KEY_KEYBOARD },
	{ 0x30, KEY_GOTO },		/* Pointing arrow */
	{ 0x36, KEY_MACRO },		/* Maximize/Minimize (yellow) */
	{ 0x0b, KEY_RADIO },
	{ 0x10, KEY_POWER },

};
DEFINE_LEGACY_IR_KEYTABLE(npgtech);
#else
DECLARE_IR_KEYTABLE(npgtech);
#endif
