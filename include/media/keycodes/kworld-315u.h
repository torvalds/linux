/* kworld-315u.h - Keytable for kworld_315u Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Kworld 315U
 */

#ifdef IR_KEYMAPS
static struct ir_scancode kworld_315u[] = {
	{ 0x6143, KEY_POWER },
	{ 0x6101, KEY_TUNER },		/* source */
	{ 0x610b, KEY_ZOOM },
	{ 0x6103, KEY_POWER2 },		/* shutdown */

	{ 0x6104, KEY_1 },
	{ 0x6108, KEY_2 },
	{ 0x6102, KEY_3 },
	{ 0x6109, KEY_CHANNELUP },

	{ 0x610f, KEY_4 },
	{ 0x6105, KEY_5 },
	{ 0x6106, KEY_6 },
	{ 0x6107, KEY_CHANNELDOWN },

	{ 0x610c, KEY_7 },
	{ 0x610d, KEY_8 },
	{ 0x610a, KEY_9 },
	{ 0x610e, KEY_VOLUMEUP },

	{ 0x6110, KEY_LAST },
	{ 0x6111, KEY_0 },
	{ 0x6112, KEY_ENTER },
	{ 0x6113, KEY_VOLUMEDOWN },

	{ 0x6114, KEY_RECORD },
	{ 0x6115, KEY_STOP },
	{ 0x6116, KEY_PLAY },
	{ 0x6117, KEY_MUTE },

	{ 0x6118, KEY_UP },
	{ 0x6119, KEY_DOWN },
	{ 0x611a, KEY_LEFT },
	{ 0x611b, KEY_RIGHT },

	{ 0x611c, KEY_RED },
	{ 0x611d, KEY_GREEN },
	{ 0x611e, KEY_YELLOW },
	{ 0x611f, KEY_BLUE },
};
DEFINE_IR_KEYTABLE(kworld_315u, IR_TYPE_NEC);
#else
DECLARE_IR_KEYTABLE(kworld_315u);
#endif
