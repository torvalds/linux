/*	$OpenBSD: omkbdmap.c,v 1.4 2023/01/23 09:36:39 nicm Exp $	*/

/* Partially from:
 *	$NetBSD: lunaws.c,v 1.6 2002/03/17 19:40:42 atatat Exp $
 *
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tohru Nishimura.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>

#include <luna88k/dev/omkbdmap.h>

#ifdef WSDISPLAY_COMPAT_RAWKBD
#include <dev/wscons/wskbdraw.h>

/*
 * Translate LUNA keycodes to US keyboard XT scancodes, for proper
 * X11-over-wsmux operation.
 */
const u_int8_t omkbd_raw[0x80] = {
	RAWKEY_Null,		/* 0x00 */
	RAWKEY_Null,		/* 0x01 */
	RAWKEY_Null,		/* 0x02 */
	RAWKEY_Null,		/* 0x03 */
	RAWKEY_Null,		/* 0x04 */
	RAWKEY_Null,		/* 0x05 */
	RAWKEY_Null,		/* 0x06 */
	RAWKEY_Null,		/* 0x07 */
	RAWKEY_Null,		/* 0x08 */
	RAWKEY_Tab,		/* 0x09 */
	RAWKEY_Control_L,	/* 0x0a */
	0x70,			/* 0x0b: Kana / No RAWKEY_XXX symbol */
	RAWKEY_Shift_R,		/* 0x0c */
	RAWKEY_Shift_L,		/* 0x0d */
	RAWKEY_Caps_Lock,	/* 0x0e */
	RAWKEY_Alt_L,		/* 0x0f: Zenmen */
	RAWKEY_Escape,		/* 0x10 */
	RAWKEY_BackSpace,	/* 0x11 */
	RAWKEY_Return,		/* 0x12 */
	RAWKEY_Null,		/* 0x13 */
	RAWKEY_space,		/* 0x14 */
	RAWKEY_Delete,		/* 0x15 */
	RAWKEY_Alt_L,		/* 0x16: Henkan */
	RAWKEY_Alt_R,		/* 0x17: Kakutei */
	RAWKEY_f11,		/* 0x18: Shokyo */
	RAWKEY_f12,		/* 0x19: Yobidashi */
	RAWKEY_Null,		/* 0x1a: Bunsetsu L / f13 */
	RAWKEY_Null,		/* 0x1b: Bunsetsu R / f14 */
	RAWKEY_KP_Up,		/* 0x1c */
	RAWKEY_KP_Left,		/* 0x1d */
	RAWKEY_KP_Right,	/* 0x1e */
	RAWKEY_KP_Down,		/* 0x1f */
	RAWKEY_f11,		/* 0x20 */
	RAWKEY_f12,		/* 0x21 */
	RAWKEY_1,		/* 0x22: exclam */
	RAWKEY_2,		/* 0x23: quotedbl */
	RAWKEY_3,		/* 0x24: numbersign */
	RAWKEY_4,		/* 0x25: dollar */
	RAWKEY_5,		/* 0x26: percent */
	RAWKEY_6,		/* 0x27: ampersand */
	RAWKEY_7,		/* 0x28: apostrophe */
	RAWKEY_8,		/* 0x29: parenleft */
	RAWKEY_9,		/* 0x2a: parenright  */
	RAWKEY_0,		/* 0x2b */
	RAWKEY_minus,		/* 0x2c: equal */
	RAWKEY_equal,		/* 0x2d: asciitilde */
	0x7d,			/* 0x2e: bar / No RAWKEY_XXX symbol */
	RAWKEY_Null, 		/* 0x2f */
	RAWKEY_Null, 		/* 0x30: f13 */
	RAWKEY_Null, 		/* 0x31: f14 */
	RAWKEY_q,		/* 0x32 */
	RAWKEY_w,		/* 0x33 */
	RAWKEY_e,		/* 0x34 */
	RAWKEY_r,		/* 0x35 */
	RAWKEY_t,		/* 0x36 */
	RAWKEY_y,		/* 0x37 */
	RAWKEY_u,		/* 0x38 */
	RAWKEY_i,		/* 0x39 */
	RAWKEY_o,		/* 0x3a */
	RAWKEY_p,		/* 0x3b */
	RAWKEY_bracketleft,	/* 0x3c: grave */
	RAWKEY_bracketright,	/* 0x3d: braceleft */
	RAWKEY_Null,		/* 0x3e */
	RAWKEY_Null,		/* 0x3f */
	RAWKEY_Null,		/* 0x40 */
	RAWKEY_Null,		/* 0x41 */
	RAWKEY_a,		/* 0x42 */
	RAWKEY_s,		/* 0x43 */
	RAWKEY_d,		/* 0x44 */
	RAWKEY_f,		/* 0x45 */
	RAWKEY_g,		/* 0x46 */
	RAWKEY_h,		/* 0x47 */
	RAWKEY_j,		/* 0x48 */
	RAWKEY_k,		/* 0x49 */
	RAWKEY_l,		/* 0x4a */
	RAWKEY_semicolon,	/* 0x4b: plus */
	RAWKEY_apostrophe,	/* 0x4c: asterisk */
	RAWKEY_backslash,	/* 0x4d: braceright */
	RAWKEY_Null,		/* 0x4e */
	RAWKEY_Null,		/* 0x4f */
	RAWKEY_Null,		/* 0x50 */
	RAWKEY_Null,		/* 0x51 */
	RAWKEY_z,		/* 0x52 */
	RAWKEY_x,		/* 0x53 */
	RAWKEY_c,		/* 0x54 */
	RAWKEY_v,		/* 0x55 */
	RAWKEY_b,		/* 0x56 */
	RAWKEY_n,		/* 0x57 */
	RAWKEY_m,		/* 0x58 */
	RAWKEY_comma,		/* 0x59: less */
	RAWKEY_period,		/* 0x5a: greater */
	RAWKEY_slash,		/* 0x5b: question */
	RAWKEY_Meta_L,		/* 0x5c: underscore */
	RAWKEY_Null,		/* 0x5d */
	RAWKEY_Null,		/* 0x5e */
	RAWKEY_Null,		/* 0x5f */
	RAWKEY_KP_Delete,	/* 0x60 */
	RAWKEY_KP_Add,		/* 0x61 */
	RAWKEY_KP_Subtract,	/* 0x62 */
	RAWKEY_KP_Home,		/* 0x63: KP 7 */
	RAWKEY_KP_Up,		/* 0x64: KP 8 */
	RAWKEY_KP_Prior,	/* 0x65: KP 9 */
	RAWKEY_KP_Left,		/* 0x66: KP 4 */
	RAWKEY_KP_Begin,	/* 0x67: KP 5 */
	RAWKEY_KP_Right,	/* 0x68: KP 6 */
	RAWKEY_KP_End,		/* 0x69: KP 1 */
	RAWKEY_KP_Down,		/* 0x6a: KP 2 */
	RAWKEY_KP_Next,		/* 0x6b: KP 3 */
	RAWKEY_KP_Insert,	/* 0x6c: KP 0 */
	RAWKEY_KP_Delete,	/* 0x6d: KP Decimal */
	RAWKEY_KP_Enter,	/* 0x6e */
	RAWKEY_Null,		/* 0x6f */
	RAWKEY_Null,		/* 0x70 */
	RAWKEY_Null,		/* 0x71 */
	RAWKEY_f1,		/* 0x72 */
	RAWKEY_f2,		/* 0x73 */
	RAWKEY_f3,		/* 0x74 */
	RAWKEY_f4,		/* 0x75 */
	RAWKEY_f5,		/* 0x76 */
	RAWKEY_f6,		/* 0x77 */
	RAWKEY_f7,		/* 0x78 */
	RAWKEY_f8,		/* 0x79 */
	RAWKEY_f9,		/* 0x7a */
	RAWKEY_f10,		/* 0x7b */
	RAWKEY_KP_Multiply,	/* 0x7c */
	RAWKEY_KP_Divide,	/* 0x7d */
	RAWKEY_KP_Equal,	/* 0x7E */
	RAWKEY_Null,		/* 0x7f: KP Separator */
};
#endif

