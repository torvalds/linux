/*	$OpenBSD: sunkbdmap.c,v 1.7 2023/01/23 09:36:40 nicm Exp $	*/

/*
 * Copyright (c) 2002, 2003 Miodrag Vallat.
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/timeout.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdvar.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>

#include <dev/sun/sunkbdreg.h>
#include <dev/sun/sunkbdvar.h>

#ifdef WSDISPLAY_COMPAT_RAWKBD
#include <dev/wscons/wskbdraw.h>

/*
 * Translate Sun keycodes to US keyboard XT scancodes, for proper
 * X11-over-wsmux operation.
 */
const u_int8_t sunkbd_rawmap[0x80] = {
	RAWKEY_Null,
	RAWKEY_L1,
	RAWKEY_AudioLower,
	RAWKEY_L2,
	RAWKEY_AudioRaise,
	RAWKEY_f1,
	RAWKEY_f2,
	RAWKEY_f10,
	RAWKEY_f3,
	RAWKEY_f11,
	RAWKEY_f4,
	RAWKEY_f12,
	RAWKEY_f5,
	RAWKEY_Alt_R,
	RAWKEY_f6,
	RAWKEY_Null,
	RAWKEY_f7,
	RAWKEY_f8,
	RAWKEY_f9,
	RAWKEY_Alt_L,
	RAWKEY_Up,
	RAWKEY_Pause,
	RAWKEY_Print_Screen,
	RAWKEY_Hold_Screen,
	RAWKEY_Left,
	RAWKEY_L3,
	RAWKEY_L4,
	RAWKEY_Down,
	RAWKEY_Right,
	RAWKEY_Escape,
	RAWKEY_1,
	RAWKEY_2,
	RAWKEY_3,
	RAWKEY_4,
	RAWKEY_5,
	RAWKEY_6,
	RAWKEY_7,
	RAWKEY_8,
	RAWKEY_9,
	RAWKEY_0,
	RAWKEY_minus,
	RAWKEY_equal,
	RAWKEY_grave,
	RAWKEY_BackSpace,
	RAWKEY_Insert,
	RAWKEY_KP_Equal,	/* type 4 only */
	RAWKEY_KP_Divide,
	RAWKEY_KP_Multiply,
	RAWKEY_Null,
	RAWKEY_L5,
	RAWKEY_KP_Delete,
	RAWKEY_L6,
	RAWKEY_Home,
	RAWKEY_Tab,
	RAWKEY_q,
	RAWKEY_w,
	RAWKEY_e,
	RAWKEY_r,
	RAWKEY_t,
	RAWKEY_y,
	RAWKEY_u,
	RAWKEY_i,
	RAWKEY_o,
	RAWKEY_p,
	RAWKEY_bracketleft,
	RAWKEY_bracketright,
	RAWKEY_Delete,
	RAWKEY_Compose,
	RAWKEY_KP_Home,
	RAWKEY_KP_Up,
	RAWKEY_KP_Prior,
	RAWKEY_KP_Subtract,
	RAWKEY_L7,
	RAWKEY_L8,
	RAWKEY_End,
	RAWKEY_Null,
	RAWKEY_Control_L,
	RAWKEY_a,
	RAWKEY_s,
	RAWKEY_d,
	RAWKEY_f,
	RAWKEY_g,
	RAWKEY_h,
	RAWKEY_j,
	RAWKEY_k,
	RAWKEY_l,
	RAWKEY_semicolon,
	RAWKEY_apostrophe,
	RAWKEY_backslash,
	RAWKEY_Return,
	RAWKEY_KP_Enter,
	RAWKEY_KP_Left,
	RAWKEY_KP_Begin,
	RAWKEY_KP_Right,
	RAWKEY_KP_Insert,
	RAWKEY_L9,
	RAWKEY_Prior,
	RAWKEY_L10,
	RAWKEY_Num_Lock,
	RAWKEY_Shift_L,
	RAWKEY_z,
	RAWKEY_x,
	RAWKEY_c,
	RAWKEY_v,
	RAWKEY_b,
	RAWKEY_n,
	RAWKEY_m,
	RAWKEY_comma,
	RAWKEY_period,
	RAWKEY_slash,
	RAWKEY_Shift_R,
	RAWKEY_Null,		/* KS_Linefeed on type 3/4 */
	RAWKEY_KP_End,
	RAWKEY_KP_Down,
	RAWKEY_KP_Next,
	RAWKEY_Null,
	RAWKEY_Null,
	RAWKEY_Null,
	RAWKEY_Help,
	RAWKEY_Caps_Lock,
	RAWKEY_Meta_L,
	RAWKEY_space,
	RAWKEY_Meta_R,
	RAWKEY_Next,
	RAWKEY_Null,
	RAWKEY_KP_Add,
	RAWKEY_Null,
	RAWKEY_AudioMute	/* type 5 remapped 0x2d */
};
#endif

#define	KC(n)	KS_KEYCODE(n)

