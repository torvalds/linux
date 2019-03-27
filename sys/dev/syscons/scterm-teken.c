/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Copyright (c) 2008-2009 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_syscons.h"
#include "opt_teken.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/consio.h>
#include <sys/kbio.h>

#if defined(__arm__) || defined(__mips__) || \
	defined(__powerpc__) || defined(__sparc64__)
#include <machine/sc_machdep.h>
#else
#include <machine/pc/display.h>
#endif

#include <dev/syscons/syscons.h>

#include <teken/teken.h>

static void scteken_sc_to_te_attr(unsigned char, teken_attr_t *);
static int scteken_te_to_sc_attr(const teken_attr_t *);

static sc_term_init_t		scteken_init;
static sc_term_term_t		scteken_term;
static sc_term_puts_t		scteken_puts;
static sc_term_ioctl_t		scteken_ioctl;
static sc_term_default_attr_t	scteken_default_attr;
static sc_term_clear_t		scteken_clear;
static sc_term_input_t		scteken_input;
static sc_term_fkeystr_t	scteken_fkeystr;
static sc_term_sync_t		scteken_sync;
static void			scteken_nop(void);

typedef struct {
	teken_t		ts_teken;
	int		ts_busy;
} teken_stat;

static teken_stat	reserved_teken_stat;

static void scteken_sync_internal(scr_stat *, teken_stat *);

static sc_term_sw_t sc_term_scteken = {
	{ NULL, NULL },
	"scteken",			/* emulator name */
	"teken terminal",		/* description */
	"*",				/* matching renderer */
	sizeof(teken_stat),		/* softc size */
	0,
	scteken_init,
	scteken_term,
	scteken_puts,
	scteken_ioctl,
	(sc_term_reset_t *)scteken_nop,
	scteken_default_attr,
	scteken_clear,
	(sc_term_notify_t *)scteken_nop,
	scteken_input,
	scteken_fkeystr,
	scteken_sync,
};

SCTERM_MODULE(scteken, sc_term_scteken);

static tf_bell_t	scteken_bell;
static tf_cursor_t	scteken_cursor;
static tf_putchar_t	scteken_putchar;
static tf_fill_t	scteken_fill;
static tf_copy_t	scteken_copy;
static tf_param_t	scteken_param;
static tf_respond_t	scteken_respond;

static const teken_funcs_t scteken_funcs = {
	.tf_bell	= scteken_bell,
	.tf_cursor	= scteken_cursor,
	.tf_putchar	= scteken_putchar,
	.tf_fill	= scteken_fill,
	.tf_copy	= scteken_copy,
	.tf_param	= scteken_param,
	.tf_respond	= scteken_respond,
};

static int
scteken_init(scr_stat *scp, void **softc, int code)
{
	teken_stat *ts;
	teken_attr_t ta;

	if (*softc == NULL) {
		if (reserved_teken_stat.ts_busy)
			return (EINVAL);
		*softc = &reserved_teken_stat;
	}
	ts = *softc;

	switch (code) {
	case SC_TE_COLD_INIT:
		++sc_term_scteken.te_refcount;
		ts->ts_busy = 1;
		/* FALLTHROUGH */
	case SC_TE_WARM_INIT:
		ta = *teken_get_defattr(&ts->ts_teken);
		teken_init(&ts->ts_teken, &scteken_funcs, scp);
		teken_set_defattr(&ts->ts_teken, &ta);
#ifndef TEKEN_UTF8
		teken_set_8bit(&ts->ts_teken);
#endif /* !TEKEN_UTF8 */
#ifdef TEKEN_CONS25
		teken_set_cons25(&ts->ts_teken);
#endif /* TEKEN_CONS25 */
		teken_set_cons25keys(&ts->ts_teken);
		scteken_sync_internal(scp, ts);
		break;
	}

	return (0);
}

