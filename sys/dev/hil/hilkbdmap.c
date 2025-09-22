/*	$OpenBSD: hilkbdmap.c,v 1.9 2023/01/23 09:36:39 nicm Exp $	*/
/*
 * Copyright (c) 2003, Miodrag Vallat.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/types.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>
#include <dev/hil/hilkbdmap.h>

#ifdef WSDISPLAY_COMPAT_RAWKBD
#include <dev/wscons/wskbdraw.h>

/*
 * Translate HIL keycodes to US keyboard XT scancodes, for proper
 * X11-over-wsmux operation.
 */
const u_int8_t hilkbd_raw[0x80] = {
	RAWKEY_Control_R,
	RAWKEY_Null,
	RAWKEY_Alt_R,
	RAWKEY_Alt_L,
	RAWKEY_Shift_R,
	RAWKEY_Shift_L,
	RAWKEY_Control_L,
	RAWKEY_Pause,		/* 7 Break/Reset */
	RAWKEY_KP_Left,
	RAWKEY_KP_Up,
	RAWKEY_KP_Begin,
	RAWKEY_KP_Prior,
	RAWKEY_KP_Right,
	RAWKEY_KP_Home,
	RAWKEY_KP_Delete,
	RAWKEY_KP_Enter,
	RAWKEY_KP_End,
	RAWKEY_KP_Divide,
	RAWKEY_KP_Down,
	RAWKEY_KP_Add,
	RAWKEY_KP_Next,
	RAWKEY_KP_Multiply,
	RAWKEY_KP_Insert,
	RAWKEY_KP_Subtract,
	RAWKEY_b,
	RAWKEY_v,
	RAWKEY_c,
	RAWKEY_x,
	RAWKEY_z,
	RAWKEY_Null,		/* 29 Kanji Left */
	RAWKEY_Null,		/* 30 */
	RAWKEY_Escape,
	RAWKEY_Null,
	RAWKEY_f10,
	RAWKEY_Null,
	RAWKEY_f11,
	RAWKEY_KP_Delete,
	RAWKEY_f9,
	RAWKEY_Tab,
	RAWKEY_f12,
	RAWKEY_h,
	RAWKEY_g,
	RAWKEY_f,
	RAWKEY_d,
	RAWKEY_s,
	RAWKEY_a,
	RAWKEY_Null,		/* 46 */
	RAWKEY_Caps_Lock,
	RAWKEY_u,
	RAWKEY_y,
	RAWKEY_t,
	RAWKEY_r,
	RAWKEY_e,
	RAWKEY_w,
	RAWKEY_q,
	RAWKEY_Tab,
	RAWKEY_7,
	RAWKEY_6,
	RAWKEY_5,
	RAWKEY_4,
	RAWKEY_3,
	RAWKEY_2,
	RAWKEY_1,
	RAWKEY_grave,
	RAWKEY_Null,		/* 64 */
	RAWKEY_Null,		/* 65 */
	RAWKEY_Null,		/* 66 */
	RAWKEY_Null,		/* 67 */
	RAWKEY_Null,		/* 68 */
	RAWKEY_Null,		/* 69 */
	RAWKEY_Null,		/* 70 */
	RAWKEY_Null,		/* 71 */
	RAWKEY_Print_Screen,
	RAWKEY_f4,
	RAWKEY_f3,
	RAWKEY_f2,
	RAWKEY_f1,
	RAWKEY_Null,		/* 77 */
	RAWKEY_Hold_Screen,
	RAWKEY_Return,
	RAWKEY_Num_Lock,
	RAWKEY_f5,
	RAWKEY_f6,
	RAWKEY_f7,
	RAWKEY_f8,
	RAWKEY_Null,		/* 85 */
	RAWKEY_Null,		/* 86 Clear line */
	RAWKEY_Null,		/* 87 Clear display */
	RAWKEY_8,
	RAWKEY_9,
	RAWKEY_0,
	RAWKEY_minus,
	RAWKEY_equal,
	RAWKEY_BackSpace,
	RAWKEY_Null,		/* 94 Insert line */
	RAWKEY_Null,		/* 95 Delete line */
	RAWKEY_i,
	RAWKEY_o,
	RAWKEY_p,
	RAWKEY_bracketleft,
	RAWKEY_bracketright,
	RAWKEY_backslash,
	RAWKEY_Insert,
	RAWKEY_Delete,
	RAWKEY_j,
	RAWKEY_k,
	RAWKEY_l,
	RAWKEY_semicolon,
	RAWKEY_apostrophe,
	RAWKEY_Return,
	RAWKEY_Home,
	RAWKEY_Prior,
	RAWKEY_m,
	RAWKEY_comma,
	RAWKEY_period,
	RAWKEY_slash,
	RAWKEY_Null,		/* 116 */
	RAWKEY_End,
	RAWKEY_Null,		/* 118 */
	RAWKEY_Next,
	RAWKEY_n,
	RAWKEY_space,
	RAWKEY_Null,		/* 122 */
	RAWKEY_Null,		/* 123 Kanji Right */
	RAWKEY_Left,
	RAWKEY_Down,
	RAWKEY_Up,
	RAWKEY_Right
};
#endif

