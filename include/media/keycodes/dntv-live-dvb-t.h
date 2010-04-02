/* dntv-live-dvb-t.h - Keytable for dntv_live_dvb_t Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* DigitalNow DNTV Live DVB-T Remote */

#ifdef IR_KEYMAPS
static struct ir_scancode dntv_live_dvb_t[] = {
	{ 0x00, KEY_ESC },		/* 'go up a level?' */
	/* Keys 0 to 9 */
	{ 0x0a, KEY_0 },
	{ 0x01, KEY_1 },
	{ 0x02, KEY_2 },
	{ 0x03, KEY_3 },
	{ 0x04, KEY_4 },
	{ 0x05, KEY_5 },
	{ 0x06, KEY_6 },
	{ 0x07, KEY_7 },
	{ 0x08, KEY_8 },
	{ 0x09, KEY_9 },

	{ 0x0b, KEY_TUNER },		/* tv/fm */
	{ 0x0c, KEY_SEARCH },		/* scan */
	{ 0x0d, KEY_STOP },
	{ 0x0e, KEY_PAUSE },
	{ 0x0f, KEY_LIST },		/* source */

	{ 0x10, KEY_MUTE },
	{ 0x11, KEY_REWIND },		/* backward << */
	{ 0x12, KEY_POWER },
	{ 0x13, KEY_CAMERA },		/* snap */
	{ 0x14, KEY_AUDIO },		/* stereo */
	{ 0x15, KEY_CLEAR },		/* reset */
	{ 0x16, KEY_PLAY },
	{ 0x17, KEY_ENTER },
	{ 0x18, KEY_ZOOM },		/* full screen */
	{ 0x19, KEY_FASTFORWARD },	/* forward >> */
	{ 0x1a, KEY_CHANNELUP },
	{ 0x1b, KEY_VOLUMEUP },
	{ 0x1c, KEY_INFO },		/* preview */
	{ 0x1d, KEY_RECORD },		/* record */
	{ 0x1e, KEY_CHANNELDOWN },
	{ 0x1f, KEY_VOLUMEDOWN },
};
DEFINE_LEGACY_IR_KEYTABLE(dntv_live_dvb_t);
#else
DECLARE_IR_KEYTABLE(dntv_live_dvb_t);
#endif
