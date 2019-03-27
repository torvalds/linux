/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2009 Ed Schouten <ed@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
#if defined(__FreeBSD__) && defined(_KERNEL)
#include <sys/param.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/systm.h>
#define	teken_assert(x)		MPASS(x)
#else /* !(__FreeBSD__ && _KERNEL) */
#include <sys/types.h>
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define	teken_assert(x)		assert(x)
#endif /* __FreeBSD__ && _KERNEL */

/* debug messages */
#define	teken_printf(x,...)

/* Private flags for t_stateflags. */
#define	TS_FIRSTDIGIT	0x0001	/* First numeric digit in escape sequence. */
#define	TS_INSERT	0x0002	/* Insert mode. */
#define	TS_AUTOWRAP	0x0004	/* Autowrap. */
#define	TS_ORIGIN	0x0008	/* Origin mode. */
#define	TS_WRAPPED	0x0010	/* Next character should be printed on col 0. */
#define	TS_8BIT		0x0020	/* UTF-8 disabled. */
#define	TS_CONS25	0x0040	/* cons25 emulation. */
#define	TS_INSTRING	0x0080	/* Inside string. */
#define	TS_CURSORKEYS	0x0100	/* Cursor keys mode. */
#define	TS_CONS25KEYS	0x0400	/* Fuller cons25 emul (fix function keys). */

/* Character that blanks a cell. */
#define	BLANK	' '

#include "teken.h"
#include "teken_wcwidth.h"
#include "teken_scs.h"

static teken_state_t	teken_state_init;

/*
 * Wrappers for hooks.
 */

static inline void
teken_funcs_bell(const teken_t *t)
{

	teken_assert(t->t_funcs->tf_bell != NULL);
	t->t_funcs->tf_bell(t->t_softc);
}

static inline void
teken_funcs_cursor(const teken_t *t)
{

	teken_assert(t->t_cursor.tp_row < t->t_winsize.tp_row);
	teken_assert(t->t_cursor.tp_col < t->t_winsize.tp_col);

	teken_assert(t->t_funcs->tf_cursor != NULL);
	t->t_funcs->tf_cursor(t->t_softc, &t->t_cursor);
}

static inline void
teken_funcs_putchar(const teken_t *t, const teken_pos_t *p, teken_char_t c,
    const teken_attr_t *a)
{

	teken_assert(p->tp_row < t->t_winsize.tp_row);
	teken_assert(p->tp_col < t->t_winsize.tp_col);

	teken_assert(t->t_funcs->tf_putchar != NULL);
	t->t_funcs->tf_putchar(t->t_softc, p, c, a);
}

static inline void
teken_funcs_fill(const teken_t *t, const teken_rect_t *r,
    const teken_char_t c, const teken_attr_t *a)
{

	teken_assert(r->tr_end.tp_row > r->tr_begin.tp_row);
	teken_assert(r->tr_end.tp_row <= t->t_winsize.tp_row);
	teken_assert(r->tr_end.tp_col > r->tr_begin.tp_col);
	teken_assert(r->tr_end.tp_col <= t->t_winsize.tp_col);

	teken_assert(t->t_funcs->tf_fill != NULL);
	t->t_funcs->tf_fill(t->t_softc, r, c, a);
}

static inline void
teken_funcs_copy(const teken_t *t, const teken_rect_t *r, const teken_pos_t *p)
{

	teken_assert(r->tr_end.tp_row > r->tr_begin.tp_row);
	teken_assert(r->tr_end.tp_row <= t->t_winsize.tp_row);
	teken_assert(r->tr_end.tp_col > r->tr_begin.tp_col);
	teken_assert(r->tr_end.tp_col <= t->t_winsize.tp_col);
	teken_assert(p->tp_row + (r->tr_end.tp_row - r->tr_begin.tp_row) <= t->t_winsize.tp_row);
	teken_assert(p->tp_col + (r->tr_end.tp_col - r->tr_begin.tp_col) <= t->t_winsize.tp_col);

	teken_assert(t->t_funcs->tf_copy != NULL);
	t->t_funcs->tf_copy(t->t_softc, r, p);
}