#define KC(n) KS_KEYCODE(n)

/*
 * 1f. US ASCII
 *
 * We use the same table for PS/2 and old HIL keyboards, as the only
 * differences are a few keys which are only present in one of both layouts,
 * and the one-function-only keypad in the old HIL flavour (hilkbd.c knows
 * about this and does The Right Thing).
 */

static const keysym_t hilkbd_keydesc_us[] = {
/*  pos      command		normal		shifted */
    KC(0),   KS_Cmd1,		KS_Control_R,
    KC(2),   KS_Cmd2,		KS_Mode_switch,	KS_Multi_key,
    KC(3),   KS_Cmd2,		KS_Alt_L,
    KC(4),			KS_Shift_R,
    KC(5),			KS_Shift_L,
    KC(6),   KS_Cmd1,		KS_Control_L,
    KC(7),   KS_Cmd_KbdReset,			/* Break/Reset */
    KC(8),			KS_KP_Left,	KS_KP_4,
    KC(9),			KS_KP_Up,	KS_KP_8,
    KC(10),			KS_KP_Begin,	KS_KP_5,
    KC(11),			KS_KP_Prior,	KS_KP_9,
    KC(12),			KS_KP_Right,	KS_KP_6,
    KC(13),			KS_KP_Home,	KS_KP_7,
    KC(14),			KS_KP_Separator,
    KC(15),			KS_KP_Enter,
    KC(16),			KS_KP_End,	KS_KP_1,
    KC(17),			KS_KP_Divide,
    KC(18),			KS_KP_Down,	KS_KP_2,
    KC(19),			KS_KP_Add,
    KC(20),			KS_KP_Next,	KS_KP_3,
    KC(21),			KS_KP_Multiply,
    KC(22),			KS_KP_Insert,	KS_KP_0,
    KC(23),			KS_KP_Subtract,
    KC(24),			KS_b,
    KC(25),			KS_v,		KS_V,		KS_section,
    KC(26),			KS_c,		KS_C,		KS_ccedilla,
    KC(27),			KS_x,		KS_X,		KS_L2_scaron,
    KC(28),			KS_z,		KS_Z,		KS_paragraph,
    /* 29 Kanji Left */

    KC(31), KS_Cmd_Debugger,	KS_Escape,	KS_Delete,
    KC(33), KS_Cmd_Screen9,	KS_f10,		/* also KS_KP_F2 */
    KC(35), KS_Cmd_Screen10,	KS_f11,		/* also KS_KP_F3 */
    KC(36),			KS_KP_Delete,	KS_KP_Decimal,
    KC(37), KS_Cmd_Screen8,	KS_f9,		/* also KS_KP_F1 */
    KC(38),			KS_Tab,		KS_Backtab,	/* numeric pad */
    KC(39), KS_Cmd_Screen11,	KS_f12,		/* also KS_KP_F4 */
    KC(40),			KS_h,		KS_H,		KS_yen,
    KC(41),			KS_g,		KS_G,		KS_currency,
    KC(42),			KS_f,
    KC(43),			KS_d,		KS_D,		KS_eth,
    KC(44),			KS_s,		KS_S,		KS_ssharp,
    KC(45),			KS_a,		KS_A,		KS_aring,
    /* 46 Mode_Switch ??? */
    KC(47),			KS_Caps_Lock,
    KC(48),			KS_u,		KS_U,		KS_dead_diaeresis,
    KC(49),			KS_y,		KS_Y,		KS_dead_circumflex,
    KC(50),			KS_t,		KS_T,		KS_dead_grave,
    KC(51),			KS_r,		KS_R,		KS_dead_acute,
    KC(52),			KS_e,		KS_E,		KS_ae,
    KC(53),			KS_w,		KS_W,		KS_asciitilde,
    KC(54),			KS_q,		KS_Q,		KS_periodcentered,
    KC(55),			KS_Tab,		KS_Backtab,
    KC(56),			KS_7,		KS_ampersand,	KS_backslash,
    KC(57),			KS_6,		KS_asciicircum,	KS_asciicircum,
    KC(58),			KS_5,		KS_percent,	KS_onehalf,
    KC(59),			KS_4,		KS_dollar,
				KS_onequarter,	KS_threequarters,
    KC(60),			KS_3,		KS_numbersign,	KS_numbersign,
    KC(61),			KS_2,		KS_at,		KS_at,
    KC(62),			KS_1,		KS_exclam,	KS_exclamdown,
    KC(63),			KS_grave,	KS_asciitilde,
				KS_guillemotleft,KS_guillemotright,

    KC(72),			KS_Print_Screen, /* Menu */
    KC(73),  KS_Cmd_Screen3,	KS_f4,
    KC(74),  KS_Cmd_Screen2,	KS_f3,
    KC(75),  KS_Cmd_Screen1,	KS_f2,
    KC(76),  KS_Cmd_Screen0,	KS_f1,

    KC(78),			KS_Hold_Screen,
    KC(79),			KS_Return,	KS_Print_Screen,
    KC(80),			KS_Num_Lock,	/* System/User */
    KC(81),  KS_Cmd_Screen4,	KS_f5,
    KC(82),  KS_Cmd_Screen5,	KS_f6,
    KC(83),  KS_Cmd_Screen6,	KS_f7,
    KC(84),  KS_Cmd_Screen7,	KS_f8,

    /* 86 Clear line */
    KC(87),			KS_Clear,
    KC(88),			KS_8,		KS_asterisk,
				KS_bracketleft,	KS_braceleft,
    KC(89),			KS_9,		KS_parenleft,
				KS_bracketright,KS_braceright,
    KC(90),			KS_0,		KS_parenright,	KS_questiondown,
    KC(91),			KS_minus,	KS_underscore,	KS_macron,
    KC(92),			KS_equal,	KS_plus,	KS_plusminus,
    KC(93),  KS_Cmd_ResetEmul,	KS_Delete,	/* Backspace */
    /* 94 Insert line */
    /* 95 Delete line */
    KC(96),			KS_i,		KS_I,		KS_dead_tilde,
    KC(97),			KS_o,		KS_O,		KS_oslash,
    KC(98),			KS_p,		KS_P,		KS_thorn,
    KC(99),			KS_bracketleft,	KS_braceleft,	KS_degree,
    KC(100),			KS_bracketright,KS_braceright,
				KS_bar,		KS_brokenbar,
    KC(101),			KS_backslash,	KS_bar,		KS_mu,
    KC(102),			KS_Insert,
    KC(103),			KS_Delete,
    KC(104),			KS_j,		KS_J,		KS_dollar,
    KC(105),			KS_k,		KS_K,		KS_cent,
    KC(106),			KS_l,		KS_L,		KS_sterling,
    KC(107),			KS_semicolon,	KS_colon,
    KC(108),			KS_apostrophe,	KS_quotedbl,
				KS_grave,	KS_apostrophe,
    KC(109),			KS_Return,
    KC(110),			KS_Home,
    KC(111), KS_Cmd_ScrollBack,	KS_Prior,
    KC(112),			KS_m,		KS_M,		KS_masculine,
    KC(113),			KS_comma,	KS_less,	KS_less,
    KC(114),			KS_period,	KS_greater,	KS_greater,
    KC(115),			KS_slash,	KS_question,	KS_underscore,

    KC(117),			KS_End,		/* Select */

    KC(119), KS_Cmd_ScrollFwd,	KS_Next,
    KC(120),			KS_n,		KS_N,		KS_ordfeminine,
    KC(121),			KS_space,

    /* 123 Kanji Right */
    KC(124),			KS_Left,
    KC(125),			KS_Down,
    KC(126),			KS_Up,
    KC(127),			KS_Right,
};