static int
scteken_term(scr_stat *scp, void **softc)
{

	if (*softc == &reserved_teken_stat) {
		*softc = NULL;
		reserved_teken_stat.ts_busy = 0;
	}
	--sc_term_scteken.te_refcount;

	return (0);
}

static void
scteken_puts(scr_stat *scp, u_char *buf, int len)
{
	teken_stat *ts = scp->ts;

	scp->sc->write_in_progress++;
	teken_input(&ts->ts_teken, buf, len);
	scp->sc->write_in_progress--;
}

static int
scteken_ioctl(scr_stat *scp, struct tty *tp, u_long cmd, caddr_t data,
	     struct thread *td)
{
	teken_stat *ts = scp->ts;
	vid_info_t *vi;
	int attr;

	switch (cmd) {
	case GIO_ATTR:      	/* get current attributes */
		*(int*)data =
		    scteken_te_to_sc_attr(teken_get_curattr(&ts->ts_teken));
		return (0);
	case CONS_GETINFO:  	/* get current (virtual) console info */
		vi = (vid_info_t *)data;
		if (vi->size != sizeof(struct vid_info))
			return EINVAL;

		attr = scteken_te_to_sc_attr(teken_get_defattr(&ts->ts_teken));
		vi->mv_norm.fore = attr & 0x0f;
		vi->mv_norm.back = (attr >> 4) & 0x0f;
		vi->mv_rev.fore = vi->mv_norm.back;
		vi->mv_rev.back = vi->mv_norm.fore;
		/*
		 * The other fields are filled by the upper routine. XXX
		 */
		return (ENOIOCTL);
	}

	return (ENOIOCTL);
}

static void
scteken_default_attr(scr_stat *scp, int color, int rev_color)
{
	teken_stat *ts = scp->ts;
	teken_attr_t ta;

	scteken_sc_to_te_attr(color, &ta);
	teken_set_defattr(&ts->ts_teken, &ta);
}

static void
scteken_clear(scr_stat *scp)
{
	teken_stat *ts = scp->ts;

	sc_move_cursor(scp, 0, 0);
	scteken_sync_internal(scp, ts);
	sc_vtb_clear(&scp->vtb, scp->sc->scr_map[0x20],
		     scteken_te_to_sc_attr(teken_get_curattr(&ts->ts_teken))
		     << 8);
	mark_all(scp);
}

static int
scteken_input(scr_stat *scp, int c, struct tty *tp)
{

	return FALSE;
}

static const char *
scteken_fkeystr(scr_stat *scp, int c)
{
	teken_stat *ts = scp->ts;
	unsigned int k;

	switch (c) {
	case FKEY | F(1):  case FKEY | F(2):  case FKEY | F(3):
	case FKEY | F(4):  case FKEY | F(5):  case FKEY | F(6):
	case FKEY | F(7):  case FKEY | F(8):  case FKEY | F(9):
	case FKEY | F(10): case FKEY | F(11): case FKEY | F(12):
		k = TKEY_F1 + c - (FKEY | F(1));
		break;
	case FKEY | F(49):
		k = TKEY_HOME;
		break;
	case FKEY | F(50):
		k = TKEY_UP;
		break;
	case FKEY | F(51):
		k = TKEY_PAGE_UP;
		break;
	case FKEY | F(53):
		k = TKEY_LEFT;
		break;
	case FKEY | F(55):
		k = TKEY_RIGHT;
		break;
	case FKEY | F(57):
		k = TKEY_END;
		break;
	case FKEY | F(58):
		k = TKEY_DOWN;
		break;
	case FKEY | F(59):
		k = TKEY_PAGE_DOWN;
		break;
	case FKEY | F(60):
		k = TKEY_INSERT;
		break;
	case FKEY | F(61):
		k = TKEY_DELETE;
		break;
	default:
		return (NULL);
	}

	return (teken_get_sequence(&ts->ts_teken, k));
}

