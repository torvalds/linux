/*	$OpenBSD: wskbdraw.h,v 1.4 2023/07/24 19:28:40 miod Exp $	*/

/*
 * Copyright (c) 2005, Miodrag Vallat
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
 */

/*
 * US keyboard XT scancodes
 */

#define	RAWKEY_Null			0x00

/*
 * These names match KS_xxx symbols whenever possible
 */

#define	RAWKEY_Escape			0x01
#define	RAWKEY_1			0x02
#define	RAWKEY_2			0x03
#define	RAWKEY_3			0x04
#define	RAWKEY_4			0x05
#define	RAWKEY_5			0x06
#define	RAWKEY_6			0x07
#define	RAWKEY_7			0x08
#define	RAWKEY_8			0x09
#define	RAWKEY_9			0x0a
#define	RAWKEY_0			0x0b
#define	RAWKEY_minus			0x0c
#define	RAWKEY_equal			0x0d
#define	RAWKEY_Tab			0x0f
#define	RAWKEY_q			0x10
#define	RAWKEY_w			0x11
#define	RAWKEY_e			0x12
#define	RAWKEY_r			0x13
#define	RAWKEY_t			0x14
#define	RAWKEY_y			0x15
#define	RAWKEY_u			0x16
#define	RAWKEY_i			0x17
#define	RAWKEY_o			0x18
#define	RAWKEY_p			0x19
#define	RAWKEY_bracketleft		0x1a
#define	RAWKEY_bracketright		0x1b
#define	RAWKEY_Return			0x1c
#define	RAWKEY_Control_L		0x1d
#define	RAWKEY_a			0x1e
#define	RAWKEY_s			0x1f
#define	RAWKEY_d			0x20
#define	RAWKEY_f			0x21
#define	RAWKEY_g			0x22
#define	RAWKEY_h			0x23
#define	RAWKEY_j			0x24
#define	RAWKEY_k			0x25
#define	RAWKEY_l			0x26
#define	RAWKEY_semicolon		0x27
#define	RAWKEY_apostrophe		0x28
#define	RAWKEY_grave			0x29
#define	RAWKEY_Shift_L			0x2a
#define	RAWKEY_backslash		0x2b
#define	RAWKEY_z			0x2c
#define	RAWKEY_x			0x2d
#define	RAWKEY_c			0x2e
#define	RAWKEY_v			0x2f
#define	RAWKEY_b			0x30
#define	RAWKEY_n			0x31
#define	RAWKEY_m			0x32
#define	RAWKEY_comma			0x33
#define	RAWKEY_period			0x34
#define	RAWKEY_slash			0x35
#define	RAWKEY_Shift_R			0x36
#define	RAWKEY_KP_Multiply		0x37
#define	RAWKEY_Alt_L			0x38
#define	RAWKEY_space			0x39
#define	RAWKEY_Caps_Lock		0x3a
#define	RAWKEY_f1			0x3b
#define	RAWKEY_f2			0x3c
#define	RAWKEY_f3			0x3d
#define	RAWKEY_f4			0x3e
#define	RAWKEY_f5			0x3f
#define	RAWKEY_f6			0x40
#define	RAWKEY_f7			0x41
#define	RAWKEY_f8			0x42
#define	RAWKEY_f9			0x43
#define	RAWKEY_f10			0x44
#define	RAWKEY_Num_Lock			0x45
#define	RAWKEY_Hold_Screen		0x46	/* Scroll Lock */
#define	RAWKEY_KP_Home			0x47
#define	RAWKEY_KP_Up			0x48
#define	RAWKEY_KP_Prior			0x49
#define	RAWKEY_KP_Subtract		0x4a
#define	RAWKEY_KP_Left			0x4b
#define	RAWKEY_KP_Begin			0x4c
#define	RAWKEY_KP_Right			0x4d
#define	RAWKEY_KP_Add			0x4e
#define	RAWKEY_KP_End			0x4f
#define	RAWKEY_KP_Down			0x50
#define	RAWKEY_KP_Next			0x51
#define	RAWKEY_KP_Insert		0x52
#define	RAWKEY_KP_Delete		0x53
#define	RAWKEY_less			0x56	/* < > on European keyboards */
#define	RAWKEY_f11			0x57
#define	RAWKEY_f12			0x58
#define	RAWKEY_Pause			0x6a
#define	RAWKEY_Meta_L			0x73
#define	RAWKEY_Meta_R			0x74
#define	RAWKEY_KP_Equal			0x76
#define	RAWKEY_KP_Enter			0x9c
#define	RAWKEY_Control_R		0x9d
#define	RAWKEY_KP_Divide		0xb5
#define	RAWKEY_Print_Screen		0xb7
#define	RAWKEY_Alt_R			0xb8
#define	RAWKEY_Home			0xc7
#define	RAWKEY_Up			0xc8
#define	RAWKEY_Prior			0xc9
#define	RAWKEY_Left			0xcb
#define	RAWKEY_Right			0xcd
#define	RAWKEY_End			0xcf
#define	RAWKEY_Down			0xd0
#define	RAWKEY_Next			0xd1
#define	RAWKEY_Insert			0xd2
#define	RAWKEY_Delete			0xd3

#define	RAWKEY_Begin			0x5d
#define	RAWKEY_Menu			0x6d
#define	RAWKEY_Compose			0x72

/*
 * The following keys have no KS_xxx equivalents
 */

#define	RAWKEY_BackSpace		0x0e
#define	RAWKEY_SysReq			0x54

#define	RAWKEY_Power			0x84
#define	RAWKEY_AudioMute		0x85
#define	RAWKEY_AudioLower		0x86
#define	RAWKEY_AudioRaise		0x87
#define	RAWKEY_Help			0x88
#define	RAWKEY_L1			0x89	/* Stop */
#define	RAWKEY_L2			0x8a	/* Again */
#define	RAWKEY_L3			0x8b	/* Props */
#define	RAWKEY_L4			0x8c	/* Undo */
#define	RAWKEY_L5			0x8d	/* Front */
#define	RAWKEY_L6			0x8e	/* Copy */
#define	RAWKEY_L7			0x8f	/* Open */
#define	RAWKEY_L8			0x90	/* Paste */
#define	RAWKEY_L9			0x91	/* Find */
#define	RAWKEY_L10			0x92	/* Cut */