/*
 * 0e. Swedish
 */

static const keysym_t hilkbd_keydesc_sv[] = {
    KC(56),	KS_7,		KS_slash,	KS_backslash,
    KC(57),	KS_6,		KS_ampersand,	KS_asciicircum,
    KC(61),	KS_2,		KS_quotedbl,	KS_at,
    KC(63),	KS_less,	KS_greater,
		KS_guillemotleft,KS_guillemotright,
    KC(88),	KS_8,		KS_parenleft,	KS_bracketleft,	KS_braceleft,
    KC(89),	KS_9,		KS_parenright,	KS_bracketright,KS_braceright,
    KC(90),	KS_0,		KS_equal,	KS_exclamdown,
    KC(91),	KS_plus,	KS_question,	KS_macron,
    KC(92),	KS_eacute,	KS_Eacute,	KS_plusminus,
    KC(99),	KS_aring,	KS_Aring,	KS_degree,
    KC(100),	KS_udiaeresis,	KS_Udiaeresis,	KS_bar,		KS_brokenbar,
    KC(101),	KS_apostrophe,	KS_asterisk,	KS_mu,
    KC(107),	KS_odiaeresis,
    KC(108),	KS_adiaeresis,	KS_Adiaeresis,	KS_grave,	KS_apostrophe,
    KC(113),	KS_comma,	KS_semicolon,	KS_less,
    KC(114),	KS_period,	KS_colon,	KS_greater,
    KC(115),	KS_minus,	KS_underscore,	KS_underscore
};