static void
scteken_sync_internal(scr_stat *scp, teken_stat *ts)
{
	teken_pos_t tp;

	tp.tp_col = scp->xsize;
	tp.tp_row = scp->ysize;
	teken_set_winsize_noreset(&ts->ts_teken, &tp);
	tp.tp_col = scp->xpos;
	tp.tp_row = scp->ypos;
	teken_set_cursor(&ts->ts_teken, &tp);
}

static void
scteken_sync(scr_stat *scp)
{
	scteken_sync_internal(scp, scp->ts);
}

static void
scteken_nop(void)
{

}

/*
 * libteken routines.
 */

static const teken_color_t sc_to_te_color[] = {
	TC_BLACK,     TC_BLUE,         TC_GREEN,     TC_CYAN,
	TC_RED,       TC_MAGENTA,      TC_BROWN,     TC_WHITE,
};

static const unsigned char te_to_sc_color[] = {
	FG_BLACK,     FG_RED,          FG_GREEN,     FG_BROWN,
	FG_BLUE,      FG_MAGENTA,      FG_CYAN,      FG_LIGHTGREY,
};

static void
scteken_sc_to_te_attr(unsigned char color, teken_attr_t *a)
{

	/*
	 * Conversions of attrs are not reversible.  Since sc attrs are
	 * pure colors in the simplest mode (16-color graphics) and the
	 * API is too deficient to tell us the mode, always convert to
	 * pure colors.  The conversion is essentially the identity except
	 * for reordering the non-brightness bits in the 2 color numbers.
	 */
	a->ta_format = 0;
	a->ta_fgcolor = sc_to_te_color[color & 7] | (color & 8);
	a->ta_bgcolor = sc_to_te_color[(color >> 4) & 7] | ((color >> 4) & 8);
}

static int
scteken_te_to_sc_attr(const teken_attr_t *a)
{
	int attr;
	teken_color_t fg, bg;

	if (a->ta_format & TF_REVERSE) {
		fg = a->ta_bgcolor;
		bg = a->ta_fgcolor;
	} else {
		fg = a->ta_fgcolor;
		bg = a->ta_bgcolor;
	}
	if (fg >= 16)
		fg = teken_256to16(fg);
	if (bg >= 16)
		bg = teken_256to16(bg);
	attr = te_to_sc_color[fg & 7] | (fg & 8) |
	    ((te_to_sc_color[bg & 7] | (bg & 8)) << 4);

	/* XXX: underline mapping for Hercules adapter can be better. */
	if (a->ta_format & (TF_BOLD | TF_UNDERLINE))
		attr ^= 8;
	if (a->ta_format & TF_BLINK)
		attr ^= 0x80;

	return (attr);
}

static void
scteken_bell(void *arg)
{
	scr_stat *scp = arg;

	sc_bell(scp, scp->bell_pitch, scp->bell_duration);
}

static void
scteken_cursor(void *arg, const teken_pos_t *p)
{
	scr_stat *scp = arg;

	sc_move_cursor(scp, p->tp_col, p->tp_row);
}

#ifdef TEKEN_UTF8
struct unicp437 {
	uint16_t	unicode_base;
	uint8_t		cp437_base;
	uint8_t		length;
};

