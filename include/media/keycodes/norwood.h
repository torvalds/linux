/* norwood.h - Keytable for norwood Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Norwood Micro (non-Pro) TV Tuner
   By Peter Naulls <peter@chocky.org>
   Key comments are the functions given in the manual */

#ifdef IR_KEYMAPS
static struct ir_scancode norwood[] = {
	/* Keys 0 to 9 */
	{ 0x20, KEY_0 },
	{ 0x21, KEY_1 },
	{ 0x22, KEY_2 },
	{ 0x23, KEY_3 },
	{ 0x24, KEY_4 },
	{ 0x25, KEY_5 },
	{ 0x26, KEY_6 },
	{ 0x27, KEY_7 },
	{ 0x28, KEY_8 },
	{ 0x29, KEY_9 },

	{ 0x78, KEY_TUNER },		/* Video Source        */
	{ 0x2c, KEY_EXIT },		/* Open/Close software */
	{ 0x2a, KEY_SELECT },		/* 2 Digit Select      */
	{ 0x69, KEY_AGAIN },		/* Recall              */

	{ 0x32, KEY_BRIGHTNESSUP },	/* Brightness increase */
	{ 0x33, KEY_BRIGHTNESSDOWN },	/* Brightness decrease */
	{ 0x6b, KEY_KPPLUS },		/* (not named >>>>>)   */
	{ 0x6c, KEY_KPMINUS },		/* (not named <<<<<)   */

	{ 0x2d, KEY_MUTE },		/* Mute                */
	{ 0x30, KEY_VOLUMEUP },		/* Volume up           */
	{ 0x31, KEY_VOLUMEDOWN },	/* Volume down         */
	{ 0x60, KEY_CHANNELUP },	/* Channel up          */
	{ 0x61, KEY_CHANNELDOWN },	/* Channel down        */

	{ 0x3f, KEY_RECORD },		/* Record              */
	{ 0x37, KEY_PLAY },		/* Play                */
	{ 0x36, KEY_PAUSE },		/* Pause               */
	{ 0x2b, KEY_STOP },		/* Stop                */
	{ 0x67, KEY_FASTFORWARD },	/* Foward              */
	{ 0x66, KEY_REWIND },		/* Rewind              */
	{ 0x3e, KEY_SEARCH },		/* Auto Scan           */
	{ 0x2e, KEY_CAMERA },		/* Capture Video       */
	{ 0x6d, KEY_MENU },		/* Show/Hide Control   */
	{ 0x2f, KEY_ZOOM },		/* Full Screen         */
	{ 0x34, KEY_RADIO },		/* FM                  */
	{ 0x65, KEY_POWER },		/* Computer power      */
};
DEFINE_LEGACY_IR_KEYTABLE(norwood);
#else
DECLARE_IR_KEYTABLE(norwood);
#endif
