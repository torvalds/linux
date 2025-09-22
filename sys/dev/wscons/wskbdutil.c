/*	$OpenBSD: wskbdutil.c,v 1.19 2021/12/30 06:55:11 anton Exp $	*/
/*	$NetBSD: wskbdutil.c,v 1.7 1999/12/21 11:59:13 drochner Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Juergen Hannken-Illjes.
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

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>

static struct compose_tab_s {
	keysym_t elem[2];
	keysym_t result;
} compose_tab[] = {
	{ { KS_plus,			KS_plus },		KS_numbersign },
	{ { KS_a,			KS_a },			KS_at },
	{ { KS_parenleft,		KS_parenleft },		KS_bracketleft },
	{ { KS_slash,			KS_slash },		KS_backslash },
	{ { KS_parenright,		KS_parenright },	KS_bracketright },
	{ { KS_parenleft,		KS_minus },		KS_braceleft },
	{ { KS_slash,			KS_minus },		KS_bar },
	{ { KS_parenright,		KS_minus },		KS_braceright },
	{ { KS_exclam,			KS_exclam },		KS_exclamdown },
	{ { KS_c,			KS_slash },		KS_cent },
	{ { KS_l,			KS_minus },		KS_sterling },
	{ { KS_y,			KS_minus },		KS_yen },
	{ { KS_s,			KS_o },			KS_section },
	{ { KS_x,			KS_o },			KS_currency },
	{ { KS_c,			KS_o },			KS_copyright },
	{ { KS_less,			KS_less },		KS_guillemotleft },
	{ { KS_greater,			KS_greater },		KS_guillemotright },
	{ { KS_question,		KS_question },		KS_questiondown },
	{ { KS_dead_acute,		KS_space },		KS_apostrophe },
	{ { KS_dead_grave,		KS_space },		KS_grave },
	{ { KS_dead_tilde,		KS_space },		KS_asciitilde },
	{ { KS_dead_circumflex,		KS_space },		KS_asciicircum },
	{ { KS_dead_diaeresis,		KS_space },		KS_quotedbl },
	{ { KS_dead_cedilla,		KS_space },		KS_comma },
	{ { KS_dead_circumflex,		KS_A },			KS_Acircumflex },
	{ { KS_dead_diaeresis,		KS_A },			KS_Adiaeresis },
	{ { KS_dead_grave,		KS_A },			KS_Agrave },
	{ { KS_dead_abovering,		KS_A },			KS_Aring },
	{ { KS_dead_tilde,		KS_A },			KS_Atilde },
	{ { KS_dead_cedilla,		KS_C },			KS_Ccedilla },
	{ { KS_dead_acute,		KS_E },			KS_Eacute },
	{ { KS_dead_circumflex,		KS_E },			KS_Ecircumflex },
	{ { KS_dead_diaeresis,		KS_E },			KS_Ediaeresis },
	{ { KS_dead_grave,		KS_E },			KS_Egrave },
	{ { KS_dead_acute,		KS_I },			KS_Iacute },
	{ { KS_dead_circumflex,		KS_I },			KS_Icircumflex },
	{ { KS_dead_diaeresis,		KS_I },			KS_Idiaeresis },
	{ { KS_dead_grave,		KS_I },			KS_Igrave },
	{ { KS_dead_tilde,		KS_N },			KS_Ntilde },
	{ { KS_dead_acute,		KS_O },			KS_Oacute },
	{ { KS_dead_circumflex,		KS_O },			KS_Ocircumflex },
	{ { KS_dead_diaeresis,		KS_O },			KS_Odiaeresis },
	{ { KS_dead_grave,		KS_O },			KS_Ograve },
	{ { KS_dead_tilde,		KS_O },			KS_Otilde },
	{ { KS_dead_acute,		KS_U },			KS_Uacute },
	{ { KS_dead_circumflex,		KS_U },			KS_Ucircumflex },
	{ { KS_dead_diaeresis,		KS_U },			KS_Udiaeresis },
	{ { KS_dead_grave,		KS_U },			KS_Ugrave },
	{ { KS_dead_acute,		KS_Y },			KS_Yacute },
	{ { KS_dead_acute,		KS_a },			KS_aacute },
	{ { KS_dead_circumflex,		KS_a },			KS_acircumflex },
	{ { KS_dead_diaeresis,		KS_a },			KS_adiaeresis },
	{ { KS_dead_grave,		KS_a },			KS_agrave },
	{ { KS_dead_abovering,		KS_a },			KS_aring },
	{ { KS_dead_tilde,		KS_a },			KS_atilde },
	{ { KS_dead_cedilla,		KS_c },			KS_ccedilla },
	{ { KS_dead_acute,		KS_e },			KS_eacute },
	{ { KS_dead_circumflex,		KS_e },			KS_ecircumflex },
	{ { KS_dead_diaeresis,		KS_e },			KS_ediaeresis },
	{ { KS_dead_grave,		KS_e },			KS_egrave },
	{ { KS_dead_acute,		KS_i },			KS_iacute },
	{ { KS_dead_circumflex,		KS_i },			KS_icircumflex },
	{ { KS_dead_diaeresis,		KS_i },			KS_idiaeresis },
	{ { KS_dead_grave,		KS_i },			KS_igrave },
	{ { KS_dead_tilde,		KS_n },			KS_ntilde },
	{ { KS_dead_acute,		KS_o },			KS_oacute },
	{ { KS_dead_circumflex,		KS_o },			KS_ocircumflex },
	{ { KS_dead_diaeresis,		KS_o },			KS_odiaeresis },
	{ { KS_dead_grave,		KS_o },			KS_ograve },
	{ { KS_dead_tilde,		KS_o },			KS_otilde },
	{ { KS_dead_acute,		KS_u },			KS_uacute },
	{ { KS_dead_circumflex,		KS_u },			KS_ucircumflex },
	{ { KS_dead_diaeresis,		KS_u },			KS_udiaeresis },
	{ { KS_dead_grave,		KS_u },			KS_ugrave },
	{ { KS_dead_acute,		KS_y },			KS_yacute },
	{ { KS_dead_diaeresis,		KS_y },			KS_ydiaeresis },
	{ { KS_quotedbl,		KS_A },			KS_Adiaeresis },
	{ { KS_quotedbl,		KS_E },			KS_Ediaeresis },
	{ { KS_quotedbl,		KS_I },			KS_Idiaeresis },
	{ { KS_quotedbl,		KS_O },			KS_Odiaeresis },
	{ { KS_quotedbl,		KS_U },			KS_Udiaeresis },
	{ { KS_quotedbl,		KS_a },			KS_adiaeresis },
	{ { KS_quotedbl,		KS_e },			KS_ediaeresis },
	{ { KS_quotedbl,		KS_i },			KS_idiaeresis },
	{ { KS_quotedbl,		KS_o },			KS_odiaeresis },
	{ { KS_quotedbl,		KS_u },			KS_udiaeresis },
	{ { KS_quotedbl,		KS_y },			KS_ydiaeresis },
	{ { KS_acute,			KS_A },			KS_Aacute },
	{ { KS_asciicircum,		KS_A },			KS_Acircumflex },
	{ { KS_grave,			KS_A },			KS_Agrave },
	{ { KS_asterisk,		KS_A },			KS_Aring },
	{ { KS_asciitilde,		KS_A },			KS_Atilde },
	{ { KS_cedilla,			KS_C },			KS_Ccedilla },
	{ { KS_acute,			KS_E },			KS_Eacute },
	{ { KS_asciicircum,		KS_E },			KS_Ecircumflex },
	{ { KS_grave,			KS_E },			KS_Egrave },
	{ { KS_acute,			KS_I },			KS_Iacute },
	{ { KS_asciicircum,		KS_I },			KS_Icircumflex },
	{ { KS_grave,			KS_I },			KS_Igrave },
	{ { KS_asciitilde,		KS_N },			KS_Ntilde },
	{ { KS_acute,			KS_O },			KS_Oacute },
	{ { KS_asciicircum,		KS_O },			KS_Ocircumflex },
	{ { KS_grave,			KS_O },			KS_Ograve },
	{ { KS_asciitilde,		KS_O },			KS_Otilde },
	{ { KS_acute,			KS_U },			KS_Uacute },
	{ { KS_asciicircum,		KS_U },			KS_Ucircumflex },
	{ { KS_grave,			KS_U },			KS_Ugrave },
	{ { KS_acute,			KS_Y },			KS_Yacute },
	{ { KS_acute,			KS_a },			KS_aacute },
	{ { KS_asciicircum,		KS_a },			KS_acircumflex },
	{ { KS_grave,			KS_a },			KS_agrave },
	{ { KS_asterisk,		KS_a },			KS_aring },
	{ { KS_asciitilde,		KS_a },			KS_atilde },
	{ { KS_cedilla,			KS_c },			KS_ccedilla },
	{ { KS_acute,			KS_e },			KS_eacute },
	{ { KS_asciicircum,		KS_e },			KS_ecircumflex },
	{ { KS_grave,			KS_e },			KS_egrave },
	{ { KS_acute,			KS_i },			KS_iacute },
	{ { KS_asciicircum,		KS_i },			KS_icircumflex },
	{ { KS_grave,			KS_i },			KS_igrave },
	{ { KS_asciitilde,		KS_n },			KS_ntilde },
	{ { KS_acute,			KS_o },			KS_oacute },
	{ { KS_asciicircum,		KS_o },			KS_ocircumflex },
	{ { KS_grave,			KS_o },			KS_ograve },
	{ { KS_asciitilde,		KS_o },			KS_otilde },
	{ { KS_acute,			KS_u },			KS_uacute },
	{ { KS_asciicircum,		KS_u },			KS_ucircumflex },
	{ { KS_grave,			KS_u },			KS_ugrave },
	{ { KS_acute,			KS_y },			KS_yacute },
	{ { KS_dead_caron,		KS_space },		KS_L2_caron },
	{ { KS_dead_caron,		KS_S },			KS_L2_Scaron },
	{ { KS_dead_caron,		KS_Z },			KS_L2_Zcaron },
	{ { KS_dead_caron,		KS_s },			KS_L2_scaron },
	{ { KS_dead_caron,		KS_z },			KS_L2_zcaron }
};

#define COMPOSE_SIZE	nitems(compose_tab)

static int compose_tab_inorder = 0;

keysym_t ksym_upcase(keysym_t);
void fillmapentry(const keysym_t *, int, struct wscons_keymap *);

static inline int
compose_tab_cmp(struct compose_tab_s *i, struct compose_tab_s *j)
{
	if (i->elem[0] == j->elem[0])
		return(i->elem[1] - j->elem[1]);
	else
		return(i->elem[0] - j->elem[0]);
}

keysym_t
wskbd_compose_value(keysym_t *compose_buf)
{
	int i, j, r;
	struct compose_tab_s v;

	if (!compose_tab_inorder) {
		/* Insertion sort. */
		for (i = 1; i < COMPOSE_SIZE; i++) {
			v = compose_tab[i];
			/* find correct slot, moving others up */
			for (j = i; --j >= 0 &&
			    compose_tab_cmp(&v, &compose_tab[j]) < 0;)
				compose_tab[j + 1] = compose_tab[j];
			compose_tab[j + 1] = v;
		}
		compose_tab_inorder = 1;
	}

	for (j = 0, i = COMPOSE_SIZE; i != 0; i /= 2) {
		if (compose_tab[j + i/2].elem[0] == compose_buf[0]) {
			if (compose_tab[j + i/2].elem[1] == compose_buf[1])
				return(compose_tab[j + i/2].result);
			r = compose_tab[j + i/2].elem[1] < compose_buf[1];
		} else
			r = compose_tab[j + i/2].elem[0] < compose_buf[0];
		if (r) {
			j += i/2 + 1;
			i--;
		}
	}

	return(KS_voidSymbol);
}