static const struct unicp437 cp437table[] = {
	{ 0x0020, 0x20, 0x5e }, { 0x00a0, 0x20, 0x00 },
	{ 0x00a1, 0xad, 0x00 }, { 0x00a2, 0x9b, 0x00 },
	{ 0x00a3, 0x9c, 0x00 }, { 0x00a5, 0x9d, 0x00 },
	{ 0x00a7, 0x15, 0x00 }, { 0x00aa, 0xa6, 0x00 },
	{ 0x00ab, 0xae, 0x00 }, { 0x00ac, 0xaa, 0x00 },
	{ 0x00b0, 0xf8, 0x00 }, { 0x00b1, 0xf1, 0x00 },
	{ 0x00b2, 0xfd, 0x00 }, { 0x00b5, 0xe6, 0x00 },
	{ 0x00b6, 0x14, 0x00 }, { 0x00b7, 0xfa, 0x00 },
	{ 0x00ba, 0xa7, 0x00 }, { 0x00bb, 0xaf, 0x00 },
	{ 0x00bc, 0xac, 0x00 }, { 0x00bd, 0xab, 0x00 },
	{ 0x00bf, 0xa8, 0x00 }, { 0x00c0, 0x41, 0x00 },
	{ 0x00c1, 0x41, 0x00 }, { 0x00c2, 0x41, 0x00 },
	{ 0x00c4, 0x8e, 0x01 }, { 0x00c6, 0x92, 0x00 }, 
	{ 0x00c7, 0x80, 0x00 }, { 0x00c8, 0x45, 0x00 },
	{ 0x00c9, 0x90, 0x00 }, { 0x00ca, 0x45, 0x00 },
	{ 0x00cb, 0x45, 0x00 }, { 0x00cc, 0x49, 0x00 },
	{ 0x00cd, 0x49, 0x00 }, { 0x00ce, 0x49, 0x00 },
	{ 0x00cf, 0x49, 0x00 }, { 0x00d1, 0xa5, 0x00 },
	{ 0x00d2, 0x4f, 0x00 }, { 0x00d3, 0x4f, 0x00 },
	{ 0x00d4, 0x4f, 0x00 }, { 0x00d6, 0x99, 0x00 }, 
	{ 0x00d9, 0x55, 0x00 }, { 0x00da, 0x55, 0x00 },
	{ 0x00db, 0x55, 0x00 }, { 0x00dc, 0x9a, 0x00 },
	{ 0x00df, 0xe1, 0x00 }, { 0x00e0, 0x85, 0x00 },
	{ 0x00e1, 0xa0, 0x00 }, { 0x00e2, 0x83, 0x00 },
	{ 0x00e4, 0x84, 0x00 }, { 0x00e5, 0x86, 0x00 },
	{ 0x00e6, 0x91, 0x00 }, { 0x00e7, 0x87, 0x00 },
	{ 0x00e8, 0x8a, 0x00 }, { 0x00e9, 0x82, 0x00 },
	{ 0x00ea, 0x88, 0x01 }, { 0x00ec, 0x8d, 0x00 },
	{ 0x00ed, 0xa1, 0x00 }, { 0x00ee, 0x8c, 0x00 },
	{ 0x00ef, 0x8b, 0x00 }, { 0x00f0, 0xeb, 0x00 },
	{ 0x00f1, 0xa4, 0x00 }, { 0x00f2, 0x95, 0x00 },
	{ 0x00f3, 0xa2, 0x00 }, { 0x00f4, 0x93, 0x00 },
	{ 0x00f6, 0x94, 0x00 }, { 0x00f7, 0xf6, 0x00 },
	{ 0x00f8, 0xed, 0x00 }, { 0x00f9, 0x97, 0x00 },
	{ 0x00fa, 0xa3, 0x00 }, { 0x00fb, 0x96, 0x00 },
	{ 0x00fc, 0x81, 0x00 }, { 0x00ff, 0x98, 0x00 },
	{ 0x013f, 0x4c, 0x00 }, { 0x0140, 0x6c, 0x00 },
	{ 0x0192, 0x9f, 0x00 }, { 0x0393, 0xe2, 0x00 },
	{ 0x0398, 0xe9, 0x00 }, { 0x03a3, 0xe4, 0x00 },
	{ 0x03a6, 0xe8, 0x00 }, { 0x03a9, 0xea, 0x00 },
	{ 0x03b1, 0xe0, 0x01 }, { 0x03b4, 0xeb, 0x00 },
	{ 0x03b5, 0xee, 0x00 }, { 0x03bc, 0xe6, 0x00 },
	{ 0x03c0, 0xe3, 0x00 }, { 0x03c3, 0xe5, 0x00 },
	{ 0x03c4, 0xe7, 0x00 }, { 0x03c6, 0xed, 0x00 },
	{ 0x03d5, 0xed, 0x00 }, { 0x2010, 0x2d, 0x00 },
	{ 0x2014, 0x2d, 0x00 }, { 0x2018, 0x60, 0x00 },
	{ 0x2019, 0x27, 0x00 }, { 0x201c, 0x22, 0x00 },
	{ 0x201d, 0x22, 0x00 }, { 0x2022, 0x07, 0x00 },
	{ 0x203c, 0x13, 0x00 }, { 0x207f, 0xfc, 0x00 },
	{ 0x20a7, 0x9e, 0x00 }, { 0x20ac, 0xee, 0x00 },
	{ 0x2126, 0xea, 0x00 }, { 0x2190, 0x1b, 0x00 },
	{ 0x2191, 0x18, 0x00 }, { 0x2192, 0x1a, 0x00 },
	{ 0x2193, 0x19, 0x00 }, { 0x2194, 0x1d, 0x00 },
	{ 0x2195, 0x12, 0x00 }, { 0x21a8, 0x17, 0x00 },
	{ 0x2202, 0xeb, 0x00 }, { 0x2208, 0xee, 0x00 },
	{ 0x2211, 0xe4, 0x00 }, { 0x2212, 0x2d, 0x00 },
	{ 0x2219, 0xf9, 0x00 }, { 0x221a, 0xfb, 0x00 },
	{ 0x221e, 0xec, 0x00 }, { 0x221f, 0x1c, 0x00 },
	{ 0x2229, 0xef, 0x00 }, { 0x2248, 0xf7, 0x00 },
	{ 0x2261, 0xf0, 0x00 }, { 0x2264, 0xf3, 0x00 },
	{ 0x2265, 0xf2, 0x00 }, { 0x2302, 0x7f, 0x00 },
	{ 0x2310, 0xa9, 0x00 }, { 0x2320, 0xf4, 0x00 },
	{ 0x2321, 0xf5, 0x00 }, { 0x2500, 0xc4, 0x00 },
	{ 0x2502, 0xb3, 0x00 }, { 0x250c, 0xda, 0x00 },
	{ 0x2510, 0xbf, 0x00 }, { 0x2514, 0xc0, 0x00 },
	{ 0x2518, 0xd9, 0x00 }, { 0x251c, 0xc3, 0x00 },
	{ 0x2524, 0xb4, 0x00 }, { 0x252c, 0xc2, 0x00 },
	{ 0x2534, 0xc1, 0x00 }, { 0x253c, 0xc5, 0x00 },
	{ 0x2550, 0xcd, 0x00 }, { 0x2551, 0xba, 0x00 },
	{ 0x2552, 0xd5, 0x00 }, { 0x2553, 0xd6, 0x00 },
	{ 0x2554, 0xc9, 0x00 }, { 0x2555, 0xb8, 0x00 },
	{ 0x2556, 0xb7, 0x00 }, { 0x2557, 0xbb, 0x00 },
	{ 0x2558, 0xd4, 0x00 }, { 0x2559, 0xd3, 0x00 },
	{ 0x255a, 0xc8, 0x00 }, { 0x255b, 0xbe, 0x00 },
	{ 0x255c, 0xbd, 0x00 }, { 0x255d, 0xbc, 0x00 },
	{ 0x255e, 0xc6, 0x01 }, { 0x2560, 0xcc, 0x00 },
	{ 0x2561, 0xb5, 0x00 }, { 0x2562, 0xb6, 0x00 },
	{ 0x2563, 0xb9, 0x00 }, { 0x2564, 0xd1, 0x01 },
	{ 0x2566, 0xcb, 0x00 }, { 0x2567, 0xcf, 0x00 },
	{ 0x2568, 0xd0, 0x00 }, { 0x2569, 0xca, 0x00 },
	{ 0x256a, 0xd8, 0x00 }, { 0x256b, 0xd7, 0x00 },
	{ 0x256c, 0xce, 0x00 }, { 0x2580, 0xdf, 0x00 },
	{ 0x2584, 0xdc, 0x00 }, { 0x2588, 0xdb, 0x00 },
	{ 0x258c, 0xdd, 0x00 }, { 0x2590, 0xde, 0x00 },
	{ 0x2591, 0xb0, 0x02 }, { 0x25a0, 0xfe, 0x00 },
	{ 0x25ac, 0x16, 0x00 }, 
	{ 0x25ae, 0xdb, 0x00 }, { 0x25b2, 0x1e, 0x00 },
	{ 0x25ba, 0x10, 0x00 }, { 0x25bc, 0x1f, 0x00 },
	{ 0x25c4, 0x11, 0x00 }, { 0x25cb, 0x09, 0x00 },
	{ 0x25d8, 0x08, 0x00 }, { 0x25d9, 0x0a, 0x00 },
	{ 0x263a, 0x01, 0x01 }, { 0x263c, 0x0f, 0x00 },
	{ 0x2640, 0x0c, 0x00 }, { 0x2642, 0x0b, 0x00 },
	{ 0x2660, 0x06, 0x00 }, { 0x2663, 0x05, 0x00 },
	{ 0x2665, 0x03, 0x01 }, { 0x266a, 0x0d, 0x01 },
};