static inline void
teken_funcs_pre_input(const teken_t *t)
{

	if (t->t_funcs->tf_pre_input != NULL)
		t->t_funcs->tf_pre_input(t->t_softc);
}

static inline void
teken_funcs_post_input(const teken_t *t)
{

	if (t->t_funcs->tf_post_input != NULL)
		t->t_funcs->tf_post_input(t->t_softc);
}

static inline void
teken_funcs_param(const teken_t *t, int cmd, unsigned int value)
{

	teken_assert(t->t_funcs->tf_param != NULL);
	t->t_funcs->tf_param(t->t_softc, cmd, value);
}

static inline void
teken_funcs_respond(const teken_t *t, const void *buf, size_t len)
{

	teken_assert(t->t_funcs->tf_respond != NULL);
	t->t_funcs->tf_respond(t->t_softc, buf, len);
}

#include "teken_subr.h"
#include "teken_subr_compat.h"

/*
 * Programming interface.
 */

void
teken_init(teken_t *t, const teken_funcs_t *tf, void *softc)
{
	teken_pos_t tp = { .tp_row = 24, .tp_col = 80 };

	t->t_funcs = tf;
	t->t_softc = softc;

	t->t_nextstate = teken_state_init;
	t->t_stateflags = 0;
	t->t_utf8_left = 0;

	t->t_defattr.ta_format = 0;
	t->t_defattr.ta_fgcolor = TC_WHITE;
	t->t_defattr.ta_bgcolor = TC_BLACK;
	teken_subr_do_reset(t);

	teken_set_winsize(t, &tp);
}

static void
teken_input_char(teken_t *t, teken_char_t c)
{

	/*
	 * There is no support for DCS and OSC.  Just discard strings
	 * until we receive characters that may indicate string
	 * termination.
	 */
	if (t->t_stateflags & TS_INSTRING) {
		switch (c) {
		case '\x1B':
			t->t_stateflags &= ~TS_INSTRING;
			break;
		case '\a':
			t->t_stateflags &= ~TS_INSTRING;
			return;
		default:
			return;
		}
	}

	switch (c) {
	case '\0':
		break;
	case '\a':
		teken_subr_bell(t);
		break;
	case '\b':
		teken_subr_backspace(t);
		break;
	case '\n':
	case '\x0B':
		teken_subr_newline(t);
		break;
	case '\x0C':
		teken_subr_newpage(t);
		break;
	case '\x0E':
		if (t->t_stateflags & TS_CONS25)
			t->t_nextstate(t, c);
		else
			t->t_curscs = 1;
		break;
	case '\x0F':
		if (t->t_stateflags & TS_CONS25)
			t->t_nextstate(t, c);
		else
			t->t_curscs = 0;
		break;
	case '\r':
		teken_subr_carriage_return(t);
		break;
	case '\t':
		teken_subr_horizontal_tab(t);
		break;
	default:
		t->t_nextstate(t, c);
		break;
	}

	/* Post-processing assertions. */
	teken_assert(t->t_cursor.tp_row >= t->t_originreg.ts_begin);
	teken_assert(t->t_cursor.tp_row < t->t_originreg.ts_end);
	teken_assert(t->t_cursor.tp_row < t->t_winsize.tp_row);
	teken_assert(t->t_cursor.tp_col < t->t_winsize.tp_col);
	teken_assert(t->t_saved_cursor.tp_row < t->t_winsize.tp_row);
	teken_assert(t->t_saved_cursor.tp_col < t->t_winsize.tp_col);
	teken_assert(t->t_scrollreg.ts_end <= t->t_winsize.tp_row);
	teken_assert(t->t_scrollreg.ts_begin < t->t_scrollreg.ts_end);
	/* Origin region has to be window size or the same as scrollreg. */
	teken_assert((t->t_originreg.ts_begin == t->t_scrollreg.ts_begin &&
	    t->t_originreg.ts_end == t->t_scrollreg.ts_end) ||
	    (t->t_originreg.ts_begin == 0 &&
	    t->t_originreg.ts_end == t->t_winsize.tp_row));
}

