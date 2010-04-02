/* encore-enltv2.h - Keytable for encore_enltv2 Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Encore ENLTV2-FM  - silver plastic - "Wand Media" written at the botton
    Mauro Carvalho Chehab <mchehab@infradead.org> */

#ifdef IR_KEYMAPS
static struct ir_scancode encore_enltv2[] = {
	{ 0x4c, KEY_POWER2 },
	{ 0x4a, KEY_TUNER },
	{ 0x40, KEY_1 },
	{ 0x60, KEY_2 },
	{ 0x50, KEY_3 },
	{ 0x70, KEY_4 },
	{ 0x48, KEY_5 },
	{ 0x68, KEY_6 },
	{ 0x58, KEY_7 },
	{ 0x78, KEY_8 },
	{ 0x44, KEY_9 },
	{ 0x54, KEY_0 },

	{ 0x64, KEY_LAST },		/* +100 */
	{ 0x4e, KEY_AGAIN },		/* Recall */

	{ 0x6c, KEY_SWITCHVIDEOMODE },	/* Video Source */
	{ 0x5e, KEY_MENU },
	{ 0x56, KEY_SCREEN },
	{ 0x7a, KEY_SETUP },

	{ 0x46, KEY_MUTE },
	{ 0x5c, KEY_MODE },		/* Stereo */
	{ 0x74, KEY_INFO },
	{ 0x7c, KEY_CLEAR },

	{ 0x55, KEY_UP },
	{ 0x49, KEY_DOWN },
	{ 0x7e, KEY_LEFT },
	{ 0x59, KEY_RIGHT },
	{ 0x6a, KEY_ENTER },

	{ 0x42, KEY_VOLUMEUP },
	{ 0x62, KEY_VOLUMEDOWN },
	{ 0x52, KEY_CHANNELUP },
	{ 0x72, KEY_CHANNELDOWN },

	{ 0x41, KEY_RECORD },
	{ 0x51, KEY_CAMERA },		/* Snapshot */
	{ 0x75, KEY_TIME },		/* Timeshift */
	{ 0x71, KEY_TV2 },		/* PIP */

	{ 0x45, KEY_REWIND },
	{ 0x6f, KEY_PAUSE },
	{ 0x7d, KEY_FORWARD },
	{ 0x79, KEY_STOP },
};
DEFINE_LEGACY_IR_KEYTABLE(encore_enltv2);
#else
DECLARE_IR_KEYTABLE(encore_enltv2);
#endif