static void
scteken_get_cp437(teken_char_t *c, int *attr)
{
	int min, mid, max;

	min = 0;
	max = (sizeof(cp437table) / sizeof(struct unicp437)) - 1;

	if (*c < cp437table[0].unicode_base ||
	    *c > cp437table[max].unicode_base + cp437table[max].length)
		goto bad;

	while (max >= min) {
		mid = (min + max) / 2;
		if (*c < cp437table[mid].unicode_base) {
			max = mid - 1;
		} else if (*c > cp437table[mid].unicode_base +
		    cp437table[mid].length) {
			min = mid + 1;
		} else {
			*c -= cp437table[mid].unicode_base;
			*c += cp437table[mid].cp437_base;
			return;
		}
	}
bad:
	/* Character not present in CP437. */
	*attr = (FG_RED|BG_BLACK) << 8;
	*c = '?';
}
#endif /* TEKEN_UTF8 */

static void
scteken_putchar(void *arg, const teken_pos_t *tp, teken_char_t c,
    const teken_attr_t *a)
{
	scr_stat *scp = arg;
	u_char *map;
	u_char ch;
	vm_offset_t p;
	int cursor, attr;

	/*
	 * No support for printing right hand sides for CJK fullwidth
	 * characters. Simply print a space and assume that the left
	 * hand side describes the entire character.
	 */
	attr = scteken_te_to_sc_attr(a) << 8;
	if (a->ta_format & TF_CJK_RIGHT)
		c = ' ';
#ifdef TEKEN_UTF8
	scteken_get_cp437(&c, &attr);
#endif /* TEKEN_UTF8 */
	ch = c;

	map = scp->sc->scr_map;

	cursor = tp->tp_row * scp->xsize + tp->tp_col;
	p = sc_vtb_pointer(&scp->vtb, cursor);
	sc_vtb_putchar(&scp->vtb, p, map[ch], attr);

	mark_for_update(scp, cursor);
	/*
	 * XXX: Why do we need this? Only marking `cursor' should be
	 * enough. Without this line, we get artifacts.
	 */
	mark_for_update(scp, imin(cursor + 1, scp->xsize * scp->ysize - 1));
}