#define KC(n) KS_KEYCODE(n)

static const keysym_t omkbd_keydesc_jp[] = {
/*	pos		command		normal		shifted */
	KC(0x09),			KS_Tab,		KS_Backtab,
	KC(0x0a),   KS_Cmd1,		KS_Control_L,
	KC(0x0b),			KS_Mode_switch,	/* Kana */
	KC(0x0c),			KS_Shift_R,
	KC(0x0d),			KS_Shift_L,
	KC(0x0e),			KS_Caps_Lock,
	KC(0x0f),   KS_Cmd2,		KS_Meta_L,	/* Zenmen */
	KC(0x10),   KS_Cmd_Debugger,	KS_Escape,
	KC(0x11),			KS_BackSpace,
	KC(0x12),			KS_Return,
	KC(0x14),			KS_space,
	KC(0x15),			KS_Delete,
	KC(0x16),			KS_Alt_L,	/* Henkan */
	KC(0x17),			KS_Alt_R,	/* Kakutei */
	KC(0x18),			KS_f11,		/* Shokyo */
	KC(0x19),			KS_f12,		/* Yobidashi */
	KC(0x1a),			KS_f13,		/* Bunsetsu L */
	KC(0x1b),			KS_f14,		/* Bunsetsu R */
	KC(0x1c),			KS_KP_Up,
	KC(0x1d),			KS_KP_Left,
	KC(0x1e),			KS_KP_Right,
	KC(0x1f),			KS_KP_Down,
	/* 0x20, 			KS_f11, */
	/* 0x21,			KS_f12, */
	KC(0x22),			KS_1,		KS_exclam,
	KC(0x23),			KS_2,		KS_quotedbl,
	KC(0x24),			KS_3,		KS_numbersign,
	KC(0x25),			KS_4,		KS_dollar,
	KC(0x26),			KS_5,		KS_percent,
	KC(0x27),			KS_6,		KS_ampersand,
	KC(0x28),			KS_7,		KS_apostrophe,
	KC(0x29),			KS_8,		KS_parenleft,
	KC(0x2a),			KS_9,		KS_parenright,
	KC(0x2b),			KS_0,
	KC(0x2c),			KS_minus,	KS_equal,
	KC(0x2d),			KS_asciicircum,	KS_asciitilde,
	KC(0x2e),			KS_backslash,	KS_bar,

	/* 0x30,			KS_f13, */
	/* 0x31,			KS_f14, */
	KC(0x32),			KS_q,
	KC(0x33),			KS_w,
	KC(0x34),			KS_e,
	KC(0x35),			KS_r,
	KC(0x36),			KS_t,
	KC(0x37),			KS_y,
	KC(0x38),			KS_u,
	KC(0x39),			KS_i,
	KC(0x3a),			KS_o,
	KC(0x3b),			KS_p,
	KC(0x3c),			KS_at,		KS_grave,
	KC(0x3d),			KS_bracketleft,	KS_braceleft,

	KC(0x42),			KS_a,
	KC(0x43),			KS_s,
	KC(0x44),			KS_d,
	KC(0x45),			KS_f,
	KC(0x46),			KS_g,
	KC(0x47),			KS_h,
	KC(0x48),			KS_j,
	KC(0x49),			KS_k,
	KC(0x4a),			KS_l,
	KC(0x4b),			KS_semicolon,	KS_plus,
	KC(0x4c),			KS_colon,	KS_asterisk,
	KC(0x4d),			KS_bracketright, KS_braceright,

	KC(0x52),			KS_z,
	KC(0x53),			KS_x,
	KC(0x54),			KS_c,
	KC(0x55),			KS_v,
	KC(0x56),			KS_b,
	KC(0x57),			KS_n,
	KC(0x58),			KS_m,
	KC(0x59),			KS_comma,	KS_less,
	KC(0x5a),			KS_period,	KS_greater,
	KC(0x5b),			KS_slash,	KS_question,
	KC(0x5c),			KS_underscore,

	KC(0x60),			KS_KP_Delete,
	KC(0x61),			KS_KP_Add,
	KC(0x62),			KS_KP_Subtract,
	KC(0x63),			KS_KP_7,
	KC(0x64),			KS_KP_8,
	KC(0x65),			KS_KP_9,
	KC(0x66),			KS_KP_4,
	KC(0x67),			KS_KP_5,
	KC(0x68),			KS_KP_6,
	KC(0x69),			KS_KP_1,
	KC(0x6a),			KS_KP_2,
	KC(0x6b),			KS_KP_3,
	KC(0x6c),			KS_KP_0,
	KC(0x6d),			KS_KP_Decimal,
	KC(0x6e),			KS_KP_Enter,

	KC(0x72),			KS_f1,
	KC(0x73),			KS_f2,
	KC(0x74),			KS_f3,
	KC(0x75),			KS_f4,
	KC(0x76),			KS_f5,
	KC(0x77),			KS_f6,
	KC(0x78),			KS_f7,
	KC(0x79),			KS_f8,
	KC(0x7a),			KS_f9,
	KC(0x7b),			KS_f10,
	KC(0x7c),			KS_KP_Multiply,
	KC(0x7d),			KS_KP_Divide,
	KC(0x7e),			KS_KP_Equal,
	KC(0x7f),			KS_KP_Separator,
};

#define	SIZE(map) (sizeof(map)/sizeof(keysym_t))

const struct wscons_keydesc omkbd_keydesctab[] = {
	{ KB_JP, 0, SIZE(omkbd_keydesc_jp), omkbd_keydesc_jp, },
	{ 0, 0, 0, 0 },
};
