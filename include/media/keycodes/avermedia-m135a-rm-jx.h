/* avermedia-m135a-rm-jx.h - Keytable for avermedia_m135a_rm_jx Remote Controller
 *
 * Imported from ir-keymaps.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 * Avermedia M135A with IR model RM-JX
 * The same codes exist on both Positivo (BR) and original IR
 * Mauro Carvalho Chehab <mchehab@infradead.org>
 */

#ifdef IR_KEYMAPS
static struct ir_scancode avermedia_m135a_rm_jx[] = {
	{ 0x0200, KEY_POWER2 },
	{ 0x022e, KEY_DOT },		/* '.' */
	{ 0x0201, KEY_MODE },		/* TV/FM or SOURCE */

	{ 0x0205, KEY_1 },
	{ 0x0206, KEY_2 },
	{ 0x0207, KEY_3 },
	{ 0x0209, KEY_4 },
	{ 0x020a, KEY_5 },
	{ 0x020b, KEY_6 },
	{ 0x020d, KEY_7 },
	{ 0x020e, KEY_8 },
	{ 0x020f, KEY_9 },
	{ 0x0211, KEY_0 },

	{ 0x0213, KEY_RIGHT },		/* -> or L */
	{ 0x0212, KEY_LEFT },		/* <- or R */

	{ 0x0217, KEY_SLEEP },		/* Capturar Imagem or Snapshot */
	{ 0x0210, KEY_SHUFFLE },	/* Amostra or 16 chan prev */

	{ 0x0303, KEY_CHANNELUP },
	{ 0x0302, KEY_CHANNELDOWN },
	{ 0x021f, KEY_VOLUMEUP },
	{ 0x021e, KEY_VOLUMEDOWN },
	{ 0x020c, KEY_ENTER },		/* Full Screen */

	{ 0x0214, KEY_MUTE },
	{ 0x0208, KEY_AUDIO },

	{ 0x0203, KEY_TEXT },		/* Teletext */
	{ 0x0204, KEY_EPG },
	{ 0x022b, KEY_TV2 },		/* TV2 or PIP */

	{ 0x021d, KEY_RED },
	{ 0x021c, KEY_YELLOW },
	{ 0x0301, KEY_GREEN },
	{ 0x0300, KEY_BLUE },

	{ 0x021a, KEY_PLAYPAUSE },
	{ 0x0219, KEY_RECORD },
	{ 0x0218, KEY_PLAY },
	{ 0x021b, KEY_STOP },
};
DEFINE_IR_KEYTABLE(avermedia_m135a_rm_jx, IR_TYPE_NEC);
#else
DECLARE_IR_KEYTABLE(avermedia_m135a_rm_jx);
#endif