static void
scteken_fill(void *arg, const teken_rect_t *r, teken_char_t c,
    const teken_attr_t *a)
{
	scr_stat *scp = arg;
	u_char *map;
	u_char ch;
	unsigned int width;
	int attr, row;

	attr = scteken_te_to_sc_attr(a) << 8;
#ifdef TEKEN_UTF8
	scteken_get_cp437(&c, &attr);
#endif /* TEKEN_UTF8 */
	ch = c;

	map = scp->sc->scr_map;

	if (r->tr_begin.tp_col == 0 && r->tr_end.tp_col == scp->xsize) {
		/* Single contiguous region to fill. */
		sc_vtb_erase(&scp->vtb, r->tr_begin.tp_row * scp->xsize,
		    (r->tr_end.tp_row - r->tr_begin.tp_row) * scp->xsize,
		    map[ch], attr);
	} else {
		/* Fill display line by line. */
		width = r->tr_end.tp_col - r->tr_begin.tp_col;

		for (row = r->tr_begin.tp_row; row < r->tr_end.tp_row; row++) {
			sc_vtb_erase(&scp->vtb, r->tr_begin.tp_row *
			    scp->xsize + r->tr_begin.tp_col,
			    width, map[ch], attr);
		}
	}

	/* Mark begin and end positions to be refreshed. */
	mark_for_update(scp,
	    r->tr_begin.tp_row * scp->xsize + r->tr_begin.tp_col);
	mark_for_update(scp,
	    (r->tr_end.tp_row - 1) * scp->xsize + (r->tr_end.tp_col - 1));
	sc_remove_cutmarking(scp);
}