static void
teken_input_byte(teken_t *t, unsigned char c)
{

	/*
	 * UTF-8 handling.
	 */
	if ((c & 0x80) == 0x00 || t->t_stateflags & TS_8BIT) {
		/* One-byte sequence. */
		t->t_utf8_left = 0;
		teken_input_char(t, c);
	} else if ((c & 0xe0) == 0xc0) {
		/* Two-byte sequence. */
		t->t_utf8_left = 1;
		t->t_utf8_partial = c & 0x1f;
	} else if ((c & 0xf0) == 0xe0) {
		/* Three-byte sequence. */
		t->t_utf8_left = 2;
		t->t_utf8_partial = c & 0x0f;
	} else if ((c & 0xf8) == 0xf0) {
		/* Four-byte sequence. */
		t->t_utf8_left = 3;
		t->t_utf8_partial = c & 0x07;
	} else if ((c & 0xc0) == 0x80) {
		if (t->t_utf8_left == 0)
			return;
		t->t_utf8_left--;
		t->t_utf8_partial = (t->t_utf8_partial << 6) | (c & 0x3f);
		if (t->t_utf8_left == 0) {
			teken_printf("Got UTF-8 char %x\n", t->t_utf8_partial);
			teken_input_char(t, t->t_utf8_partial);
		}
	}
}

void
teken_input(teken_t *t, const void *buf, size_t len)
{
	const char *c = buf;

	teken_funcs_pre_input(t);
	while (len-- > 0)
		teken_input_byte(t, *c++);
	teken_funcs_post_input(t);
}

const teken_pos_t *
teken_get_cursor(const teken_t *t)
{

	return (&t->t_cursor);
}

void
teken_set_cursor(teken_t *t, const teken_pos_t *p)
{

	/* XXX: bounds checking with originreg! */
	teken_assert(p->tp_row < t->t_winsize.tp_row);
	teken_assert(p->tp_col < t->t_winsize.tp_col);

	t->t_cursor = *p;
}

const teken_attr_t *
teken_get_curattr(const teken_t *t)
{

	return (&t->t_curattr);
}

void
teken_set_curattr(teken_t *t, const teken_attr_t *a)
{

	t->t_curattr = *a;
}

const teken_attr_t *
teken_get_defattr(const teken_t *t)
{

	return (&t->t_defattr);
}

void
teken_set_defattr(teken_t *t, const teken_attr_t *a)
{

	t->t_curattr = t->t_saved_curattr = t->t_defattr = *a;
}

const teken_pos_t *
teken_get_winsize(const teken_t *t)
{

	return (&t->t_winsize);
}

static void
teken_trim_cursor_pos(teken_t *t, const teken_pos_t *new)
{
	const teken_pos_t *cur;

	cur = &t->t_winsize;

	if (cur->tp_row < new->tp_row || cur->tp_col < new->tp_col)
		return;
	if (t->t_cursor.tp_row >= new->tp_row)
		t->t_cursor.tp_row = new->tp_row - 1;
	if (t->t_cursor.tp_col >= new->tp_col)
		t->t_cursor.tp_col = new->tp_col - 1;
}

void
teken_set_winsize(teken_t *t, const teken_pos_t *p)
{

	teken_trim_cursor_pos(t, p);
	t->t_winsize = *p;
	teken_subr_do_reset(t);
}

void
teken_set_winsize_noreset(teken_t *t, const teken_pos_t *p)
{

	teken_trim_cursor_pos(t, p);
	t->t_winsize = *p;
	teken_subr_do_resize(t);
}

void
teken_set_8bit(teken_t *t)
{

	t->t_stateflags |= TS_8BIT;
}

void
teken_set_cons25(teken_t *t)
{

	t->t_stateflags |= TS_CONS25;
}

void
teken_set_cons25keys(teken_t *t)
{

	t->t_stateflags |= TS_CONS25KEYS;
}