/* 000/021/022 US English type 4/5 keyboard */
const keysym_t sunkbd_keydesc_us[] = {
    KC(0x01), KS_Cmd,
    KC(0x02), KS_Cmd_BrightnessDown,
    KC(0x03),				KS_Again,
    KC(0x04), KS_Cmd_BrightnessUp,
    KC(0x05),				KS_f1,
    KC(0x06),				KS_f2,
    KC(0x07),				KS_f10,
    KC(0x08),				KS_f3,
    KC(0x09),				KS_f11,
    KC(0x0a),				KS_f4,
    KC(0x0b),				KS_f12,
    KC(0x0c),				KS_f5,
    KC(0x0d),				KS_Alt_R,
    KC(0x0e),				KS_f6,
    KC(0x10),				KS_f7,
    KC(0x11),				KS_f8,
    KC(0x12),				KS_f9,
    KC(0x13),				KS_Alt_L,
    KC(0x14),				KS_Up,
    KC(0x15),				KS_Pause,
    KC(0x16),				KS_Print_Screen,
    KC(0x17),				KS_Hold_Screen,
    KC(0x18),				KS_Left,
    KC(0x19),				KS_Props,
    KC(0x1a),				KS_Undo,
    KC(0x1b),				KS_Down,
    KC(0x1c),				KS_Right,
    KC(0x1d),				KS_Escape,
    KC(0x1e),				KS_1,		KS_exclam,
    KC(0x1f),				KS_2,		KS_at,
    KC(0x20),				KS_3,		KS_numbersign,
    KC(0x21),				KS_4,		KS_dollar,
    KC(0x22),				KS_5,		KS_percent,
    KC(0x23),				KS_6,		KS_asciicircum,
    KC(0x24),				KS_7,		KS_ampersand,
    KC(0x25),				KS_8,		KS_asterisk,
    KC(0x26),				KS_9,		KS_parenleft,
    KC(0x27),				KS_0,		KS_parenright,
    KC(0x28),				KS_minus,	KS_underscore,
    KC(0x29),				KS_equal,	KS_plus,
    KC(0x2a),				KS_grave,	KS_asciitilde,
    KC(0x2b),				KS_Delete,
    KC(0x2c),				KS_Insert,
    KC(0x2d),				KS_KP_Equal,	/* type 4 */
    KC(0x2e),				KS_KP_Divide,
    KC(0x2f),				KS_KP_Multiply,
    KC(0x31),				KS_Front,
    KC(0x32),				KS_KP_Delete,	KS_KP_Decimal,
    KC(0x33),				KS_Copy,
    KC(0x34),				KS_Home,
    KC(0x35),				KS_Tab,		KS_Backtab,
    KC(0x36),				KS_q,
    KC(0x37),				KS_w,
    KC(0x38),				KS_e,
    KC(0x39),				KS_r,
    KC(0x3a),				KS_t,
    KC(0x3b),				KS_y,
    KC(0x3c),				KS_u,
    KC(0x3d),				KS_i,
    KC(0x3e),				KS_o,
    KC(0x3f),				KS_p,
    KC(0x40),				KS_bracketleft,	KS_braceleft,
    KC(0x41),				KS_bracketright,KS_braceright,
    KC(0x42),				KS_Delete,
    KC(0x43),				KS_Multi_key,
    KC(0x44),				KS_KP_Home,	KS_KP_7,
    KC(0x45),				KS_KP_Up,	KS_KP_8,
    KC(0x46),				KS_KP_Prior,	KS_KP_9,
    KC(0x47),				KS_KP_Subtract,
    KC(0x48),				KS_Open,
    KC(0x49),				KS_Paste,
    KC(0x4a),				KS_End,
    KC(0x4c),				KS_Control_L,
    KC(0x4d), KS_Cmd_Debugger,		KS_a,
    KC(0x4e),				KS_s,
    KC(0x4f),				KS_d,
    KC(0x50),				KS_f,
    KC(0x51),				KS_g,
    KC(0x52),				KS_h,
    KC(0x53),				KS_j,
    KC(0x54),				KS_k,
    KC(0x55),				KS_l,
    KC(0x56),				KS_semicolon,	KS_colon,
    KC(0x57),				KS_apostrophe,	KS_quotedbl,
    KC(0x58),				KS_backslash,	KS_bar,
    KC(0x59),				KS_Return,
    KC(0x5a),				KS_KP_Enter,
    KC(0x5b),				KS_KP_Left,	KS_KP_4,
    KC(0x5c),				KS_KP_Begin,	KS_KP_5,
    KC(0x5d),				KS_KP_Right,	KS_KP_6,
    KC(0x5e),				KS_KP_Insert,	KS_KP_0,
    KC(0x5f),				KS_Find,
    KC(0x60),				KS_Prior,
    KC(0x61),				KS_Cut,
    KC(0x62),				KS_Num_Lock,
    KC(0x63),				KS_Shift_L,
    KC(0x64),				KS_z,
    KC(0x65),				KS_x,
    KC(0x66),				KS_c,
    KC(0x67),				KS_v,
    KC(0x68),				KS_b,
    KC(0x69),				KS_n,
    KC(0x6a),				KS_m,
    KC(0x6b),				KS_comma,	KS_less,
    KC(0x6c),				KS_period,	KS_greater,
    KC(0x6d),				KS_slash,	KS_question,
    KC(0x6e),				KS_Shift_R,
    KC(0x6f),				KS_Linefeed,
    KC(0x70),				KS_KP_End,	KS_KP_1,
    KC(0x71),				KS_KP_Down,	KS_KP_2,
    KC(0x72),				KS_KP_Next,	KS_KP_3,
    KC(0x76),				KS_Help,
    KC(0x77),				KS_Caps_Lock,
    KC(0x78),				KS_Meta_L,
    KC(0x79),				KS_space,
    KC(0x7a),				KS_Meta_R,
    KC(0x7b),				KS_Next,
    KC(0x7d),				KS_KP_Add,
    KC(0x7f),				KS_AudioMute,	/* type 5 KC(0x2d) */
};

/* 002 French/Belgian type 4 keyboard */
const keysym_t sunkbd_keydesc_befr[] = {
    KC(0x0d),		KS_Caps_Lock,
    KC(0x0f),		KS_bracketright,KS_braceright,	KS_guillemotright,
    KC(0x1e),		KS_ampersand,	KS_1,
    KC(0x1f),		KS_eacute,	KS_2,		KS_twosuperior,
    KC(0x20),		KS_quotedbl,	KS_3,		KS_threesuperior,
    KC(0x21),		KS_apostrophe,	KS_4,
    KC(0x22),		KS_parenleft,	KS_5,
    KC(0x23),		KS_section,	KS_6,
    KC(0x24),		KS_egrave,	KS_7,
    KC(0x25),		KS_exclam,	KS_8,		KS_sterling,
    KC(0x26),		KS_ccedilla,	KS_9,		KS_backslash,
    KC(0x27),		KS_agrave,	KS_0,
    KC(0x28),		KS_parenright,	KS_degree,	KS_asciitilde,
    KC(0x29),		KS_minus,	KS_underscore,	KS_numbersign,
    KC(0x2a),		KS_asterisk,	KS_bar,		KS_currency,
    KC(0x36),		KS_a,
    KC(0x37),		KS_z,
    KC(0x40),		KS_dead_circumflex,KS_dead_diaeresis,
    KC(0x41),		KS_grave,	KS_dollar,	KS_at,
    KC(0x4d), KS_Cmd_Debugger,	KS_q,
    KC(0x56),		KS_m,		KS_M,		KS_mu,
    KC(0x57),		KS_ugrave,	KS_percent,
    KC(0x58),		KS_bracketleft,	KS_braceleft,	KS_guillemotleft,
    KC(0x64),		KS_w,
    KC(0x6a),		KS_comma,	KS_question,
    KC(0x6b),		KS_semicolon,	KS_period,
    KC(0x6c),		KS_colon,	KS_slash,
    KC(0x6d),		KS_equal,	KS_plus,
    KC(0x77),		KS_Mode_switch,
    KC(0x7c),		KS_less,	KS_greater,
};