static void
scteken_copy(void *arg, const teken_rect_t *r, const teken_pos_t *p)
{
	scr_stat *scp = arg;
	unsigned int width;
	int src, dst, end;

#ifndef SC_NO_HISTORY
	/*
	 * We count a line of input as history if we perform a copy of
	 * one whole line upward. In other words: if a line of text gets
	 * overwritten by a rectangle that's right below it.
	 */
	if (scp->history != NULL &&
	    r->tr_begin.tp_col == 0 && r->tr_end.tp_col == scp->xsize &&
	    r->tr_begin.tp_row == p->tp_row + 1) {
		sc_hist_save_one_line(scp, p->tp_row);
	}
#endif

	if (r->tr_begin.tp_col == 0 && r->tr_end.tp_col == scp->xsize) {
		/* Single contiguous region to copy. */
		sc_vtb_move(&scp->vtb, r->tr_begin.tp_row * scp->xsize,
		    p->tp_row * scp->xsize,
		    (r->tr_end.tp_row - r->tr_begin.tp_row) * scp->xsize);
	} else {
		/* Copy line by line. */
		width = r->tr_end.tp_col - r->tr_begin.tp_col;

		if (p->tp_row < r->tr_begin.tp_row) {
			/* Copy from top to bottom. */
			src = r->tr_begin.tp_row * scp->xsize +
			    r->tr_begin.tp_col;
			end = r->tr_end.tp_row * scp->xsize +
			    r->tr_end.tp_col;
			dst = p->tp_row * scp->xsize + p->tp_col;

			while (src < end) {
				sc_vtb_move(&scp->vtb, src, dst, width);

				src += scp->xsize;
				dst += scp->xsize;
			}
		} else {
			/* Copy from bottom to top. */
			src = (r->tr_end.tp_row - 1) * scp->xsize +
			    r->tr_begin.tp_col;
			end = r->tr_begin.tp_row * scp->xsize +
			    r->tr_begin.tp_col;
			dst = (p->tp_row + r->tr_end.tp_row -
			    r->tr_begin.tp_row - 1) * scp->xsize + p->tp_col;

			while (src >= end) {
				sc_vtb_move(&scp->vtb, src, dst, width);

				src -= scp->xsize;
				dst -= scp->xsize;
			}
		}
	}

	/* Mark begin and end positions to be refreshed. */
	mark_for_update(scp,
	    p->tp_row * scp->xsize + p->tp_col);
	mark_for_update(scp,
	    (p->tp_row + r->tr_end.tp_row - r->tr_begin.tp_row - 1) *
	    scp->xsize +
	    (p->tp_col + r->tr_end.tp_col - r->tr_begin.tp_col - 1));
	sc_remove_cutmarking(scp);
}