/*
 * State machine.
 */

static void
teken_state_switch(teken_t *t, teken_state_t *s)
{

	t->t_nextstate = s;
	t->t_curnum = 0;
	t->t_stateflags |= TS_FIRSTDIGIT;
}

static int
teken_state_numbers(teken_t *t, teken_char_t c)
{

	teken_assert(t->t_curnum < T_NUMSIZE);

	if (c >= '0' && c <= '9') {
		if (t->t_stateflags & TS_FIRSTDIGIT) {
			/* First digit. */
			t->t_stateflags &= ~TS_FIRSTDIGIT;
			t->t_nums[t->t_curnum] = c - '0';
		} else if (t->t_nums[t->t_curnum] < UINT_MAX / 100) {
			/*
			 * There is no need to continue parsing input
			 * once the value exceeds the size of the
			 * terminal. It would only allow for integer
			 * overflows when performing arithmetic on the
			 * cursor position.
			 *
			 * Ignore any further digits if the value is
			 * already UINT_MAX / 100.
			 */
			t->t_nums[t->t_curnum] =
			    t->t_nums[t->t_curnum] * 10 + c - '0';
		}
		return (1);
	} else if (c == ';') {
		if (t->t_stateflags & TS_FIRSTDIGIT)
			t->t_nums[t->t_curnum] = 0;

		/* Only allow a limited set of arguments. */
		if (++t->t_curnum == T_NUMSIZE) {
			teken_state_switch(t, teken_state_init);
			return (1);
		}

		t->t_stateflags |= TS_FIRSTDIGIT;
		return (1);
	} else {
		if (t->t_stateflags & TS_FIRSTDIGIT && t->t_curnum > 0) {
			/* Finish off the last empty argument. */
			t->t_nums[t->t_curnum] = 0;
			t->t_curnum++;
		} else if ((t->t_stateflags & TS_FIRSTDIGIT) == 0) {
			/* Also count the last argument. */
			t->t_curnum++;
		}
	}

	return (0);
}

#define	k	TC_BLACK
#define	b	TC_BLUE
#define	y	TC_BROWN
#define	c	TC_CYAN
#define	g	TC_GREEN
#define	m	TC_MAGENTA
#define	r	TC_RED
#define	w	TC_WHITE
#define	K	(TC_BLACK | TC_LIGHT)
#define	B	(TC_BLUE | TC_LIGHT)
#define	Y	(TC_BROWN | TC_LIGHT)
#define	C	(TC_CYAN | TC_LIGHT)
#define	G	(TC_GREEN | TC_LIGHT)
#define	M	(TC_MAGENTA | TC_LIGHT)
#define	R	(TC_RED | TC_LIGHT)
#define	W	(TC_WHITE | TC_LIGHT)

/**
 * The xterm-256 color map has steps of 0x28 (in the range 0-0xff), except
 * for the first step which is 0x5f.  Scale to the range 0-6 by dividing
 * by 0x28 and rounding down.  The range of 0-5 cannot represent the
 * larger first step.
 *
 * This table is generated by the follow rules:
 * - if all components are equal, the result is black for (0, 0, 0) and
 *   (2, 2, 2), else white; otherwise:
 * - subtract the smallest component from all components
 * - if this gives only one nonzero component, then that is the color
 * - else if one component is 2 or more larger than the other nonzero one,
 *   then that component gives the color
 * - else there are 2 nonzero components.  The color is that of a small
 *   equal mixture of these components (cyan, yellow or magenta).  E.g.,
 *   (0, 5, 6) (Turquoise2) is a much purer cyan than (0, 2, 3)
 *   (DeepSkyBlue4), but we map both to cyan since we can't represent
 *   delicate shades of either blue or cyan and blue would be worse.
 *   Here it is important that components of 1 never occur.  Blue would
 *   be twice as large as green in (0, 1, 2).
 */