/* 023 French type 5 keyboard */
const keysym_t sunkbd5_keydesc_fr[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1e),		KS_ampersand,	KS_1,
    KC(0x1f),		KS_eacute,	KS_2,		KS_asciitilde,
    KC(0x20),		KS_quotedbl,	KS_3,		KS_numbersign,
    KC(0x21),		KS_apostrophe,	KS_4,		KS_braceleft,
    KC(0x22),		KS_parenleft,	KS_5,		KS_bracketleft,
    KC(0x23),		KS_minus,	KS_6,		KS_bar,
    KC(0x24),		KS_egrave,	KS_7,		KS_grave,
    KC(0x25),		KS_underscore,	KS_8,		KS_backslash,
    KC(0x26),		KS_ccedilla,	KS_9,		KS_asciicircum,
    KC(0x27),		KS_agrave,	KS_0,		KS_at,
    KC(0x28),		KS_parenright,	KS_degree,	KS_bracketright,
    KC(0x29),		KS_equal,	KS_plus,	KS_braceright,
    KC(0x2a),		KS_twosuperior,
    KC(0x36),		KS_a,
    KC(0x37),		KS_z,
    KC(0x40),		KS_dead_circumflex,KS_dead_diaeresis,
    KC(0x41),		KS_dollar,	KS_sterling,	KS_currency,
    KC(0x4d), KS_Cmd_Debugger,	KS_q,
    KC(0x56),		KS_m,
    KC(0x57),		KS_ugrave,	KS_percent,
    KC(0x58),		KS_asterisk,	KS_mu,
    KC(0x64),		KS_w,
    KC(0x6a),		KS_comma,	KS_question,
    KC(0x6b),		KS_semicolon,	KS_period,
    KC(0x6c),		KS_colon,	KS_slash,
    KC(0x6d),		KS_exclam,	KS_section,
    KC(0x7c),		KS_less,	KS_greater,
};

/* 004 Danish type 4 keyboard */
const keysym_t sunkbd_keydesc_dk[] = {
    KC(0x0d),		KS_Multi_key,
    KC(0x0f),		KS_asciitilde,	KS_asciicircum, 
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_numbersign,	KS_sterling,
    KC(0x21),		KS_4,		KS_currency,	KS_dollar,
    KC(0x23),		KS_6,		KS_ampersand,
    KC(0x24),		KS_7,		KS_slash,	KS_braceleft,
    KC(0x25),		KS_8,		KS_parenleft,	KS_bracketleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_bracketright,
    KC(0x27),		KS_0,		KS_equal,	KS_braceright,
    KC(0x28),		KS_plus,	KS_question,
    KC(0x29),		KS_dead_acute,	KS_dead_grave,	KS_bar,
    KC(0x2a),		KS_apostrophe,	KS_asterisk,	KS_grave,
    KC(0x40),		KS_aring,
    KC(0x41),		KS_dead_diaeresis,KS_dead_circumflex,KS_dead_tilde,
    KC(0x43),		KS_Mode_switch,
    KC(0x4c),		KS_Caps_Lock,
    KC(0x56),		KS_ae,
    KC(0x57),		KS_oslash,
    KC(0x58),		KS_onehalf,	KS_section,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x77),		KS_Control_L,
    KC(0x7c),		KS_less,	KS_greater,	KS_backslash,
};

/* 024 Danish type 5 keyboard */
const keysym_t sunkbd5_keydesc_dk[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_numbersign,	KS_sterling,
    KC(0x21),		KS_4,		KS_currency,	KS_dollar,
    KC(0x22),		KS_5,		KS_percent,	KS_asciitilde,
    KC(0x23),		KS_6,		KS_ampersand,	KS_asciicircum,
    KC(0x24),		KS_7,		KS_slash,	KS_braceleft,
    KC(0x25),		KS_8,		KS_parenleft,	KS_bracketleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_bracketright,
    KC(0x27),		KS_0,		KS_equal,	KS_braceright,
    KC(0x28),		KS_plus,	KS_question,
    KC(0x29),		KS_dead_acute,	KS_dead_grave,	KS_bar,
    KC(0x2a),		KS_onehalf,	KS_asterisk,	KS_grave,
    KC(0x40),		KS_aring,
    KC(0x41),		KS_dead_diaeresis,KS_dead_circumflex,KS_dead_tilde,
    KC(0x56),		KS_ae,
    KC(0x57),		KS_oslash,
    KC(0x58),		KS_backslash,	KS_asterisk,	KS_grave,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x7c),		KS_less,	KS_greater,	KS_backslash,
};

/* 005 German type 4 keyboard */
const keysym_t sunkbd_keydesc_de[] = {
    KC(0x0d),		KS_Alt_L,
    KC(0x0f),		KS_bracketright,KS_braceright,	KS_guillemotright,
    KC(0x13),		KS_Mode_switch,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_twosuperior,
    KC(0x20),		KS_3,		KS_section,	KS_threesuperior,
    KC(0x23),		KS_6,		KS_ampersand,
    KC(0x24),		KS_7,		KS_slash,	KS_degree,
    KC(0x25),		KS_8,		KS_parenleft,	KS_grave,
    KC(0x26),		KS_9,		KS_parenright,	KS_apostrophe,
    KC(0x27),		KS_0,		KS_equal,	KS_bar,
    KC(0x28),		KS_ssharp,	KS_question,	KS_backslash,
    KC(0x29),		KS_dead_acute,	KS_dead_grave,
    KC(0x2a),		KS_numbersign,	KS_asciicircum,	KS_at,
    KC(0x3b),		KS_z,
    KC(0x40),		KS_udiaeresis,
    KC(0x41),		KS_plus,	KS_asterisk,	KS_asciitilde,
    KC(0x4c),		KS_Caps_Lock,
    KC(0x56),		KS_odiaeresis,
    KC(0x57),		KS_adiaeresis,
    KC(0x58),		KS_bracketleft,	KS_braceleft,	KS_guillemotleft,
    KC(0x64),		KS_y,
    KC(0x6a),		KS_m,		KS_M,		KS_mu,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x77),		KS_Control_L,
    KC(0x7c),		KS_less,	KS_greater,
};

/* 025 German type 5 keyboard */
const keysym_t sunkbd5_keydesc_de[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_twosuperior,
    KC(0x20),		KS_3,		KS_section,	KS_threesuperior,
    KC(0x23),		KS_6,		KS_ampersand,
    KC(0x24),		KS_7,		KS_slash,	KS_braceleft,
    KC(0x25),		KS_8,		KS_parenleft,	KS_bracketleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_bracketright,
    KC(0x27),		KS_0,		KS_equal,	KS_braceright,
    KC(0x28),		KS_ssharp,	KS_question,	KS_backslash,
    KC(0x29),		KS_dead_acute,	KS_dead_grave,
    KC(0x2a),		KS_asciicircum,	KS_degree,
    KC(0x36),		KS_q,		KS_Q,		KS_at,
    KC(0x3b),		KS_z,
    KC(0x40),		KS_udiaeresis,
    KC(0x41),		KS_plus,	KS_asterisk,	KS_asciitilde,
    KC(0x56),		KS_odiaeresis,
    KC(0x57),		KS_adiaeresis,
    KC(0x58),		KS_numbersign,	KS_apostrophe,	KS_grave,
    KC(0x64),		KS_y,
    KC(0x6a),		KS_m,		KS_M,		KS_mu,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x7c),		KS_less,	KS_greater,	KS_bar,
};

