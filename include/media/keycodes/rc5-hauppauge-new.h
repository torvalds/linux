/* rc5-hauppauge-new.h - Keytable for rc5_hauppauge_new Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * Hauppauge:the newer, gray remotes (seems there are multiple
 * slightly different versions), shipped with cx88+ivtv cards.
 *
 * This table contains the complete RC5 code, instead of just the data part
 */

#ifdef IR_KEYMAPS
static struct ir_scancode rc5_hauppauge_new[] = {
	/* Keys 0 to 9 */
	{ 0x1e00, KEY_0 },
	{ 0x1e01, KEY_1 },
	{ 0x1e02, KEY_2 },
	{ 0x1e03, KEY_3 },
	{ 0x1e04, KEY_4 },
	{ 0x1e05, KEY_5 },
	{ 0x1e06, KEY_6 },
	{ 0x1e07, KEY_7 },
	{ 0x1e08, KEY_8 },
	{ 0x1e09, KEY_9 },

	{ 0x1e0a, KEY_TEXT },		/* keypad asterisk as well */
	{ 0x1e0b, KEY_RED },		/* red button */
	{ 0x1e0c, KEY_RADIO },
	{ 0x1e0d, KEY_MENU },
	{ 0x1e0e, KEY_SUBTITLE },		/* also the # key */
	{ 0x1e0f, KEY_MUTE },
	{ 0x1e10, KEY_VOLUMEUP },
	{ 0x1e11, KEY_VOLUMEDOWN },
	{ 0x1e12, KEY_PREVIOUS },		/* previous channel */
	{ 0x1e14, KEY_UP },
	{ 0x1e15, KEY_DOWN },
	{ 0x1e16, KEY_LEFT },
	{ 0x1e17, KEY_RIGHT },
	{ 0x1e18, KEY_VIDEO },		/* Videos */
	{ 0x1e19, KEY_AUDIO },		/* Music */
	/* 0x1e1a: Pictures - presume this means
	   "Multimedia Home Platform" -
	   no "PICTURES" key in input.h
	 */
	{ 0x1e1a, KEY_MHP },

	{ 0x1e1b, KEY_EPG },		/* Guide */
	{ 0x1e1c, KEY_TV },
	{ 0x1e1e, KEY_NEXTSONG },		/* skip >| */
	{ 0x1e1f, KEY_EXIT },		/* back/exit */
	{ 0x1e20, KEY_CHANNELUP },	/* channel / program + */
	{ 0x1e21, KEY_CHANNELDOWN },	/* channel / program - */
	{ 0x1e22, KEY_CHANNEL },		/* source (old black remote) */
	{ 0x1e24, KEY_PREVIOUSSONG },	/* replay |< */
	{ 0x1e25, KEY_ENTER },		/* OK */
	{ 0x1e26, KEY_SLEEP },		/* minimize (old black remote) */
	{ 0x1e29, KEY_BLUE },		/* blue key */
	{ 0x1e2e, KEY_GREEN },		/* green button */
	{ 0x1e30, KEY_PAUSE },		/* pause */
	{ 0x1e32, KEY_REWIND },		/* backward << */
	{ 0x1e34, KEY_FASTFORWARD },	/* forward >> */
	{ 0x1e35, KEY_PLAY },
	{ 0x1e36, KEY_STOP },
	{ 0x1e37, KEY_RECORD },		/* recording */
	{ 0x1e38, KEY_YELLOW },		/* yellow key */
	{ 0x1e3b, KEY_SELECT },		/* top right button */
	{ 0x1e3c, KEY_ZOOM },		/* full */
	{ 0x1e3d, KEY_POWER },		/* system power (green button) */
};
DEFINE_IR_KEYTABLE(rc5_hauppauge_new, IR_TYPE_RC5);
#else
DECLARE_IR_KEYTABLE(rc5_hauppauge_new);
#endif