static const keysym_t hilkbd_keydesc_sv_nodead[] = {
    KC(48),	KS_u,		KS_U,		KS_diaeresis,
    KC(49),	KS_y,		KS_Y,		KS_asciicircum,
    KC(50),	KS_t,		KS_T,		KS_grave,
    KC(51),	KS_r,		KS_R,		KS_apostrophe,
    KC(96),	KS_i,		KS_I,		KS_asciitilde,
};

static const keysym_t hilkbd_keydesc_sv_ps2[] = {
    KC(24),	KS_b,
    KC(25),	KS_v,
    KC(26),	KS_c,
    KC(27),	KS_x,
    KC(28),	KS_z,
    KC(36),	KS_KP_Separator,KS_KP_Delete,
    KC(40),	KS_h,
    KC(41),	KS_g,
    KC(42),	KS_f,
    KC(43),	KS_d,
    KC(44),	KS_s,
    KC(45),	KS_a,
    KC(48),	KS_u,
    KC(49),	KS_y,
    KC(50),	KS_t,
    KC(51),	KS_r,
    KC(52),	KS_e,
    KC(53),	KS_w,
    KC(54),	KS_q,
    KC(56),	KS_7,		KS_slash,	KS_braceleft,
    KC(57),	KS_6,		KS_ampersand,
    KC(58),	KS_5,		KS_percent,
    KC(59),	KS_4,		KS_currency,	KS_dollar,
    KC(60),	KS_3,		KS_numbersign,	KS_sterling,
    KC(61),	KS_2,		KS_quotedbl,	KS_at,
    KC(62),	KS_1,		KS_exclam,
    KC(63),	KS_section,	KS_onehalf,
    KC(88),	KS_8,		KS_parenleft,	KS_bracketleft,
    KC(89),	KS_9,		KS_parenright,	KS_bracketright,
    KC(90),	KS_0,		KS_equal,	KS_braceright,
    KC(91),	KS_minus,	KS_question,	KS_backslash,
    KC(92),	KS_dead_acute,	KS_dead_grave,
    KC(96),	KS_i,
    KC(97),	KS_o,
    KC(98),	KS_p,
    KC(99),	KS_aring,
    KC(100),	KS_dead_diaeresis,KS_dead_circumflex,KS_dead_tilde,
    KC(104),	KS_j,
    KC(105),	KS_k,
    KC(106),	KS_l,
    KC(107),	KS_odiaeresis,
    KC(108),	KS_adiaeresis,
    KC(112),	KS_m,
    KC(113),	KS_comma,	KS_semicolon,
    KC(114),	KS_period,	KS_colon,
    KC(115),	KS_minus,	KS_underscore,
    KC(116),	KS_apostrophe,	KS_asterisk,
    KC(118),	KS_less,	KS_greater,	KS_bar,
    KC(120),	KS_n
};