/* 006 Italian type 4 keyboard */
const keysym_t sunkbd_keydesc_it[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x0f),		KS_bracketright,KS_braceright,	KS_guillemotright,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_twosuperior,
    KC(0x20),		KS_3,		KS_sterling,	KS_threesuperior,
    KC(0x23),		KS_6,		KS_ampersand,	KS_notsign,
    KC(0x24),		KS_7,		KS_slash,
    KC(0x25),		KS_8,		KS_parenleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_backslash,
    KC(0x27),		KS_0,		KS_equal,	KS_bar,
    KC(0x28),		KS_apostrophe,	KS_question,	KS_grave,
    KC(0x29),		KS_igrave,	KS_asciicircum,
    KC(0x2a),		KS_ugrave,	KS_section,
    KC(0x40),		KS_egrave,	KS_eacute,
    KC(0x41),		KS_plus,	KS_asterisk,	KS_asciitilde,
    KC(0x4c),		KS_Caps_Lock,
    KC(0x56),		KS_ograve,	KS_ccedilla,	KS_at,
    KC(0x57),		KS_agrave,	KS_degree,	KS_numbersign,
    KC(0x58),		KS_bracketleft,	KS_braceleft,	KS_guillemotleft,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x77),		KS_Control_L,
    KC(0x7c),		KS_less,	KS_greater,
};

/* 026 Italian type 5 keyboard */
const keysym_t sunkbd5_keydesc_it[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1f),		KS_2,		KS_quotedbl,
    KC(0x20),		KS_3,		KS_sterling,
    KC(0x23),		KS_6,		KS_ampersand,
    KC(0x24),		KS_7,		KS_slash,
    KC(0x25),		KS_8,		KS_parenleft,	KS_braceleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_braceright,
    KC(0x27),		KS_0,		KS_equal,
    KC(0x28),		KS_apostrophe,	KS_question,	KS_grave,
    KC(0x29),		KS_igrave,	KS_asciicircum,
    KC(0x2a),		KS_backslash,	KS_bar,
    KC(0x40),		KS_egrave,	KS_eacute,	KS_bracketleft,
    KC(0x41),		KS_plus,	KS_asterisk,	KS_bracketright,
    KC(0x56),		KS_ograve,	KS_ccedilla,	KS_at,
    KC(0x57),		KS_agrave,	KS_degree,	KS_numbersign,
    KC(0x58),		KS_ugrave,	KS_section,	KS_asciitilde,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x7c),		KS_less,	KS_greater,
};

/* 007 Dutch type 4 keyboard */
const keysym_t sunkbd_keydesc_nl[] = {
    KC(0x0d),		KS_Caps_Lock,
    KC(0x0f),		KS_backslash,	KS_bar,
    KC(0x1e),		KS_1,		KS_exclam,	KS_onesuperior,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_twosuperior,
    KC(0x20),		KS_3,		KS_numbersign,	KS_threesuperior,
    KC(0x21),		KS_4,		KS_dollar,	KS_onequarter,
    KC(0x22),		KS_5,		KS_percent,	KS_onehalf,
    KC(0x23),		KS_6,		KS_ampersand,	KS_threequarters,
    KC(0x24),		KS_7,		KS_underscore,	KS_sterling,
    KC(0x25),		KS_8,		KS_parenleft,	KS_braceleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_braceright,
    KC(0x27),		KS_0,		KS_apostrophe,	KS_grave,
    KC(0x28),		KS_slash,	KS_question,
    KC(0x29),		KS_degree,	KS_dead_tilde,	KS_dead_abovering,
    KC(0x2a),		KS_less,	KS_greater,
    KC(0x40),		KS_dead_diaeresis,KS_dead_circumflex,
    KC(0x41),		KS_asterisk,	KS_brokenbar,	KS_asciitilde,
    KC(0x4e),		KS_s,		KS_S,		KS_ssharp,
    KC(0x56),		KS_plus,	KS_plusminus,
    KC(0x57),		KS_dead_acute,	KS_dead_grave,
    KC(0x58),		KS_at,		KS_section,	KS_notsign,
    KC(0x64),		KS_z,		KS_Z,		KS_guillemotleft,
    KC(0x65),		KS_x,		KS_X,		KS_guillemotright,
    KC(0x66),		KS_c,		KS_C,		KS_cent,
    KC(0x6a),		KS_m,		KS_M,		KS_mu,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_equal,
    KC(0x77),		KS_Mode_switch,
    KC(0x7d),		KS_bracketright,KS_bracketleft,
};

/* 027 Dutch type 5 keyboard */
const keysym_t sunkbd5_keydesc_nl[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1e),		KS_1,		KS_exclam,	KS_onesuperior,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_twosuperior,
    KC(0x20),		KS_3,		KS_numbersign,	KS_threesuperior,
    KC(0x21),		KS_4,		KS_dollar,	KS_onequarter,
    KC(0x22),		KS_5,		KS_percent,	KS_onehalf,
    KC(0x23),		KS_6,		KS_ampersand,	KS_threequarters,
    KC(0x24),		KS_7,		KS_underscore,	KS_sterling,
    KC(0x25),		KS_8,		KS_parenleft,	KS_braceleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_braceright,
    KC(0x27),		KS_0,		KS_apostrophe,	KS_grave,
    KC(0x28),		KS_slash,	KS_question,	KS_backslash,
    KC(0x29),		KS_degree,	KS_dead_tilde,	KS_dead_abovering,
    KC(0x2a),		KS_at,		KS_section,	KS_notsign,
    KC(0x40),		KS_dead_diaeresis,KS_dead_circumflex,
    KC(0x41),		KS_asterisk,	KS_bar,		KS_asciitilde,
    KC(0x4e),		KS_s,		KS_S,		KS_ssharp,
    KC(0x56),		KS_plus,	KS_plusminus,
    KC(0x57),		KS_dead_acute,	KS_dead_grave,
    KC(0x58),		KS_less,	KS_greater,	KS_asciicircum,
    KC(0x64),		KS_z,		KS_Z,		KS_guillemotleft,
    KC(0x65),		KS_x,		KS_X,		KS_guillemotright,
    KC(0x66),		KS_c,		KS_C,		KS_cent,
    KC(0x6a),		KS_m,		KS_M,		KS_mu,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,	KS_hyphen,
    KC(0x6d),		KS_minus,	KS_equal,
    KC(0x7d),		KS_bracketright,KS_bracketleft,	KS_brokenbar,
};

/* 008 Norwegian type 4 keyboard */
const keysym_t sunkbd_keydesc_no[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x0f),		KS_asciitilde,	KS_asciicircum,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_numbersign,	KS_sterling,
    KC(0x21),		KS_4,		KS_currency,	KS_dollar,
    KC(0x23),		KS_6,		KS_ampersand,
    KC(0x24),		KS_7,		KS_slash,	KS_braceleft,
    KC(0x25),		KS_8,		KS_parenleft,	KS_bracketleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_bracketright,
    KC(0x27),		KS_0,		KS_equal,	KS_braceright,
    KC(0x28),		KS_plus,	KS_question,
    KC(0x29),		KS_backslash,	KS_dead_grave,	KS_dead_acute,
    KC(0x2a),		KS_apostrophe,	KS_asterisk,	KS_grave,
    KC(0x40),		KS_aring,
    KC(0x41),		KS_dead_diaeresis,KS_dead_circumflex,KS_dead_tilde,
    KC(0x4c),		KS_Caps_Lock,
    KC(0x56),		KS_oslash,
    KC(0x57),		KS_ae,
    KC(0x58),		KS_bar,		KS_section,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x77),		KS_Control_L,
    KC(0x7d),		KS_less,	KS_greater,
};