static const u_char latin1_to_upper[256] = {
/*      0  8  1  9  2  a  3  b  4  c  5  d  6  e  7  f               */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 0 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 1 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 1 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 2 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 2 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 3 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 3 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 4 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 4 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 5 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 5 */
	0x00,  'A',  'B',  'C',  'D',  'E',  'F',  'G',		/* 6 */
	 'H',  'I',  'J',  'K',  'L',  'M',  'N',  'O',		/* 6 */
	 'P',  'Q',  'R',  'S',  'T',  'U',  'V',  'W',		/* 7 */
	 'X',  'Y',  'Z', 0x00, 0x00, 0x00, 0x00, 0x00,		/* 7 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 8 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 8 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 9 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* 9 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* a */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* a */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* b */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* b */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* c */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* c */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* d */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		/* d */
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,		/* e */
	0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,		/* e */
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0x00,		/* f */
	0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0x00,		/* f */
};

keysym_t
ksym_upcase(keysym_t ksym)
{
	if (ksym >= KS_f1 && ksym <= KS_f20)
		return(KS_F1 - KS_f1 + ksym);

	if (KS_GROUP(ksym) == KS_GROUP_Ascii && ksym <= 0xff &&
	    latin1_to_upper[ksym] != 0x00)
		return(latin1_to_upper[ksym]);

	return(ksym);
}