static const teken_color_t teken_256to8tab[] = {
	/* xterm normal colors: */
	k, r, g, y, b, m, c, w,

	/* xterm bright colors: */
	k, r, g, y, b, m, c, w,

	/* Red0 submap. */
	k, b, b, b, b, b,
	g, c, c, b, b, b,
	g, c, c, c, b, b,
	g, g, c, c, c, b,
	g, g, g, c, c, c,
	g, g, g, g, c, c,

	/* Red2 submap. */
	r, m, m, b, b, b,
	y, k, b, b, b, b,
	y, g, c, c, b, b,
	g, g, c, c, c, b,
	g, g, g, c, c, c,
	g, g, g, g, c, c,

	/* Red3 submap. */
	r, m, m, m, b, b,
	y, r, m, m, b, b,
	y, y, w, b, b, b,
	y, y, g, c, c, b,
	g, g, g, c, c, c,
	g, g, g, g, c, c,

	/* Red4 submap. */
	r, r, m, m, m, b,
	r, r, m, m, m, b,
	y, y, r, m, m, b,
	y, y, y, w, b, b,
	y, y, y, g, c, c,
	g, g, g, g, c, c,

	/* Red5 submap. */
	r, r, r, m, m, m,
	r, r, r, m, m, m,
	r, r, r, m, m, m,
	y, y, y, r, m, m,
	y, y, y, y, w, b,
	y, y, y, y, g, c,

	/* Red6 submap. */
	r, r, r, r, m, m,
	r, r, r, r, m, m,
	r, r, r, r, m, m,
	r, r, r, r, m, m,
	y, y, y, y, r, m,
	y, y, y, y, y, w,

	/* Grey submap. */
	k, k, k, k, k, k,
	k, k, k, k, k, k,
	w, w, w, w, w, w,
	w, w, w, w, w, w,
};

/*
 * This table is generated from the previous one by setting TC_LIGHT for
 * entries whose luminosity in the xterm256 color map is 60% or larger.
 * Thus the previous table is currently not really needed.  It will be
 * used for different fine tuning of the tables.
 */
static const teken_color_t teken_256to16tab[] = {
	/* xterm normal colors: */
	k, r, g, y, b, m, c, w,

	/* xterm bright colors: */
	K, R, G, Y, B, M, C, W,

	/* Red0 submap. */
	k, b, b, b, b, b,
	g, c, c, b, b, b,
	g, c, c, c, b, b,
	g, g, c, c, c, b,
	g, g, g, c, c, c,
	g, g, g, g, c, c,

	/* Red2 submap. */
	r, m, m, b, b, b,
	y, K, b, b, B, B,
	y, g, c, c, B, B,
	g, g, c, c, C, B,
	g, G, G, C, C, C,
	g, G, G, G, C, C,

	/* Red3 submap. */
	r, m, m, m, b, b,
	y, r, m, m, B, B,
	y, y, w, B, B, B,
	y, y, G, C, C, B,
	g, G, G, C, C, C,
	g, G, G, G, C, C,

	/* Red4 submap. */
	r, r, m, m, m, b,
	r, r, m, m, M, B,
	y, y, R, M, M, B,
	y, y, Y, W, B, B,
	y, Y, Y, G, C, C,
	g, G, G, G, C, C,

	/* Red5 submap. */
	r, r, r, m, m, m,
	r, R, R, M, M, M,
	r, R, R, M, M, M,
	y, Y, Y, R, M, M,
	y, Y, Y, Y, W, B,
	y, Y, Y, Y, G, C,

	/* Red6 submap. */
	r, r, r, r, m, m,
	r, R, R, R, M, M,
	r, R, R, R, M, M,
	r, R, R, R, M, M,
	y, Y, Y, Y, R, M,
	y, Y, Y, Y, Y, W,

	/* Grey submap. */
	k, k, k, k, k, k,
	K, K, K, K, K, K,
	w, w, w, w, w, w,
	W, W, W, W, W, W,
};

#undef	k
#undef	b
#undef	y
#undef	c
#undef	g
#undef	m
#undef	r
#undef	w
#undef	K
#undef	B
#undef	Y
#undef	C
#undef	G
#undef	M
#undef	R
#undef	W