static void
scteken_param(void *arg, int cmd, unsigned int value)
{
	static int cattrs[] = {
		0,					/* block */
		CONS_BLINK_CURSOR,			/* blinking block */
		CONS_CHAR_CURSOR,			/* underline */
		CONS_CHAR_CURSOR | CONS_BLINK_CURSOR,	/* blinking underline */
		CONS_RESET_CURSOR,			/* reset to default */
		CONS_HIDDEN_CURSOR,			/* hide cursor */
	};
	static int tcattrs[] = {
		CONS_RESET_CURSOR | CONS_LOCAL_CURSOR,	/* normal */
		CONS_HIDDEN_CURSOR | CONS_LOCAL_CURSOR,	/* invisible */
	};
	scr_stat *scp = arg;
	int flags, n, v0, v1, v2;

	switch (cmd) {
	case TP_SETBORDER:
		scp->border = value & 0xff;
		if (scp == scp->sc->cur_scp)
			sc_set_border(scp, scp->border);
		break;
	case TP_SETGLOBALCURSOR:
		n = value & 0xff;
		v0 = (value >> 8) & 0xff;
		v1 = (value >> 16) & 0xff;
		v2 = (value >> 24) & 0xff;
		switch (n) {
		case 1:	/* flags only */
			if (v0 < sizeof(cattrs) / sizeof(cattrs[0]))
				v0 = cattrs[v0];
			else /* backward compatibility */
				v0 = cattrs[v0 & 0x3];
			sc_change_cursor_shape(scp, v0, -1, -1);
			break;
		case 2:
			v2 = 0;
			v0 &= 0x1f;	/* backward compatibility */
			v1 &= 0x1f;
			/* FALL THROUGH */
		case 3:	/* base and height */
			if (v2 == 0)	/* count from top */
				sc_change_cursor_shape(scp, -1,
				    scp->font_size - v1 - 1,
				    v1 - v0 + 1);
			else if (v2 == 1) /* count from bottom */
				sc_change_cursor_shape(scp, -1,
				    v0, v1 - v0 + 1);
			break;
		}
		break;
	case TP_SETLOCALCURSOR:
		if (value < sizeof(tcattrs) / sizeof(tcattrs[0]))
			sc_change_cursor_shape(scp, tcattrs[value], -1, -1);
		else if (value == 2) {
			sc_change_cursor_shape(scp,
			    CONS_RESET_CURSOR | CONS_LOCAL_CURSOR, -1, -1);
			flags = scp->base_curs_attr.flags ^ CONS_BLINK_CURSOR;
			sc_change_cursor_shape(scp,
			    flags | CONS_LOCAL_CURSOR, -1, -1);
		}
		break;
	case TP_SHOWCURSOR:
		if (value != 0)
			flags = scp->base_curs_attr.flags & ~CONS_HIDDEN_CURSOR;
		else
			flags = scp->base_curs_attr.flags | CONS_HIDDEN_CURSOR;
		sc_change_cursor_shape(scp, flags | CONS_LOCAL_CURSOR, -1, -1);
		break;
	case TP_SWITCHVT:
		sc_switch_scr(scp->sc, value);
		break;
	case TP_SETBELLPD:
		scp->bell_pitch = TP_SETBELLPD_PITCH(value);
		scp->bell_duration = TP_SETBELLPD_DURATION(value);
		break;
	case TP_MOUSE:
		scp->mouse_level = value;
		break;
	}
}

static void
scteken_respond(void *arg, const void *buf, size_t len)
{
	scr_stat *scp = arg;

	sc_respond(scp, buf, len, 0);
}
