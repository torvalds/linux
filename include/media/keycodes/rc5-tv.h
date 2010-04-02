/* rc5-tv.h - Keytable for rc5_tv Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* generic RC5 keytable                                          */
/* see http://users.pandora.be/nenya/electronics/rc5/codes00.htm */
/* used by old (black) Hauppauge remotes                         */

#ifdef IR_KEYMAPS
static struct ir_scancode rc5_tv[] = {
	/* Keys 0 to 9 */
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

	{ 0x0b, KEY_CHANNEL },		/* channel / program (japan: 11) */
	{ 0x0c, KEY_POWER },		/* standby */
	{ 0x0d, KEY_MUTE },		/* mute / demute */
	{ 0x0f, KEY_TV },		/* display */
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x12, KEY_BRIGHTNESSUP },
	{ 0x13, KEY_BRIGHTNESSDOWN },
	{ 0x1e, KEY_SEARCH },		/* search + */
	{ 0x20, KEY_CHANNELUP },	/* channel / program + */
	{ 0x21, KEY_CHANNELDOWN },	/* channel / program - */
	{ 0x22, KEY_CHANNEL },		/* alt / channel */
	{ 0x23, KEY_LANGUAGE },		/* 1st / 2nd language */
	{ 0x26, KEY_SLEEP },		/* sleeptimer */
	{ 0x2e, KEY_MENU },		/* 2nd controls (USA: menu) */
	{ 0x30, KEY_PAUSE },
	{ 0x32, KEY_REWIND },
	{ 0x33, KEY_GOTO },
	{ 0x35, KEY_PLAY },
	{ 0x36, KEY_STOP },
	{ 0x37, KEY_RECORD },		/* recording */
	{ 0x3c, KEY_TEXT },		/* teletext submode (Japan: 12) */
	{ 0x3d, KEY_SUSPEND },		/* system standby */

};
DEFINE_LEGACY_IR_KEYTABLE(rc5_tv);
#else
DECLARE_IR_KEYTABLE(rc5_tv);
#endif