teken_color_t
teken_256to8(teken_color_t c)
{

	return (teken_256to8tab[c % 256]);
}

teken_color_t
teken_256to16(teken_color_t c)
{

	return (teken_256to16tab[c % 256]);
}

static const char * const special_strings_cons25[] = {
	[TKEY_UP] = "\x1B[A",		[TKEY_DOWN] = "\x1B[B",
	[TKEY_LEFT] = "\x1B[D",		[TKEY_RIGHT] = "\x1B[C",

	[TKEY_HOME] = "\x1B[H",		[TKEY_END] = "\x1B[F",
	[TKEY_INSERT] = "\x1B[L",	[TKEY_DELETE] = "\x7F",
	[TKEY_PAGE_UP] = "\x1B[I",	[TKEY_PAGE_DOWN] = "\x1B[G",

	[TKEY_F1] = "\x1B[M",		[TKEY_F2] = "\x1B[N",
	[TKEY_F3] = "\x1B[O",		[TKEY_F4] = "\x1B[P",
	[TKEY_F5] = "\x1B[Q",		[TKEY_F6] = "\x1B[R",
	[TKEY_F7] = "\x1B[S",		[TKEY_F8] = "\x1B[T",
	[TKEY_F9] = "\x1B[U",		[TKEY_F10] = "\x1B[V",
	[TKEY_F11] = "\x1B[W",		[TKEY_F12] = "\x1B[X",
};

static const char * const special_strings_ckeys[] = {
	[TKEY_UP] = "\x1BOA",		[TKEY_DOWN] = "\x1BOB",
	[TKEY_LEFT] = "\x1BOD",		[TKEY_RIGHT] = "\x1BOC",

	[TKEY_HOME] = "\x1BOH",		[TKEY_END] = "\x1BOF",
};

static const char * const special_strings_normal[] = {
	[TKEY_UP] = "\x1B[A",		[TKEY_DOWN] = "\x1B[B",
	[TKEY_LEFT] = "\x1B[D",		[TKEY_RIGHT] = "\x1B[C",

	[TKEY_HOME] = "\x1B[H",		[TKEY_END] = "\x1B[F",
	[TKEY_INSERT] = "\x1B[2~",	[TKEY_DELETE] = "\x1B[3~",
	[TKEY_PAGE_UP] = "\x1B[5~",	[TKEY_PAGE_DOWN] = "\x1B[6~",

	[TKEY_F1] = "\x1BOP",		[TKEY_F2] = "\x1BOQ",
	[TKEY_F3] = "\x1BOR",		[TKEY_F4] = "\x1BOS",
	[TKEY_F5] = "\x1B[15~",		[TKEY_F6] = "\x1B[17~",
	[TKEY_F7] = "\x1B[18~",		[TKEY_F8] = "\x1B[19~",
	[TKEY_F9] = "\x1B[20~",		[TKEY_F10] = "\x1B[21~",
	[TKEY_F11] = "\x1B[23~",	[TKEY_F12] = "\x1B[24~",
};

const char *
teken_get_sequence(const teken_t *t, unsigned int k)
{

	/* Cons25 mode. */
	if ((t->t_stateflags & (TS_CONS25 | TS_CONS25KEYS)) ==
	    (TS_CONS25 | TS_CONS25KEYS))
		return (NULL);	/* Don't override good kbd(4) strings. */
	if (t->t_stateflags & TS_CONS25 &&
	    k < sizeof special_strings_cons25 / sizeof(char *))
		return (special_strings_cons25[k]);

	/* Cursor keys mode. */
	if (t->t_stateflags & TS_CURSORKEYS &&
	    k < sizeof special_strings_ckeys / sizeof(char *))
		return (special_strings_ckeys[k]);

	/* Default xterm sequences. */
	if (k < sizeof special_strings_normal / sizeof(char *))
		return (special_strings_normal[k]);

	return (NULL);
}

#include "teken_state.h"