/* 028 Norwegian type 5 keyboard */
const keysym_t sunkbd5_keydesc_no[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_numbersign,	KS_sterling,
    KC(0x21),		KS_4,		KS_currency,	KS_dollar,
    KC(0x22),		KS_5,		KS_percent,	KS_asciitilde,
    KC(0x23),		KS_6,		KS_ampersand,	KS_asciicircum,
    KC(0x24),		KS_7,		KS_slash,	KS_braceleft,
    KC(0x25),		KS_8,		KS_parenleft,	KS_bracketleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_bracketright,
    KC(0x27),		KS_0,		KS_equal,	KS_braceright,
    KC(0x28),		KS_plus,	KS_question,
    KC(0x29),		KS_backslash,	KS_dead_grave,	KS_dead_acute,
    KC(0x2a),		KS_bar,		KS_section,
    KC(0x40),		KS_aring,
    KC(0x41),		KS_dead_diaeresis,KS_dead_circumflex,KS_dead_tilde,
    KC(0x56),		KS_oslash,
    KC(0x57),		KS_ae,
    KC(0x58),		KS_apostrophe,	KS_asterisk,	KS_grave,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x7d),		KS_less,	KS_greater,
};

/* 009 Portuguese type 4 keyboard */
const keysym_t sunkbd_keydesc_pt[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x0f),		KS_bracketright,KS_braceright,	KS_guillemotright,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_numbersign,	KS_sterling,
    KC(0x21),		KS_4,		KS_dollar,	KS_section,
    KC(0x23),		KS_6,		KS_ampersand,	KS_notsign,
    KC(0x24),		KS_7,		KS_slash,
    KC(0x25),		KS_8,		KS_parenleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_backslash,
    KC(0x27),		KS_0,		KS_equal,	KS_bar,
    KC(0x28),		KS_apostrophe,	KS_question,	KS_grave,
    KC(0x29),		KS_exclamdown,	KS_questiondown,
    KC(0x2a),		KS_dead_tilde,	KS_dead_circumflex,KS_asciicircum,
    KC(0x40),		KS_dead_diaeresis,KS_asterisk,	KS_plus,
    KC(0x41),		KS_dead_acute,	KS_dead_grave,	KS_asciitilde,
    KC(0x4c),		KS_Caps_Lock,
    KC(0x56),		KS_ccedilla,
    KC(0x57),		KS_masculine,	KS_ordfeminine,
    KC(0x58),		KS_bracketleft,	KS_braceleft,	KS_guillemotleft,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x77),		KS_Control_L,
    KC(0x7d),		KS_less,	KS_greater,
};

/* 029 Portuguese type 4 keyboard */
const keysym_t sunkbd5_keydesc_pt[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_numbersign,	KS_sterling,
    KC(0x21),		KS_4,		KS_dollar,	KS_section,
    KC(0x22),		KS_5,		KS_percent,	KS_asciitilde,
    KC(0x23),		KS_6,		KS_ampersand,	KS_asciicircum,
    KC(0x24),		KS_7,		KS_slash,	KS_braceleft,
    KC(0x25),		KS_8,		KS_parenleft,	KS_bracketleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_bracketright,
    KC(0x27),		KS_0,		KS_equal,	KS_braceright,
    KC(0x28),		KS_apostrophe,	KS_question,	KS_grave,
    KC(0x29),		KS_guillemotleft,KS_guillemotright,
    KC(0x2a),		KS_backslash,	KS_bar,
    KC(0x40),		KS_plus,	KS_asterisk,	KS_dead_diaeresis,
    KC(0x41),		KS_dead_acute,	KS_dead_grave,
    KC(0x56),		KS_ccedilla,
    KC(0x57),		KS_masculine,	KS_ordfeminine,
    KC(0x58),		KS_dead_tilde,	KS_dead_circumflex,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x7d),		KS_less,	KS_greater,
};

/* 00a Spanish type 4 keyboard */
const keysym_t sunkbd_keydesc_es[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x0f),		KS_bracketright,KS_braceright,	KS_guillemotright,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_hyphen,	KS_numbersign,
    KC(0x22),		KS_5,		KS_percent,	KS_degree,
    KC(0x23),		KS_6,		KS_ampersand,	KS_notsign,
    KC(0x24),		KS_7,		KS_slash,
    KC(0x25),		KS_8,		KS_parenleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_backslash,
    KC(0x27),		KS_0,		KS_equal,	KS_bar,
    KC(0x28),		KS_apostrophe,	KS_question,	KS_grave,
    KC(0x29),		KS_exclamdown,	KS_questiondown,
    KC(0x2a),		KS_ccedilla,
    KC(0x3e),		KS_o,		KS_O,		KS_masculine,
    KC(0x40),		KS_dead_grave,	KS_dead_circumflex,KS_asciicircum,
    KC(0x41),		KS_plus,	KS_asterisk,	KS_asciitilde,
    KC(0x4c),		KS_Caps_Lock,
    KC(0x4d),		KS_a,		KS_A,		KS_ordfeminine,
    KC(0x56),		KS_ntilde,
    KC(0x57),		KS_dead_acute,	KS_dead_diaeresis,
    KC(0x58),		KS_bracketleft,	KS_braceleft,	KS_guillemotleft,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x77),		KS_Control_L,
    KC(0x7d),		KS_less,	KS_greater,
};

/* 02a Spanish type 5 keyboard */
const keysym_t sunkbd5_keydesc_es[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1e),		KS_1,		KS_exclam,	KS_bar,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_hyphen,	KS_numbersign,
    KC(0x21),		KS_4,		KS_dollar,	KS_asciicircum,
    KC(0x22),		KS_5,		KS_percent,	KS_asciitilde,
    KC(0x23),		KS_6,		KS_ampersand,	KS_notsign,
    KC(0x24),		KS_7,		KS_slash,
    KC(0x25),		KS_8,		KS_parenleft,
    KC(0x26),		KS_9,		KS_parenright,
    KC(0x27),		KS_0,		KS_equal,
    KC(0x28),		KS_apostrophe,	KS_question,	KS_grave,
    KC(0x29),		KS_exclamdown,	KS_questiondown,
    KC(0x2a),		KS_masculine,	KS_ordfeminine,	KS_backslash,
    KC(0x40),		KS_dead_grave,	KS_dead_circumflex,KS_bracketleft,
    KC(0x41),		KS_plus,	KS_asterisk,	KS_bracketright,
    KC(0x56),		KS_ntilde,
    KC(0x57),		KS_dead_acute,	KS_dead_diaeresis,KS_braceleft,
    KC(0x58),		KS_ccedilla,	KS_Ccedilla,	KS_braceright,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x7d),		KS_less,	KS_greater,
};

