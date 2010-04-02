/* asus-pc39.h - Keytable for asus_pc39 Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * Marc Fargas <telenieko@telenieko.com>
 * this is the remote control that comes with the asus p7131
 * which has a label saying is "Model PC-39"
 */

#ifdef IR_KEYMAPS
static struct ir_scancode asus_pc39[] = {
	/* Keys 0 to 9 */
	{ 0x15, KEY_0 },
	{ 0x29, KEY_1 },
	{ 0x2d, KEY_2 },
	{ 0x2b, KEY_3 },
	{ 0x09, KEY_4 },
	{ 0x0d, KEY_5 },
	{ 0x0b, KEY_6 },
	{ 0x31, KEY_7 },
	{ 0x35, KEY_8 },
	{ 0x33, KEY_9 },

	{ 0x3e, KEY_RADIO },		/* radio */
	{ 0x03, KEY_MENU },		/* dvd/menu */
	{ 0x2a, KEY_VOLUMEUP },
	{ 0x19, KEY_VOLUMEDOWN },
	{ 0x37, KEY_UP },
	{ 0x3b, KEY_DOWN },
	{ 0x27, KEY_LEFT },
	{ 0x2f, KEY_RIGHT },
	{ 0x25, KEY_VIDEO },		/* video */
	{ 0x39, KEY_AUDIO },		/* music */

	{ 0x21, KEY_TV },		/* tv */
	{ 0x1d, KEY_EXIT },		/* back */
	{ 0x0a, KEY_CHANNELUP },	/* channel / program + */
	{ 0x1b, KEY_CHANNELDOWN },	/* channel / program - */
	{ 0x1a, KEY_ENTER },		/* enter */

	{ 0x06, KEY_PAUSE },		/* play/pause */
	{ 0x1e, KEY_PREVIOUS },		/* rew */
	{ 0x26, KEY_NEXT },		/* forward */
	{ 0x0e, KEY_REWIND },		/* backward << */
	{ 0x3a, KEY_FASTFORWARD },	/* forward >> */
	{ 0x36, KEY_STOP },
	{ 0x2e, KEY_RECORD },		/* recording */
	{ 0x16, KEY_POWER },		/* the button that reads "close" */

	{ 0x11, KEY_ZOOM },		/* full screen */
	{ 0x13, KEY_MACRO },		/* recall */
	{ 0x23, KEY_HOME },		/* home */
	{ 0x05, KEY_PVR },		/* picture */
	{ 0x3d, KEY_MUTE },		/* mute */
	{ 0x01, KEY_DVD },		/* dvd */
};
DEFINE_LEGACY_IR_KEYTABLE(asus_pc39);
#else
DECLARE_IR_KEYTABLE(asus_pc39);
#endif
