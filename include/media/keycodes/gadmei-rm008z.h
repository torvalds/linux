/* gadmei-rm008z.h - Keytable for gadmei_rm008z Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* GADMEI UTV330+ RM008Z remote
   Shine Liu <shinel@foxmail.com>
 */

#ifdef IR_KEYMAPS
static struct ir_scancode gadmei_rm008z[] = {
	{ 0x14, KEY_POWER2},		/* POWER OFF */
	{ 0x0c, KEY_MUTE},		/* MUTE */

	{ 0x18, KEY_TV},		/* TV */
	{ 0x0e, KEY_VIDEO},		/* AV */
	{ 0x0b, KEY_AUDIO},		/* SV */
	{ 0x0f, KEY_RADIO},		/* FM */

	{ 0x00, KEY_1},
	{ 0x01, KEY_2},
	{ 0x02, KEY_3},
	{ 0x03, KEY_4},
	{ 0x04, KEY_5},
	{ 0x05, KEY_6},
	{ 0x06, KEY_7},
	{ 0x07, KEY_8},
	{ 0x08, KEY_9},
	{ 0x09, KEY_0},
	{ 0x0a, KEY_INFO},		/* OSD */
	{ 0x1c, KEY_BACKSPACE},		/* LAST */

	{ 0x0d, KEY_PLAY},		/* PLAY */
	{ 0x1e, KEY_CAMERA},		/* SNAPSHOT */
	{ 0x1a, KEY_RECORD},		/* RECORD */
	{ 0x17, KEY_STOP},		/* STOP */

	{ 0x1f, KEY_UP},		/* UP */
	{ 0x44, KEY_DOWN},		/* DOWN */
	{ 0x46, KEY_TAB},		/* BACK */
	{ 0x4a, KEY_ZOOM},		/* FULLSECREEN */

	{ 0x10, KEY_VOLUMEUP},		/* VOLUMEUP */
	{ 0x11, KEY_VOLUMEDOWN},	/* VOLUMEDOWN */
	{ 0x12, KEY_CHANNELUP},		/* CHANNELUP */
	{ 0x13, KEY_CHANNELDOWN},	/* CHANNELDOWN */
	{ 0x15, KEY_ENTER},		/* OK */
};
DEFINE_LEGACY_IR_KEYTABLE(gadmei_rm008z);
#else
DECLARE_IR_KEYTABLE(gadmei_rm008z);
#endif