/* 00b Swedish/Finnish type 4 keyboard */
const keysym_t sunkbd_keydesc_sv[] = {
    KC(0x0d),		KS_Multi_key,
    KC(0x0f),		KS_asciitilde,	KS_asciicircum, 
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_numbersign,	KS_sterling,
    KC(0x21),		KS_4,		KS_currency,	KS_dollar,
    KC(0x23),		KS_6,		KS_ampersand,
    KC(0x24),		KS_7,		KS_slash,	KS_braceleft,
    KC(0x25),		KS_8,		KS_parenleft,	KS_bracketleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_bracketright,
    KC(0x27),		KS_0,		KS_equal,	KS_braceright,
    KC(0x28),		KS_plus,	KS_question,	KS_backslash,
    KC(0x29),		KS_dead_acute,	KS_dead_grave,
    KC(0x2a),		KS_apostrophe,	KS_asterisk,	KS_grave,
    KC(0x40),		KS_aring,
    KC(0x41),		KS_dead_diaeresis,KS_dead_circumflex,KS_dead_tilde,
    KC(0x43),		KS_Mode_switch,
    KC(0x4c),		KS_Caps_Lock,
    KC(0x56),		KS_odiaeresis,
    KC(0x57),		KS_adiaeresis,
    KC(0x58),		KS_section,	KS_onehalf,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x77),		KS_Control_L,
    KC(0x7c),		KS_less,	KS_greater,	KS_bar,
};

const keysym_t sunkbd_keydesc_sv_nodead[] = {
    KC(0x29),		KS_apostrophe,	KS_grave,
    KC(0x41),		KS_diaeresis,	KS_asciicircum,	KS_asciitilde,
};

/* 02b Swedish type 5 keyboard */
const keysym_t sunkbd5_keydesc_sv[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x0f),		KS_asciitilde,	KS_asciicircum, 
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_numbersign,	KS_sterling,
    KC(0x21),		KS_4,		KS_currency,	KS_dollar,
    KC(0x23),		KS_6,		KS_ampersand,
    KC(0x24),		KS_7,		KS_slash,	KS_braceleft,
    KC(0x25),		KS_8,		KS_parenleft,	KS_bracketleft,
    KC(0x26),		KS_9,		KS_parenright,	KS_bracketright,
    KC(0x27),		KS_0,		KS_equal,	KS_braceright,
    KC(0x28),		KS_plus,	KS_question,	KS_backslash,
    KC(0x29),		KS_dead_acute,	KS_dead_grave,
    KC(0x2a),		KS_section,	KS_onehalf,
    KC(0x40),		KS_aring,
    KC(0x41),		KS_dead_diaeresis,KS_dead_circumflex,KS_dead_tilde,
    KC(0x43),		KS_Multi_key,
    KC(0x4c),		KS_Control_L,
    KC(0x56),		KS_odiaeresis,
    KC(0x57),		KS_adiaeresis,
    KC(0x58),		KS_apostrophe,	KS_asterisk,	KS_grave,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x77),		KS_Caps_Lock,
    KC(0x7c),		KS_less,	KS_greater,	KS_bar,
};

/* 00c Swiss-French type 4 keyboard */
const keysym_t sunkbd_keydesc_sf[] = {
    KC(0x0d),		KS_Multi_key,
    KC(0x0f),		KS_greater,	KS_braceright,
    KC(0x1e),		KS_1,		KS_plus,	KS_exclam,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_asterisk,	KS_numbersign,
    KC(0x21),		KS_4,		KS_ccedilla,	KS_cent,
    KC(0x22),		KS_5,		KS_percent,	KS_asciitilde,
    KC(0x23),		KS_6,		KS_ampersand,	KS_section,
    KC(0x24),		KS_7,		KS_slash,	KS_bar,
    KC(0x25),		KS_8,		KS_parenleft,	KS_degree,
    KC(0x26),		KS_9,		KS_parenright,	KS_backslash,
    KC(0x27),		KS_0,		KS_equal,	KS_asciicircum,
    KC(0x28),		KS_apostrophe,	KS_question,	KS_grave,
    KC(0x29),		KS_dead_circumflex,KS_dead_grave,
    KC(0x2a),		KS_dollar,	KS_dead_tilde,	KS_sterling,
    KC(0x3b),		KS_z,
    KC(0x40),		KS_egrave,	KS_udiaeresis,
    KC(0x41),		KS_dead_diaeresis,KS_dead_acute,
    KC(0x43),		KS_Mode_switch,
    KC(0x4c),		KS_Caps_Lock,
    KC(0x56),		KS_eacute,	KS_odiaeresis,
    KC(0x57),		KS_agrave,	KS_adiaeresis,
    KC(0x58),		KS_less,	KS_braceleft,
    KC(0x64),		KS_y,
    KC(0x6a),		KS_m,		KS_M,		KS_mu,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x77),		KS_Control_L,
    KC(0x7c),		KS_bracketright,KS_bracketleft,	KS_backslash,
};

/* 02c Swiss-French type 5 keyboard */
const keysym_t sunkbd5_keydesc_sf[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1e),		KS_1,		KS_plus,	KS_bar,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_asterisk,	KS_numbersign,
    KC(0x21),		KS_4,		KS_ccedilla,	KS_asciicircum,
    KC(0x22),		KS_5,		KS_percent,	KS_asciitilde,
    KC(0x23),		KS_6,		KS_ampersand,
    KC(0x24),		KS_7,		KS_slash,
    KC(0x25),		KS_8,		KS_parenleft,
    KC(0x26),		KS_9,		KS_parenright,
    KC(0x27),		KS_0,		KS_equal,	KS_grave,
    KC(0x28),		KS_apostrophe,	KS_question,	KS_dead_acute,
    KC(0x29),		KS_dead_circumflex,KS_dead_grave,KS_dead_tilde,
    KC(0x2a),		KS_dollar,	KS_degree,
    KC(0x3b),		KS_z,
    KC(0x40),		KS_egrave,	KS_udiaeresis,	KS_bracketleft,
    KC(0x41),		KS_dead_diaeresis,KS_exclam,	KS_bracketright,
    KC(0x56),		KS_eacute,	KS_odiaeresis,
    KC(0x57),		KS_agrave,	KS_adiaeresis,	KS_braceleft,
    KC(0x58),		KS_dollar,	KS_sterling,	KS_braceright,
    KC(0x64),		KS_y,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x7c),		KS_less,	KS_greater,	KS_backslash,
};