void
fillmapentry(const keysym_t *kp, int len, struct wscons_keymap *mapentry)
{
	switch (len) {
	case 0:
		mapentry->group1[0] = KS_voidSymbol;
		mapentry->group1[1] = KS_voidSymbol;
		mapentry->group2[0] = KS_voidSymbol;
		mapentry->group2[1] = KS_voidSymbol;
		break;

	case 1:
		mapentry->group1[0] = kp[0];
		mapentry->group1[1] = ksym_upcase(kp[0]);
		mapentry->group2[0] = mapentry->group1[0];
		mapentry->group2[1] = mapentry->group1[1];
		break;

	case 2:
		mapentry->group1[0] = kp[0];
		mapentry->group1[1] = kp[1];
		mapentry->group2[0] = mapentry->group1[0];
		mapentry->group2[1] = mapentry->group1[1];
		break;

	case 3:
		mapentry->group1[0] = kp[0];
		mapentry->group1[1] = kp[1];
		mapentry->group2[0] = kp[2];
		mapentry->group2[1] = ksym_upcase(kp[2]);
		break;

	case 4:
		mapentry->group1[0] = kp[0];
		mapentry->group1[1] = kp[1];
		mapentry->group2[0] = kp[2];
		mapentry->group2[1] = kp[3];
		break;

	}
}

