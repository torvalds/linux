/* evga-indtube.h - Keytable for evga_indtube Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* EVGA inDtube
   Devin Heitmueller <devin.heitmueller@gmail.com>
 */

#ifdef IR_KEYMAPS
static struct ir_scancode evga_indtube[] = {
	{ 0x12, KEY_POWER},
	{ 0x02, KEY_MODE},	/* TV */
	{ 0x14, KEY_MUTE},
	{ 0x1a, KEY_CHANNELUP},
	{ 0x16, KEY_TV2},	/* PIP */
	{ 0x1d, KEY_VOLUMEUP},
	{ 0x05, KEY_CHANNELDOWN},
	{ 0x0f, KEY_PLAYPAUSE},
	{ 0x19, KEY_VOLUMEDOWN},
	{ 0x1c, KEY_REWIND},
	{ 0x0d, KEY_RECORD},
	{ 0x18, KEY_FORWARD},
	{ 0x1e, KEY_PREVIOUS},
	{ 0x1b, KEY_STOP},
	{ 0x1f, KEY_NEXT},
	{ 0x13, KEY_CAMERA},
};
DEFINE_LEGACY_IR_KEYTABLE(evga_indtube);
#else
DECLARE_IR_KEYTABLE(evga_indtube);
#endif