/* 00d Swiss-German type 4 keyboard */
const keysym_t sunkbd_keydesc_sg[] = {
    KC(0x0d),		KS_Multi_key,
    KC(0x0f),		KS_greater,	KS_braceright,
    KC(0x1e),		KS_1,		KS_plus,	KS_exclam,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_asterisk,	KS_numbersign,
    KC(0x21),		KS_4,		KS_ccedilla,	KS_cent,
    KC(0x22),		KS_5,		KS_percent,	KS_asciitilde,
    KC(0x23),		KS_6,		KS_ampersand,	KS_section,
    KC(0x24),		KS_7,		KS_slash,	KS_bar,
    KC(0x25),		KS_8,		KS_parenleft,	KS_degree,
    KC(0x26),		KS_9,		KS_parenright,	KS_backslash,
    KC(0x27),		KS_0,		KS_equal,	KS_asciicircum,
    KC(0x28),		KS_apostrophe,	KS_question,	KS_grave,
    KC(0x29),		KS_dead_circumflex,KS_dead_grave,
    KC(0x2a),		KS_dollar,	KS_dead_tilde,	KS_sterling,
    KC(0x3b),		KS_z,
    KC(0x40),		KS_udiaeresis,	KS_egrave,
    KC(0x41),		KS_dead_diaeresis,KS_dead_acute,
    KC(0x43),		KS_Mode_switch,
    KC(0x4c),		KS_Caps_Lock,
    KC(0x56),		KS_odiaeresis,	KS_eacute,
    KC(0x57),		KS_adiaeresis,	KS_agrave,
    KC(0x58),		KS_less,	KS_braceleft,
    KC(0x64),		KS_y,
    KC(0x6a),		KS_m,		KS_M,		KS_mu,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x77),		KS_Control_L,
    KC(0x7c),		KS_bracketright,KS_bracketleft,	KS_backslash,
};

/* 02d Swiss-German type 5 keyboard */
const keysym_t sunkbd5_keydesc_sg[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1e),		KS_1,		KS_plus,	KS_bar,
    KC(0x1f),		KS_2,		KS_quotedbl,	KS_at,
    KC(0x20),		KS_3,		KS_asterisk,	KS_numbersign,
    KC(0x21),		KS_4,		KS_ccedilla,	KS_asciicircum,
    KC(0x22),		KS_5,		KS_percent,	KS_asciitilde,
    KC(0x23),		KS_6,		KS_ampersand,
    KC(0x24),		KS_7,		KS_slash,
    KC(0x25),		KS_8,		KS_parenleft,
    KC(0x26),		KS_9,		KS_parenright,
    KC(0x27),		KS_0,		KS_equal,	KS_grave,
    KC(0x28),		KS_apostrophe,	KS_question,	KS_dead_acute,
    KC(0x29),		KS_dead_circumflex,KS_dead_grave,KS_dead_tilde,
    KC(0x2a),		KS_dollar,	KS_degree,
    KC(0x3b),		KS_z,
    KC(0x40),		KS_udiaeresis,	KS_egrave,	KS_bracketleft,
    KC(0x41),		KS_dead_diaeresis,KS_exclam,	KS_bracketright,
    KC(0x56),		KS_odiaeresis,	KS_eacute,
    KC(0x57),		KS_adiaeresis,	KS_agrave,	KS_braceleft,
    KC(0x58),		KS_dollar,	KS_sterling,	KS_braceright,
    KC(0x64),		KS_y,
    KC(0x6b),		KS_comma,	KS_semicolon,
    KC(0x6c),		KS_period,	KS_colon,
    KC(0x6d),		KS_minus,	KS_underscore,
    KC(0x7c),		KS_less,	KS_greater,	KS_backslash,
};

/* 00e UK English type 4 keyboard */
const keysym_t sunkbd_keydesc_uk[] = {
    KC(0x1e),		KS_1,		KS_exclam,	KS_bar,
    KC(0x21),		KS_3,		KS_sterling,	KS_numbersign,
    KC(0x28),		KS_minus,	KS_underscore,	KS_notsign,
    KC(0x43),		KS_Mode_switch,
};

/* 02e UK English type 5 keyboard */
const keysym_t sunkbd5_keydesc_uk[] = {
    KC(0x0d),		KS_Mode_switch,
    KC(0x1f),		KS_2,		KS_quotedbl,
    KC(0x20),		KS_3,		KS_sterling,
    KC(0x2a),		KS_grave,	KS_notsign,	KS_brokenbar,
    KC(0x57),		KS_apostrophe,	KS_at,
    KC(0x58),		KS_numbersign,	KS_asciitilde,
    KC(0x7c),		KS_backslash,	KS_bar,
};

/* 031 Japan type 5 keyboard */
const keysym_t sunkbd5_keydesc_jp[] = {
    KC(0x1f),		KS_2,		KS_quotedbl,
    KC(0x23),		KS_6,		KS_ampersand,
    KC(0x24),		KS_7,		KS_apostrophe,
    KC(0x25),		KS_8,		KS_parenleft,
    KC(0x26),		KS_9,		KS_parenright,
    KC(0x27),		KS_0,
    KC(0x28),		KS_minus,	KS_equal,
    KC(0x29),		KS_asciicircum,	KS_asciitilde,
    KC(0x2a),		KS_yen,		KS_bar,
    KC(0x40),		KS_at,		KS_grave,
    KC(0x41),		KS_bracketleft,	KS_braceleft,
    KC(0x56),		KS_semicolon,	KS_plus,
    KC(0x57),		KS_colon,	KS_asterisk,
    KC(0x58),		KS_bracketright,KS_braceright,
    KC(0x7c),		KS_backslash,	KS_underscore,
};

#define KBD_MAP(name, base, map) \
    { name, base, sizeof(map)/sizeof(keysym_t), map }

/* Supported type 4 keyboard layouts */
const struct wscons_keydesc sunkbd_keydesctab[] = {
	KBD_MAP(KB_US,			0,	sunkbd_keydesc_us),
	KBD_MAP(KB_BE,			KB_US,	sunkbd_keydesc_befr),
	KBD_MAP(KB_DE,			KB_US,	sunkbd_keydesc_de),
	KBD_MAP(KB_DK,			KB_US,	sunkbd_keydesc_dk),
	KBD_MAP(KB_ES,			KB_US,	sunkbd_keydesc_es),
	KBD_MAP(KB_FR,			KB_US,	sunkbd_keydesc_befr),
	KBD_MAP(KB_IT,			KB_US,	sunkbd_keydesc_it),
	KBD_MAP(KB_NL,			KB_US,	sunkbd_keydesc_nl),
	KBD_MAP(KB_NO,			KB_US,	sunkbd_keydesc_no),
	KBD_MAP(KB_PT,			KB_US,	sunkbd_keydesc_pt),
	KBD_MAP(KB_SF,			KB_US,	sunkbd_keydesc_sf),
	KBD_MAP(KB_SG,			KB_US,	sunkbd_keydesc_sg),
	KBD_MAP(KB_SV,			KB_US,	sunkbd_keydesc_sv),
	KBD_MAP(KB_SV | KB_NODEAD,	KB_SV,	sunkbd_keydesc_sv_nodead),
	KBD_MAP(KB_UK,			KB_US,	sunkbd_keydesc_uk),
	{0, 0, 0, 0},
};