void
wskbd_get_mapentry(const struct wskbd_mapdata *mapdata, int kc,
    struct wscons_keymap *mapentry)
{
	kbd_t cur;
	const keysym_t *kp;
	const struct wscons_keydesc *mp;
	int l;
	keysym_t ksg;

	mapentry->command = KS_voidSymbol;
	mapentry->group1[0] = KS_voidSymbol;
	mapentry->group1[1] = KS_voidSymbol;
	mapentry->group2[0] = KS_voidSymbol;
	mapentry->group2[1] = KS_voidSymbol;

	for (cur = mapdata->layout & ~KB_HANDLEDBYWSKBD; cur != 0; ) {
		mp = mapdata->keydesc;
		while (mp->map_size > 0) {
			if (mp->name == cur)
				break;
			mp++;
		}

		/* If map not found, return */
		if (mp->map_size <= 0)
			return;

		for (kp = mp->map; kp < mp->map + mp->map_size; kp++) {
			ksg = KS_GROUP(*kp);
			if (ksg == KS_GROUP_Keycode &&
			    KS_VALUE(*kp) == kc) {
				/* First skip keycode and possible command */
				kp++;
				if (KS_GROUP(*kp) == KS_GROUP_Command ||
				    *kp == KS_Cmd || *kp == KS_Cmd1 || *kp == KS_Cmd2)
					mapentry->command = *kp++;

				for (l = 0; kp + l < mp->map + mp->map_size;
				    l++) {
					ksg = KS_GROUP(kp[l]);
					if (ksg == KS_GROUP_Keycode)
						break;
				}
				if (l > 4)
					panic("wskbd_get_mapentry: %d(%d): bad entry",
					      mp->name, *kp);
				fillmapentry(kp, l, mapentry);
				return;
			}
		}

		cur = mp->base;
	}
}