static const keysym_t hilkbd_keydesc_sv_ps2_nodead[] = {
    KC(92),	KS_apostrophe,	KS_grave,
    KC(100),	KS_diaeresis,	KS_asciicircum,	KS_asciitilde,
};

/*
 * 0f. German
 */

static const keysym_t hilkbd_keydesc_de[] = {
    KC(28),	KS_y,		KS_Y,		KS_paragraph,
    KC(49),	KS_z,		KS_Z,		KS_dead_circumflex,
    KC(56),	KS_7,		KS_slash,	KS_backslash,
    KC(57),	KS_6,		KS_ampersand,	KS_asciicircum,
    KC(60),	KS_3,		KS_section,	KS_numbersign,
    KC(61),	KS_2,		KS_quotedbl,	KS_at,
    KC(63),	KS_less,	KS_greater,
		KS_guillemotleft,KS_guillemotright,
    KC(88),	KS_8,		KS_parenleft,
		KS_bracketleft,	KS_braceleft,
    KC(89),	KS_9,		KS_parenright,
		KS_bracketright,KS_braceright,
    KC(90),	KS_0,		KS_equal,	KS_exclamdown,
    KC(91),	KS_ssharp,	KS_question,	KS_macron,
    KC(92),	KS_dead_acute,	KS_dead_grave,	KS_plusminus,
    KC(99),	KS_udiaeresis,	KS_Udiaeresis,	KS_dead_abovering,
    KC(100),	KS_plus,	KS_asterisk,	KS_bar,		KS_brokenbar,
    KC(101),	KS_sterling,	KS_dead_circumflex,KS_mu,
    KC(107),	KS_odiaeresis,
    KC(108),	KS_adiaeresis,	KS_Adiaeresis,	KS_grave,	KS_apostrophe,
    KC(113),	KS_comma,	KS_semicolon,	KS_less,
    KC(114),	KS_period,	KS_colon,	KS_greater,
    KC(115),	KS_minus,	KS_underscore,	KS_underscore
};

static const keysym_t hilkbd_keydesc_de_nodead[] = {
    KC(48),	KS_u,		KS_U,		KS_diaeresis,
    KC(49),	KS_z,		KS_Z,		KS_asciicircum,
    KC(50),	KS_t,		KS_T,		KS_grave,
    KC(51),	KS_r,		KS_R,		KS_apostrophe,
    KC(92),	KS_apostrophe,	KS_grave,	KS_plusminus,
    KC(99),	KS_udiaeresis,	KS_Udiaeresis,	KS_degree,
    KC(101),	KS_sterling,	KS_asciicircum,	KS_mu,
    KC(96),	KS_i,		KS_I,		KS_asciitilde
};

static const keysym_t hilkbd_keydesc_de_ps2[] = {
    KC(24),	KS_b,
    KC(25),	KS_v,
    KC(26),	KS_c,
    KC(27),	KS_x,
    KC(28),	KS_y,
    KC(36),	KS_KP_Separator,KS_KP_Delete,
    KC(40),	KS_h,
    KC(41),	KS_g,
    KC(42),	KS_f,
    KC(43),	KS_d,
    KC(44),	KS_s,
    KC(45),	KS_a,
    KC(48),	KS_u,
    KC(49),	KS_z,		KS_Z,		KS_dead_circumflex,
    KC(50),	KS_t,
    KC(51),	KS_r,
    KC(52),	KS_e,
    KC(53),	KS_w,
    KC(54),	KS_q,		KS_Q,		KS_at,
    KC(56),	KS_7,		KS_slash,	KS_braceleft,
    KC(57),	KS_6,		KS_ampersand,
    KC(58),	KS_5,		KS_percent,
    KC(59),	KS_4,		KS_dollar,
    KC(60),	KS_3,		KS_section,	KS_threesuperior,
    KC(61),	KS_2,		KS_quotedbl,	KS_twosuperior,
    KC(62),	KS_1,		KS_exclam,
    KC(63),	KS_dead_circumflex,	KS_dead_abovering,
    KC(88),	KS_8,		KS_parenleft,	KS_bracketleft,
    KC(89),	KS_9,		KS_parenright,	KS_bracketright,
    KC(90),	KS_0,		KS_equal,	KS_braceright,
    KC(91),	KS_ssharp,	KS_question,	KS_backslash,
    KC(92),	KS_dead_acute,	KS_dead_grave,
    KC(96),	KS_i,
    KC(97),	KS_o,
    KC(98),	KS_p,
    KC(99),	KS_udiaeresis,
    KC(100),	KS_plus,	KS_asterisk,	KS_dead_tilde,
    KC(104),	KS_j,
    KC(105),	KS_k,
    KC(106),	KS_l,
    KC(107),	KS_odiaeresis,
    KC(108),	KS_adiaeresis,
    KC(112),	KS_m,		KS_M,		KS_mu,
    KC(113),	KS_comma,	KS_semicolon,
    KC(114),	KS_period,	KS_colon,
    KC(115),	KS_minus,	KS_underscore,
    KC(116),	KS_numbersign,	KS_apostrophe,
    KC(118),	KS_less,	KS_greater, 	KS_bar,
    KC(120),	KS_n
};