/* Supported type 5 keyboard layouts */
const struct wscons_keydesc sunkbd5_keydesctab[] = {
	KBD_MAP(KB_US,			0,	sunkbd_keydesc_us),
	KBD_MAP(KB_DE,			KB_US,	sunkbd5_keydesc_de),
	KBD_MAP(KB_DK,			KB_US,	sunkbd5_keydesc_dk),
	KBD_MAP(KB_ES,			KB_US,	sunkbd5_keydesc_es),
	KBD_MAP(KB_FR,			KB_US,	sunkbd5_keydesc_fr),
	KBD_MAP(KB_IT,			KB_US,	sunkbd5_keydesc_it),
	KBD_MAP(KB_JP,			KB_US,	sunkbd5_keydesc_jp),
	KBD_MAP(KB_NL,			KB_US,	sunkbd5_keydesc_nl),
	KBD_MAP(KB_NO,			KB_US,	sunkbd5_keydesc_no),
	KBD_MAP(KB_PT,			KB_US,	sunkbd5_keydesc_pt),
	KBD_MAP(KB_SF,			KB_US,	sunkbd5_keydesc_sf),
	KBD_MAP(KB_SG,			KB_US,	sunkbd5_keydesc_sg),
	KBD_MAP(KB_SV,			KB_US,	sunkbd5_keydesc_sv),
	KBD_MAP(KB_SV | KB_NODEAD,	KB_SV,	sunkbd_keydesc_sv_nodead),
	KBD_MAP(KB_UK,			KB_US,	sunkbd5_keydesc_uk),
	{0, 0, 0, 0},
};

/*
 * Keyboard layout to ID table
 * References:
 *	Sun Type 5 Keyboard Supplement Installation Guide, May 1992
 *	http://docs.sun.com/db/doc/806-6642/6jfipqu57?a=view
 *	http://jp.sunsolve.sun.com/handbook_pub/Systems/SSVygr/INPUT_Compact1_Keyboard.html
 */
const int sunkbd_layouts[MAXSUNLAYOUT] = {
	/* Type 4 layouts */
	KB_US,	/* 000 USA */
	KB_US,	/* 001 same as 000 */
	KB_BE,	/* 002 Belgium/French */
	-1,	/* 003 Canada */
	KB_DK,	/* 004 Denmark */
	KB_DE,	/* 005 Germany */
	KB_IT,	/* 006 Italy */
	KB_NL,	/* 007 The Netherlands */
	KB_NO,	/* 008 Norway */
	KB_PT,	/* 009 Portugal */
	KB_ES,	/* 00a Latin America/Spanish */
	KB_SV,	/* 00b Sweden */
	KB_SF,	/* 00c Switzerland/French */
	KB_SG,	/* 00d Switzerland/German */
	KB_UK,	/* 00e Great Britain */
	-1,	/* 00f unaffected */
	-1,	/* 010 Korea */
	-1,	/* 011 Taiwan */
	-1,	/* 012 unaffected */
	-1,	/* 013 unaffected */
	-1,	/* 014 VT220 */
	-1,	/* 015 VT220 Switzerland/French */
	-1,	/* 016 VT220 Switzerland/German */
	-1,	/* 017 VT220 Switzerland/Italian */
	-1,	/* 018 unaffected */
	-1,	/* 019 Belgium */
	-1,	/* 01a unaffected */
	-1,	/* 01b unaffected */
	-1,	/* 01c unaffected */
	-1,	/* 01d unaffected */
	-1,	/* 01e unaffected */
	-1,	/* 01f unaffected */
	-1,	/* 020 Japan */

	/* Type 5 layouts */
	KB_US,	/* 021 USA */
	KB_US,	/* 022 UNIX */
	KB_FR,	/* 023 France */
	KB_DK,	/* 024 Denmark */
	KB_DE,	/* 025 Germany */
	KB_IT,	/* 026 Italy */
	KB_NL,	/* 027 The Netherlands */
	KB_NO,	/* 028 Norway */
	KB_PT,	/* 029 Portugal */
	KB_ES,	/* 02a Spain */
	KB_SV,	/* 02b Sweden */
	KB_SF,	/* 02c Switzerland/French */
	KB_SG,	/* 02d Switzerland/German */
	KB_UK,	/* 02e Great Britain */
	-1,	/* 02f Korea */
	-1,	/* 030 Taiwan */
	KB_JP,	/* 031 Japan */
	-1,	/* 032 Canada/French */
	-1,	/* 033 Hungary */
	-1,	/* 034 Poland */
	-1,	/* 035 Czech */
	-1,	/* 036 Russia */
	-1,	/* 037 Latvia */
	-1,	/* 038 Turkey-Q5 */
	-1,	/* 039 Greece */
	-1,	/* 03a Arabic */
	-1,	/* 03b Lithuania */
	-1,	/* 03c Belgium */
	-1,	/* 03d unaffected */
	-1,	/* 03e Turkey-F5 */
	-1,	/* 03f Canada/French */

	/* Not affected range */
	-1,	/* 040 */
	-1,	/* 041 */
	-1,	/* 042 */
	-1,	/* 043 */
	-1,	/* 044 */
	-1,	/* 045 */
	-1,	/* 046 */
	-1,	/* 047 */
	-1,	/* 048 */
	-1,	/* 049 */
	-1,	/* 04a */
	-1,	/* 04b */
	-1,	/* 04c */
	-1,	/* 04d */
	-1,	/* 04e */
	-1,	/* 04f */

	/* ``Compact-1'' layouts */
	KB_US,	/* 050 USA */
	KB_US,	/* 051 UNIX */
	KB_FR,	/* 052 France */
	KB_DK,	/* 053 Denmark */
	KB_DE,	/* 054 Germany */
	KB_IT,	/* 055 Italy */
	KB_NL,	/* 056 The Netherlands */
	KB_NO,	/* 057 Norway */
	KB_PT,	/* 058 Portugal */
	KB_ES,	/* 059 Spain */
	KB_SV,	/* 05a Sweden */
	KB_SF,	/* 05b Switzerland/French */
	KB_SG,	/* 05c Switzerland/German */
	KB_UK,	/* 05d Great Britain */
	-1,	/* 05e Korea */
	-1,	/* 05f Taiwan */
	KB_JP,	/* 060 Japan */
	-1,	/* 061 Canada/French */
};

struct wskbd_mapdata sunkbd_keymapdata = {
	sunkbd_keydesctab,
#ifdef SUNKBD_LAYOUT
	SUNKBD_LAYOUT,
#else
	KB_US | KB_DEFAULT,
#endif
};

struct wskbd_mapdata sunkbd5_keymapdata = {
	sunkbd5_keydesctab,
#ifdef SUNKBD5_LAYOUT
	SUNKBD5_LAYOUT,
#else
	KB_US | KB_DEFAULT,
#endif
};
