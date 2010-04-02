/* dm1105-nec.h - Keytable for dm1105_nec Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* DVBWorld remotes
   Igor M. Liplianin <liplianin@me.by>
 */

#ifdef IR_KEYMAPS
static struct ir_scancode dm1105_nec[] = {
	{ 0x0a, KEY_POWER2},		/* power */
	{ 0x0c, KEY_MUTE},		/* mute */
	{ 0x11, KEY_1},
	{ 0x12, KEY_2},
	{ 0x13, KEY_3},
	{ 0x14, KEY_4},
	{ 0x15, KEY_5},
	{ 0x16, KEY_6},
	{ 0x17, KEY_7},
	{ 0x18, KEY_8},
	{ 0x19, KEY_9},
	{ 0x10, KEY_0},
	{ 0x1c, KEY_CHANNELUP},		/* ch+ */
	{ 0x0f, KEY_CHANNELDOWN},	/* ch- */
	{ 0x1a, KEY_VOLUMEUP},		/* vol+ */
	{ 0x0e, KEY_VOLUMEDOWN},	/* vol- */
	{ 0x04, KEY_RECORD},		/* rec */
	{ 0x09, KEY_CHANNEL},		/* fav */
	{ 0x08, KEY_BACKSPACE},		/* rewind */
	{ 0x07, KEY_FASTFORWARD},	/* fast */
	{ 0x0b, KEY_PAUSE},		/* pause */
	{ 0x02, KEY_ESC},		/* cancel */
	{ 0x03, KEY_TAB},		/* tab */
	{ 0x00, KEY_UP},		/* up */
	{ 0x1f, KEY_ENTER},		/* ok */
	{ 0x01, KEY_DOWN},		/* down */
	{ 0x05, KEY_RECORD},		/* cap */
	{ 0x06, KEY_STOP},		/* stop */
	{ 0x40, KEY_ZOOM},		/* full */
	{ 0x1e, KEY_TV},		/* tvmode */
	{ 0x1b, KEY_B},			/* recall */
};
DEFINE_LEGACY_IR_KEYTABLE(dm1105_nec);
#else
DECLARE_IR_KEYTABLE(dm1105_nec);
#endif
