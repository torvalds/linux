/* budget-ci-old.h - Keytable for budget_ci_old Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* From reading the following remotes:
 * Zenith Universal 7 / TV Mode 807 / VCR Mode 837
 * Hauppauge (from NOVA-CI-s box product)
 * This is a "middle of the road" approach, differences are noted
 */

#ifdef IR_KEYMAPS
static struct ir_scancode budget_ci_old[] = {
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
	{ 0x0a, KEY_ENTER },
	{ 0x0b, KEY_RED },
	{ 0x0c, KEY_POWER },		/* RADIO on Hauppauge */
	{ 0x0d, KEY_MUTE },
	{ 0x0f, KEY_A },		/* TV on Hauppauge */
	{ 0x10, KEY_VOLUMEUP },
	{ 0x11, KEY_VOLUMEDOWN },
	{ 0x14, KEY_B },
	{ 0x1c, KEY_UP },
	{ 0x1d, KEY_DOWN },
	{ 0x1e, KEY_OPTION },		/* RESERVED on Hauppauge */
	{ 0x1f, KEY_BREAK },
	{ 0x20, KEY_CHANNELUP },
	{ 0x21, KEY_CHANNELDOWN },
	{ 0x22, KEY_PREVIOUS },		/* Prev Ch on Zenith, SOURCE on Hauppauge */
	{ 0x24, KEY_RESTART },
	{ 0x25, KEY_OK },
	{ 0x26, KEY_CYCLEWINDOWS },	/* MINIMIZE on Hauppauge */
	{ 0x28, KEY_ENTER },		/* VCR mode on Zenith */
	{ 0x29, KEY_PAUSE },
	{ 0x2b, KEY_RIGHT },
	{ 0x2c, KEY_LEFT },
	{ 0x2e, KEY_MENU },		/* FULL SCREEN on Hauppauge */
	{ 0x30, KEY_SLOW },
	{ 0x31, KEY_PREVIOUS },		/* VCR mode on Zenith */
	{ 0x32, KEY_REWIND },
	{ 0x34, KEY_FASTFORWARD },
	{ 0x35, KEY_PLAY },
	{ 0x36, KEY_STOP },
	{ 0x37, KEY_RECORD },
	{ 0x38, KEY_TUNER },		/* TV/VCR on Zenith */
	{ 0x3a, KEY_C },
	{ 0x3c, KEY_EXIT },
	{ 0x3d, KEY_POWER2 },
	{ 0x3e, KEY_TUNER },
};
DEFINE_LEGACY_IR_KEYTABLE(budget_ci_old);
#else
DECLARE_IR_KEYTABLE(budget_ci_old);
#endif