static const keysym_t hilkbd_keydesc_de_ps2_nodead[] = {
    KC(49),	KS_z,		KS_Z,		KS_asciicircum,
    KC(63),	KS_asciicircum,	KS_degree,
    KC(92),	KS_apostrophe,	KS_grave,
    KC(100),	KS_plus,	KS_asterisk,	KS_asciitilde
};

/*
 * 17. English
 */

static const keysym_t hilkbd_keydesc_uk[] = {
    KC(56),	KS_7,		KS_asciicircum,	KS_backslash,
    KC(57),	KS_6,		KS_ampersand,	KS_asciicircum,
    KC(60),	KS_3,		KS_sterling,	KS_numbersign,
    KC(61),	KS_2,		KS_quotedbl,	KS_at,
    KC(88),	KS_8,		KS_parenleft,	KS_bracketleft,	KS_braceleft,
    KC(89),	KS_9,		KS_parenright,	KS_bracketright,KS_braceright,
    KC(90),	KS_0,		KS_equal,	KS_questiondown,
    KC(91),	KS_plus,	KS_question,	KS_macron,
    KC(92),	KS_apostrophe,	KS_slash,	KS_plusminus,
    KC(101),	KS_less,	KS_greater,	KS_mu,
    KC(107),	KS_asterisk,	KS_at,
    KC(108),	KS_backslash,	KS_bar,		KS_grave,	KS_apostrophe,
    KC(113),	KS_comma,	KS_semicolon,	KS_less,
    KC(114),	KS_period,	KS_colon,	KS_greater,
    KC(115),	KS_minus,	KS_underscore,	KS_underscore
};

static const keysym_t hilkbd_keydesc_uk_ps2[] = {
    KC(24),	KS_b,
    KC(25),	KS_v,
    KC(26),	KS_c,
    KC(27),	KS_x,
    KC(28),	KS_z,
    KC(40),	KS_h,
    KC(41),	KS_g,
    KC(42),	KS_f,
    KC(43),	KS_d,
    KC(44),	KS_s,
    KC(45),	KS_a,
    KC(48),	KS_u,
    KC(49),	KS_y,
    KC(50),	KS_t,
    KC(51),	KS_r,
    KC(52),	KS_e,
    KC(53),	KS_w,
    KC(54),	KS_q,
    KC(56),	KS_7,		KS_ampersand,
    KC(57),	KS_6,		KS_asciicircum,
    KC(58),	KS_5,		KS_percent,
    KC(59),	KS_4,		KS_dollar,
    KC(60),	KS_3,		KS_sterling,
    KC(61),	KS_2,		KS_quotedbl,
    KC(62),	KS_1,		KS_exclam,
    KC(63),	KS_grave,	KS_notsign,	KS_bar,
    KC(88),	KS_8,		KS_asterisk,
    KC(89),	KS_9,		KS_parenleft,
    KC(90),	KS_0,		KS_parenright,
    KC(91),	KS_minus,	KS_underscore,
    KC(92),	KS_equal,	KS_plus,
    KC(96),	KS_i,
    KC(97),	KS_o,
    KC(98),	KS_p,
    KC(99),	KS_bracketleft,	KS_braceleft,
    KC(100),	KS_bracketright,KS_braceright,
    KC(104),	KS_j,
    KC(105),	KS_k,
    KC(106),	KS_l,
    KC(107),	KS_semicolon,	KS_colon,
    KC(108),	KS_apostrophe,	KS_at,
    KC(112),	KS_m,
    KC(113),	KS_comma,	KS_less,
    KC(114),	KS_period,	KS_greater,
    KC(115),	KS_slash,	KS_question,
    KC(116),	KS_numbersign,	KS_asciitilde,
    KC(118),	KS_backslash,	KS_brokenbar,
    KC(120),	KS_n
};

