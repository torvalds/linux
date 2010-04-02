/* powercolor-real-angel.h - Keytable for powercolor_real_angel Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * Remote control for Powercolor Real Angel 330
 * Daniel Fraga <fragabr@gmail.com>
 */

#ifdef IR_KEYMAPS
static struct ir_scancode powercolor_real_angel[] = {
	{ 0x38, KEY_SWITCHVIDEOMODE },	/* switch inputs */
	{ 0x0c, KEY_MEDIA },		/* Turn ON/OFF App */
	{ 0x00, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x0a, KEY_DIGITS },		/* single, double, tripple digit */
	{ 0x29, KEY_PREVIOUS },		/* previous channel */
	{ 0x12, KEY_BRIGHTNESSUP },
	{ 0x13, KEY_BRIGHTNESSDOWN },
	{ 0x2b, KEY_MODE },		/* stereo/mono */
	{ 0x2c, KEY_TEXT },		/* teletext */
	{ 0x20, KEY_CHANNELUP },	/* channel up */
	{ 0x21, KEY_CHANNELDOWN },	/* channel down */
	{ 0x10, KEY_VOLUMEUP },		/* volume up */
	{ 0x11, KEY_VOLUMEDOWN },	/* volume down */
	{ 0x0d, KEY_MUTE },
	{ 0x1f, KEY_RECORD },
	{ 0x17, KEY_PLAY },
	{ 0x16, KEY_PAUSE },
	{ 0x0b, KEY_STOP },
	{ 0x27, KEY_FASTFORWARD },
	{ 0x26, KEY_REWIND },
	{ 0x1e, KEY_SEARCH },		/* autoscan */
	{ 0x0e, KEY_CAMERA },		/* snapshot */
	{ 0x2d, KEY_SETUP },
	{ 0x0f, KEY_SCREEN },		/* full screen */
	{ 0x14, KEY_RADIO },		/* FM radio */
	{ 0x25, KEY_POWER },		/* power */
};
DEFINE_LEGACY_IR_KEYTABLE(powercolor_real_angel);
#else
DECLARE_IR_KEYTABLE(powercolor_real_angel);
#endif