struct wscons_keymap *
wskbd_init_keymap(int maplen)
{
	struct wscons_keymap *map;
	int i;

	map = mallocarray(maplen, sizeof(*map), M_DEVBUF, M_WAITOK);
	for (i = 0; i < maplen; i++) {
		map[i].command = KS_voidSymbol;
		map[i].group1[0] = KS_voidSymbol;
		map[i].group1[1] = KS_voidSymbol;
		map[i].group2[0] = KS_voidSymbol;
		map[i].group2[1] = KS_voidSymbol;
	}
	return map;
}

int
wskbd_load_keymap(const struct wskbd_mapdata *mapdata, kbd_t layout,
    struct wscons_keymap **map, int *maplen)
{
	int i, s, kc, stack_ptr;
	const keysym_t *kp;
	const struct wscons_keydesc *mp, *stack[10];
	kbd_t cur;
	keysym_t ksg;

	for (cur = layout & ~KB_HANDLEDBYWSKBD, stack_ptr = 0;
	     cur != 0; stack_ptr++) {
		mp = mapdata->keydesc;
		while (mp->map_size > 0) {
			if (cur == 0 || mp->name == cur) {
				break;
			}
			mp++;
		}

		if (stack_ptr == nitems(stack))
			panic("wskbd_load_keymap: %d: recursion too deep",
			      mapdata->layout);
		if (mp->map_size <= 0)
			return(EINVAL);

		stack[stack_ptr] = mp;
		cur = mp->base;
	}

	for (i = 0, s = stack_ptr - 1; s >= 0; s--) {
		mp = stack[s];
		for (kp = mp->map; kp < mp->map + mp->map_size; kp++) {
			ksg = KS_GROUP(*kp);
			if (ksg == KS_GROUP_Keycode && KS_VALUE(*kp) > i)
				i = KS_VALUE(*kp);
		}
	}

	*map = wskbd_init_keymap(i + 1);
	*maplen = i + 1;

	for (s = stack_ptr - 1; s >= 0; s--) {
		mp = stack[s];
		for (kp = mp->map; kp < mp->map + mp->map_size; ) {
			ksg = KS_GROUP(*kp);
			if (ksg != KS_GROUP_Keycode)
				panic("wskbd_load_keymap: %d(%d): bad entry",
				      mp->name, *kp);

			kc = KS_VALUE(*kp);
			kp++;

			if (KS_GROUP(*kp) == KS_GROUP_Command ||
			    *kp == KS_Cmd || *kp == KS_Cmd1 || *kp == KS_Cmd2) {
				(*map)[kc].command = *kp;
				kp++;
			}

			for (i = 0; kp + i < mp->map + mp->map_size; i++) {
				ksg = KS_GROUP(kp[i]);
				if (ksg == KS_GROUP_Keycode)
					break;
			}

			if (i > 4)
				panic("wskbd_load_keymap: %d(%d): bad entry",
				      mp->name, *kp);

			fillmapentry(kp, i, &(*map)[kc]);
			kp += i;
		}
	}

	return(0);
}