/*
 * 1b. French
 */

static const keysym_t hilkbd_keydesc_fr[] = {
    KC(28),	KS_w,		KS_W,		KS_paragraph,
    KC(45),	KS_q,		KS_Q,		KS_aring,
    KC(53),	KS_z,		KS_Z,		KS_asciitilde,
    KC(54),	KS_a,		KS_A,		KS_periodcentered,
    KC(56),	KS_egrave,	KS_7,		KS_backslash,
    KC(57),	KS_section,	KS_6,		KS_asciicircum,
    KC(58),	KS_parenleft,	KS_5,		KS_onehalf,
    KC(59),	KS_apostrophe,	KS_4,		KS_onequarter,	KS_threequarters,
    KC(60),	KS_quotedbl,	KS_3,		KS_numbersign,
    KC(61),	KS_eacute,	KS_2,		KS_at,
    KC(62),	KS_ampersand,	KS_1,		KS_exclamdown,
    KC(63),	KS_dollar,	KS_sterling,
		KS_guillemotleft,KS_guillemotright,
    KC(88),	KS_exclam,	KS_8,		KS_bracketleft,	KS_braceleft,
    KC(89),	KS_ccedilla,	KS_9,		KS_bracketright,KS_braceright,
    KC(90),	KS_agrave,	KS_0,		KS_questiondown,
    KC(91),	KS_parenright,	KS_degree,	KS_macron,
    KC(92),	KS_minus,	KS_underscore,	KS_plusminus,
    KC(99),	KS_dead_circumflex, KS_dead_diaeresis,	KS_dead_abovering,
    KC(100),	KS_grave,	KS_asterisk,	KS_bar,		KS_brokenbar,
    KC(101),	KS_less,	KS_greater,	KS_mu,
    KC(107),	KS_m,
    KC(108),	KS_ugrave,	KS_percent,	KS_grave,	KS_apostrophe,
    KC(112),	KS_comma,	KS_question,	KS_masculine,
    KC(113),	KS_semicolon,	KS_period,	KS_less,
    KC(114),	KS_colon,	KS_slash,	KS_greater,
    KC(115),	KS_equal,	KS_plus,	KS_underscore
};

static const keysym_t hilkbd_keydesc_fr_ps2[] = {
    KC(24),	KS_b,
    KC(25),	KS_v,
    KC(26),	KS_c,
    KC(27),	KS_x,
    KC(28),	KS_w,
    KC(40),	KS_h,
    KC(41),	KS_g,
    KC(42),	KS_f,
    KC(43),	KS_d,
    KC(44),	KS_s,
    KC(45),	KS_q,
    KC(48),	KS_u,
    KC(49),	KS_y,
    KC(50),	KS_t,
    KC(51),	KS_r,
    KC(52),	KS_e,
    KC(53),	KS_z,
    KC(54),	KS_a,
    KC(56),	KS_egrave,	KS_7,		KS_grave,
    KC(57),	KS_minus,	KS_6,		KS_bar,
    KC(58),	KS_parenleft,	KS_5,		KS_bracketleft,
    KC(59),	KS_apostrophe,	KS_4,		KS_braceleft,
    KC(60),	KS_quotedbl,	KS_3,		KS_numbersign,
    KC(61),	KS_eacute,	KS_2,		KS_asciitilde,
    KC(62),	KS_ampersand,	KS_1,
    KC(63),	KS_twosuperior,
    KC(88),	KS_underscore,	KS_8,		KS_backslash,
    KC(89),	KS_ccedilla,	KS_9,		KS_asciicircum,
    KC(90),	KS_agrave,	KS_0,		KS_at,
    KC(91),	KS_parenright,	KS_degree,	KS_bracketright,
    KC(92),	KS_equal,	KS_plus,	KS_braceright,
    KC(96),	KS_i,
    KC(97),	KS_o,
    KC(98),	KS_p,
    KC(99),	KS_dead_circumflex,KS_dead_diaeresis,
    KC(100),	KS_dollar,	KS_sterling,	KS_currency,
    KC(104),	KS_j,
    KC(105),	KS_k,
    KC(106),	KS_l,
    KC(107),	KS_m,
    KC(108),	KS_ugrave,	KS_percent,
    KC(112),	KS_comma,	KS_question,
    KC(113),	KS_semicolon,	KS_period,
    KC(114),	KS_colon,	KS_slash,
    KC(115),	KS_exclam,	KS_section,
    KC(116),	KS_asterisk,	KS_mu,
    KC(118),	KS_less,	KS_greater,
		KS_guillemotleft,KS_guillemotright,
    KC(120),	KS_n
};

