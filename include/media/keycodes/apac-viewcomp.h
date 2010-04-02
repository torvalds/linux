/* apac-viewcomp.h - Keytable for apac_viewcomp Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Attila Kondoros <attila.kondoros@chello.hu> */

#ifdef IR_KEYMAPS
static struct ir_scancode apac_viewcomp[] = {

	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },
	{ 0x00, KEY_0 },
	{ 0x17, KEY_LAST },		/* +100 */
	{ 0x0a, KEY_LIST },		/* recall */


	{ 0x1c, KEY_TUNER },		/* TV/FM */
	{ 0x15, KEY_SEARCH },		/* scan */
	{ 0x12, KEY_POWER },		/* power */
	{ 0x1f, KEY_VOLUMEDOWN },	/* vol up */
	{ 0x1b, KEY_VOLUMEUP },		/* vol down */
	{ 0x1e, KEY_CHANNELDOWN },	/* chn up */
	{ 0x1a, KEY_CHANNELUP },	/* chn down */

	{ 0x11, KEY_VIDEO },		/* video */
	{ 0x0f, KEY_ZOOM },		/* full screen */
	{ 0x13, KEY_MUTE },		/* mute/unmute */
	{ 0x10, KEY_TEXT },		/* min */

	{ 0x0d, KEY_STOP },		/* freeze */
	{ 0x0e, KEY_RECORD },		/* record */
	{ 0x1d, KEY_PLAYPAUSE },	/* stop */
	{ 0x19, KEY_PLAY },		/* play */

	{ 0x16, KEY_GOTO },		/* osd */
	{ 0x14, KEY_REFRESH },		/* default */
	{ 0x0c, KEY_KPPLUS },		/* fine tune >>>> */
	{ 0x18, KEY_KPMINUS },		/* fine tune <<<< */
};
DEFINE_LEGACY_IR_KEYTABLE(apac_viewcomp);
#else
DECLARE_IR_KEYTABLE(apac_viewcomp);
#endif