#define KBD_MAP(name, base, map) \
			{ name, base, sizeof(map)/sizeof(keysym_t), map }

const struct wscons_keydesc hilkbd_keydesctab[] = {
	KBD_MAP(KB_US,			0,	hilkbd_keydesc_us),
	KBD_MAP(KB_DE,			KB_US,	hilkbd_keydesc_de),
	KBD_MAP(KB_DE | KB_NODEAD,	KB_DE,	hilkbd_keydesc_de_nodead),
	KBD_MAP(KB_FR,			KB_US,	hilkbd_keydesc_fr),
	KBD_MAP(KB_UK,			KB_US,	hilkbd_keydesc_uk),
	KBD_MAP(KB_SV,			KB_US,	hilkbd_keydesc_sv),
	KBD_MAP(KB_SV | KB_NODEAD,	KB_SV,	hilkbd_keydesc_sv_nodead),
	{0, 0, 0, 0},
};

const struct wscons_keydesc hilkbd_keydesctab_ps2[] = {
	KBD_MAP(KB_US,			0,	hilkbd_keydesc_us),
	KBD_MAP(KB_DE,			KB_US,	hilkbd_keydesc_de_ps2),
	KBD_MAP(KB_DE | KB_NODEAD,	KB_DE,	hilkbd_keydesc_de_ps2_nodead),
	KBD_MAP(KB_FR,			KB_US,	hilkbd_keydesc_fr_ps2),
	KBD_MAP(KB_UK,			KB_US,	hilkbd_keydesc_uk_ps2),
	KBD_MAP(KB_SV,			KB_US,	hilkbd_keydesc_sv_ps2),
	KBD_MAP(KB_SV | KB_NODEAD,	KB_SV,	hilkbd_keydesc_sv_ps2_nodead),
	{0, 0, 0, 0},
};

/*
 * Keyboard ID to layout table
 */
const kbd_t hilkbd_layouts[MAXHILKBDLAYOUT] = {
	-1,	/* 00 Undefined or custom layout */
	-1,	/* 01 Undefined */
	-1,	/* 02 Japanese */
	-1,	/* 03 Swiss french */
	-1,	/* 04 Portuguese */
	-1,	/* 05 Arabic */
	-1,	/* 06 Hebrew */
	-1,	/* 07 Canadian English */
	-1,	/* 08 Turkish */
	-1,	/* 09 Greek */
	-1,	/* 0a Thai */
	-1,	/* 0b Italian */
	-1,	/* 0c Korean */
	-1,	/* 0d Dutch */
	KB_SV,	/* 0e Swedish */
	KB_DE,	/* 0f German */
	-1,	/* 10 Simplified Chinese */
	-1,	/* 11 Traditional Chinese */
	-1,	/* 12 Swiss French 2 */
	-1,	/* 13 Euro Spanish */
	-1,	/* 14 Swiss German 2 */
	-1,	/* 15 Belgian */
	-1,	/* 16 Finnish */
	KB_UK,	/* 17 UK English */
	-1,	/* 18 Canadian French */
	-1,	/* 19 Swiss German */
	-1,	/* 1a Norwegian */
	KB_FR,	/* 1b French */
	-1,	/* 1c Danish */
	-1,	/* 1d Katakana */
	-1,	/* 1e Latin Spanish */
	KB_US,	/* 1f US ASCII */
};
