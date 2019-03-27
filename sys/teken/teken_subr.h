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

static void teken_subr_cursor_up(teken_t *, unsigned int);
static void teken_subr_erase_line(const teken_t *, unsigned int);
static void teken_subr_regular_character(teken_t *, teken_char_t);
static void teken_subr_reset_to_initial_state(teken_t *);
static void teken_subr_save_cursor(teken_t *);

static inline int
teken_tab_isset(const teken_t *t, unsigned int col)
{
	unsigned int b, o;

	if (col >= T_NUMCOL)
		return ((col % 8) == 0);

	b = col / (sizeof(unsigned int) * 8);
	o = col % (sizeof(unsigned int) * 8);

	return (t->t_tabstops[b] & (1U << o));
}

static inline void
teken_tab_clear(teken_t *t, unsigned int col)
{
	unsigned int b, o;

	if (col >= T_NUMCOL)
		return;

	b = col / (sizeof(unsigned int) * 8);
	o = col % (sizeof(unsigned int) * 8);

	t->t_tabstops[b] &= ~(1U << o);
}

static inline void
teken_tab_set(teken_t *t, unsigned int col)
{
	unsigned int b, o;

	if (col >= T_NUMCOL)
		return;

	b = col / (sizeof(unsigned int) * 8);
	o = col % (sizeof(unsigned int) * 8);

	t->t_tabstops[b] |= 1U << o;
}

static void
teken_tab_default(teken_t *t)
{
	unsigned int i;

	memset(t->t_tabstops, 0, T_NUMCOL / 8);

	for (i = 8; i < T_NUMCOL; i += 8)
		teken_tab_set(t, i);
}

static void
teken_subr_do_scroll(const teken_t *t, int amount)
{
	teken_rect_t tr;
	teken_pos_t tp;

	teken_assert(t->t_cursor.tp_row <= t->t_winsize.tp_row);
	teken_assert(t->t_scrollreg.ts_end <= t->t_winsize.tp_row);
	teken_assert(amount != 0);

	/* Copy existing data 1 line up. */
	if (amount > 0) {
		/* Scroll down. */

		/* Copy existing data up. */
		if (t->t_scrollreg.ts_begin + amount < t->t_scrollreg.ts_end) {
			tr.tr_begin.tp_row = t->t_scrollreg.ts_begin + amount;
			tr.tr_begin.tp_col = 0;
			tr.tr_end.tp_row = t->t_scrollreg.ts_end;
			tr.tr_end.tp_col = t->t_winsize.tp_col;
			tp.tp_row = t->t_scrollreg.ts_begin;
			tp.tp_col = 0;
			teken_funcs_copy(t, &tr, &tp);

			tr.tr_begin.tp_row = t->t_scrollreg.ts_end - amount;
		} else {
			tr.tr_begin.tp_row = t->t_scrollreg.ts_begin;
		}

		/* Clear the last lines. */
		tr.tr_begin.tp_col = 0;
		tr.tr_end.tp_row = t->t_scrollreg.ts_end;
		tr.tr_end.tp_col = t->t_winsize.tp_col;
		teken_funcs_fill(t, &tr, BLANK, &t->t_curattr);
	} else {
		/* Scroll up. */
		amount = -amount;

		/* Copy existing data down. */
		if (t->t_scrollreg.ts_begin + amount < t->t_scrollreg.ts_end) {
			tr.tr_begin.tp_row = t->t_scrollreg.ts_begin;
			tr.tr_begin.tp_col = 0;
			tr.tr_end.tp_row = t->t_scrollreg.ts_end - amount;
			tr.tr_end.tp_col = t->t_winsize.tp_col;
			tp.tp_row = t->t_scrollreg.ts_begin + amount;
			tp.tp_col = 0;
			teken_funcs_copy(t, &tr, &tp);

			tr.tr_end.tp_row = t->t_scrollreg.ts_begin + amount;
		} else {
			tr.tr_end.tp_row = t->t_scrollreg.ts_end;
		}

		/* Clear the first lines. */
		tr.tr_begin.tp_row = t->t_scrollreg.ts_begin;
		tr.tr_begin.tp_col = 0;
		tr.tr_end.tp_col = t->t_winsize.tp_col;
		teken_funcs_fill(t, &tr, BLANK, &t->t_curattr);
	}
}

static ssize_t
teken_subr_do_cpr(const teken_t *t, unsigned int cmd, char response[16])
{

	switch (cmd) {
	case 5: /* Operating status. */
		strcpy(response, "0n");
		return (2);
	case 6: { /* Cursor position. */
		int len;

		len = snprintf(response, 16, "%u;%uR",
		    (t->t_cursor.tp_row - t->t_originreg.ts_begin) + 1,
		    t->t_cursor.tp_col + 1);

		if (len >= 16)
			return (-1);
		return (len);
	}
	case 15: /* Printer status. */
		strcpy(response, "13n");
		return (3);
	case 25: /* UDK status. */
		strcpy(response, "20n");
		return (3);
	case 26: /* Keyboard status. */
		strcpy(response, "27;1n");
		return (5);
	default:
		teken_printf("Unknown DSR\n");
		return (-1);
	}
}

static void
teken_subr_alignment_test(teken_t *t)
{
	teken_rect_t tr;

	t->t_cursor.tp_row = t->t_cursor.tp_col = 0;
	t->t_scrollreg.ts_begin = 0;
	t->t_scrollreg.ts_end = t->t_winsize.tp_row;
	t->t_originreg = t->t_scrollreg;
	t->t_stateflags &= ~(TS_WRAPPED|TS_ORIGIN);
	teken_funcs_cursor(t);

	tr.tr_begin.tp_row = 0;
	tr.tr_begin.tp_col = 0;
	tr.tr_end = t->t_winsize;
	teken_funcs_fill(t, &tr, 'E', &t->t_defattr);
}

static void
teken_subr_backspace(teken_t *t)
{

	if (t->t_stateflags & TS_CONS25) {
		if (t->t_cursor.tp_col == 0) {
			if (t->t_cursor.tp_row == t->t_originreg.ts_begin)
				return;
			t->t_cursor.tp_row--;
			t->t_cursor.tp_col = t->t_winsize.tp_col - 1;
		} else {
			t->t_cursor.tp_col--;
		}
	} else {
		if (t->t_cursor.tp_col == 0)
			return;

		t->t_cursor.tp_col--;
		t->t_stateflags &= ~TS_WRAPPED;
	}

	teken_funcs_cursor(t);
}

static void
teken_subr_bell(const teken_t *t)
{

	teken_funcs_bell(t);
}

static void
teken_subr_carriage_return(teken_t *t)
{

	t->t_cursor.tp_col = 0;
	t->t_stateflags &= ~TS_WRAPPED;
	teken_funcs_cursor(t);
}

static void
teken_subr_cursor_backward(teken_t *t, unsigned int ncols)
{

	if (ncols > t->t_cursor.tp_col)
		t->t_cursor.tp_col = 0;
	else
		t->t_cursor.tp_col -= ncols;
	t->t_stateflags &= ~TS_WRAPPED;
	teken_funcs_cursor(t);
}

static void
teken_subr_cursor_backward_tabulation(teken_t *t, unsigned int ntabs)
{

	do {
		/* Stop when we've reached the beginning of the line. */
		if (t->t_cursor.tp_col == 0)
			break;

		t->t_cursor.tp_col--;

		/* Tab marker set. */
		if (teken_tab_isset(t, t->t_cursor.tp_col))
			ntabs--;
	} while (ntabs > 0);

	teken_funcs_cursor(t);
}

static void
teken_subr_cursor_down(teken_t *t, unsigned int nrows)
{

	if (t->t_cursor.tp_row + nrows >= t->t_scrollreg.ts_end)
		t->t_cursor.tp_row = t->t_scrollreg.ts_end - 1;
	else
		t->t_cursor.tp_row += nrows;
	t->t_stateflags &= ~TS_WRAPPED;
	teken_funcs_cursor(t);
}

static void
teken_subr_cursor_forward(teken_t *t, unsigned int ncols)
{

	if (t->t_cursor.tp_col + ncols >= t->t_winsize.tp_col)
		t->t_cursor.tp_col = t->t_winsize.tp_col - 1;
	else
		t->t_cursor.tp_col += ncols;
	t->t_stateflags &= ~TS_WRAPPED;
	teken_funcs_cursor(t);
}

static void
teken_subr_cursor_forward_tabulation(teken_t *t, unsigned int ntabs)
{

	do {
		/* Stop when we've reached the end of the line. */
		if (t->t_cursor.tp_col == t->t_winsize.tp_col - 1)
			break;

		t->t_cursor.tp_col++;

		/* Tab marker set. */
		if (teken_tab_isset(t, t->t_cursor.tp_col))
			ntabs--;
	} while (ntabs > 0);

	teken_funcs_cursor(t);
}

static void
teken_subr_cursor_next_line(teken_t *t, unsigned int ncols)
{

	t->t_cursor.tp_col = 0;
	teken_subr_cursor_down(t, ncols);
}

static void
teken_subr_cursor_position(teken_t *t, unsigned int row, unsigned int col)
{

	row = (row - 1) + t->t_originreg.ts_begin;
	t->t_cursor.tp_row = row < t->t_originreg.ts_end ?
	    row : t->t_originreg.ts_end - 1;

	col--;
	t->t_cursor.tp_col = col < t->t_winsize.tp_col ?
	    col : t->t_winsize.tp_col - 1;

	t->t_stateflags &= ~TS_WRAPPED;
	teken_funcs_cursor(t);
}

static void
teken_subr_cursor_position_report(const teken_t *t, unsigned int cmd)
{
	char response[18] = "\x1B[";
	ssize_t len;

	len = teken_subr_do_cpr(t, cmd, response + 2);
	if (len < 0)
		return;

	teken_funcs_respond(t, response, len + 2);
}

static void
teken_subr_cursor_previous_line(teken_t *t, unsigned int ncols)
{

	t->t_cursor.tp_col = 0;
	teken_subr_cursor_up(t, ncols);
}

static void
teken_subr_cursor_up(teken_t *t, unsigned int nrows)
{

	if (t->t_scrollreg.ts_begin + nrows >= t->t_cursor.tp_row)
		t->t_cursor.tp_row = t->t_scrollreg.ts_begin;
	else
		t->t_cursor.tp_row -= nrows;
	t->t_stateflags &= ~TS_WRAPPED;
	teken_funcs_cursor(t);
}

static void
teken_subr_set_cursor_style(teken_t *t __unused, unsigned int style __unused)
{

	/* TODO */

	/*
	 * CSI Ps SP q
	 *   Set cursor style (DECSCUSR), VT520.
	 *     Ps = 0  -> blinking block.
	 *     Ps = 1  -> blinking block (default).
	 *     Ps = 2  -> steady block.
	 *     Ps = 3  -> blinking underline.
	 *     Ps = 4  -> steady underline.
	 *     Ps = 5  -> blinking bar (xterm).
	 *     Ps = 6  -> steady bar (xterm).
	 */
}

static void
teken_subr_delete_character(const teken_t *t, unsigned int ncols)
{
	teken_rect_t tr;

	tr.tr_begin.tp_row = t->t_cursor.tp_row;
	tr.tr_end.tp_row = t->t_cursor.tp_row + 1;
	tr.tr_end.tp_col = t->t_winsize.tp_col;

	if (t->t_cursor.tp_col + ncols >= t->t_winsize.tp_col) {
		tr.tr_begin.tp_col = t->t_cursor.tp_col;
	} else {
		/* Copy characters to the left. */
		tr.tr_begin.tp_col = t->t_cursor.tp_col + ncols;
		teken_funcs_copy(t, &tr, &t->t_cursor);

		tr.tr_begin.tp_col = t->t_winsize.tp_col - ncols;
	}

	/* Blank trailing columns. */
	teken_funcs_fill(t, &tr, BLANK, &t->t_curattr);
}

static void
teken_subr_delete_line(const teken_t *t, unsigned int nrows)
{
	teken_rect_t tr;

	/* Ignore if outside scrolling region. */
	if (t->t_cursor.tp_row < t->t_scrollreg.ts_begin ||
	    t->t_cursor.tp_row >= t->t_scrollreg.ts_end)
		return;

	tr.tr_begin.tp_col = 0;
	tr.tr_end.tp_row = t->t_scrollreg.ts_end;
	tr.tr_end.tp_col = t->t_winsize.tp_col;

	if (t->t_cursor.tp_row + nrows >= t->t_scrollreg.ts_end) {
		tr.tr_begin.tp_row = t->t_cursor.tp_row;
	} else {
		teken_pos_t tp;

		/* Copy rows up. */
		tr.tr_begin.tp_row = t->t_cursor.tp_row + nrows;
		tp.tp_row = t->t_cursor.tp_row;
		tp.tp_col = 0;
		teken_funcs_copy(t, &tr, &tp);

		tr.tr_begin.tp_row = t->t_scrollreg.ts_end - nrows;
	}

	/* Blank trailing rows. */
	teken_funcs_fill(t, &tr, BLANK, &t->t_curattr);
}

static void
teken_subr_device_control_string(teken_t *t)
{

	teken_printf("Unsupported device control string\n");
	t->t_stateflags |= TS_INSTRING;
}

static void
teken_subr_device_status_report(const teken_t *t, unsigned int cmd)
{
	char response[19] = "\x1B[?";
	ssize_t len;

	len = teken_subr_do_cpr(t, cmd, response + 3);
	if (len < 0)
		return;

	teken_funcs_respond(t, response, len + 3);
}

static void
teken_subr_double_height_double_width_line_top(const teken_t *t)
{

	(void)t;
	teken_printf("double height double width top\n");
}

static void
teken_subr_double_height_double_width_line_bottom(const teken_t *t)
{

	(void)t;
	teken_printf("double height double width bottom\n");
}

static void
teken_subr_erase_character(const teken_t *t, unsigned int ncols)
{
	teken_rect_t tr;

	tr.tr_begin = t->t_cursor;
	tr.tr_end.tp_row = t->t_cursor.tp_row + 1;

	if (t->t_cursor.tp_col + ncols >= t->t_winsize.tp_col)
		tr.tr_end.tp_col = t->t_winsize.tp_col;
	else
		tr.tr_end.tp_col = t->t_cursor.tp_col + ncols;

	teken_funcs_fill(t, &tr, BLANK, &t->t_curattr);
}

static void
teken_subr_erase_display(const teken_t *t, unsigned int mode)
{
	teken_rect_t r;

	r.tr_begin.tp_col = 0;
	r.tr_end.tp_col = t->t_winsize.tp_col;

	switch (mode) {
	case 1: /* Erase from the top to the cursor. */
		teken_subr_erase_line(t, 1);

		/* Erase lines above. */
		if (t->t_cursor.tp_row == 0)
			return;
		r.tr_begin.tp_row = 0;
		r.tr_end.tp_row = t->t_cursor.tp_row;
		break;
	case 2: /* Erase entire display. */
		r.tr_begin.tp_row = 0;
		r.tr_end.tp_row = t->t_winsize.tp_row;
		break;
	default: /* Erase from cursor to the bottom. */
		teken_subr_erase_line(t, 0);

		/* Erase lines below. */
		if (t->t_cursor.tp_row == t->t_winsize.tp_row - 1)
			return;
		r.tr_begin.tp_row = t->t_cursor.tp_row + 1;
		r.tr_end.tp_row = t->t_winsize.tp_row;
		break;
	}

	teken_funcs_fill(t, &r, BLANK, &t->t_curattr);
}

static void
teken_subr_erase_line(const teken_t *t, unsigned int mode)
{
	teken_rect_t r;

	r.tr_begin.tp_row = t->t_cursor.tp_row;
	r.tr_end.tp_row = t->t_cursor.tp_row + 1;

	switch (mode) {
	case 1: /* Erase from the beginning of the line to the cursor. */
		r.tr_begin.tp_col = 0;
		r.tr_end.tp_col = t->t_cursor.tp_col + 1;
		break;
	case 2: /* Erase entire line. */
		r.tr_begin.tp_col = 0;
		r.tr_end.tp_col = t->t_winsize.tp_col;
		break;
	default: /* Erase from cursor to the end of the line. */
		r.tr_begin.tp_col = t->t_cursor.tp_col;
		r.tr_end.tp_col = t->t_winsize.tp_col;
		break;
	}

	teken_funcs_fill(t, &r, BLANK, &t->t_curattr);
}

static void
teken_subr_g0_scs_special_graphics(teken_t *t)
{

	t->t_scs[0] = teken_scs_special_graphics;
}

static void
teken_subr_g0_scs_uk_national(teken_t *t)
{

	t->t_scs[0] = teken_scs_uk_national;
}

static void
teken_subr_g0_scs_us_ascii(teken_t *t)
{

	t->t_scs[0] = teken_scs_us_ascii;
}

static void
teken_subr_g1_scs_special_graphics(teken_t *t)
{

	t->t_scs[1] = teken_scs_special_graphics;
}

static void
teken_subr_g1_scs_uk_national(teken_t *t)
{

	t->t_scs[1] = teken_scs_uk_national;
}

static void
teken_subr_g1_scs_us_ascii(teken_t *t)
{

	t->t_scs[1] = teken_scs_us_ascii;
}

static void
teken_subr_horizontal_position_absolute(teken_t *t, unsigned int col)
{

	col--;
	t->t_cursor.tp_col = col < t->t_winsize.tp_col ?
	    col : t->t_winsize.tp_col - 1;

	t->t_stateflags &= ~TS_WRAPPED;
	teken_funcs_cursor(t);
}

static void
teken_subr_horizontal_tab(teken_t *t)
{

	teken_subr_cursor_forward_tabulation(t, 1);
}

static void
teken_subr_horizontal_tab_set(teken_t *t)
{

	teken_tab_set(t, t->t_cursor.tp_col);
}

static void
teken_subr_index(teken_t *t)
{

	if (t->t_cursor.tp_row < t->t_scrollreg.ts_end - 1) {
		t->t_cursor.tp_row++;
		t->t_stateflags &= ~TS_WRAPPED;
		teken_funcs_cursor(t);
	} else {
		teken_subr_do_scroll(t, 1);
	}
}

static void
teken_subr_insert_character(const teken_t *t, unsigned int ncols)
{
	teken_rect_t tr;

	tr.tr_begin = t->t_cursor;
	tr.tr_end.tp_row = t->t_cursor.tp_row + 1;

	if (t->t_cursor.tp_col + ncols >= t->t_winsize.tp_col) {
		tr.tr_end.tp_col = t->t_winsize.tp_col;
	} else {
		teken_pos_t tp;

		/* Copy characters to the right. */
		tr.tr_end.tp_col = t->t_winsize.tp_col - ncols;
		tp.tp_row = t->t_cursor.tp_row;
		tp.tp_col = t->t_cursor.tp_col + ncols;
		teken_funcs_copy(t, &tr, &tp);

		tr.tr_end.tp_col = t->t_cursor.tp_col + ncols;
	}

	/* Blank current location. */
	teken_funcs_fill(t, &tr, BLANK, &t->t_curattr);
}

static void
teken_subr_insert_line(const teken_t *t, unsigned int nrows)
{
	teken_rect_t tr;

	/* Ignore if outside scrolling region. */
	if (t->t_cursor.tp_row < t->t_scrollreg.ts_begin ||
	    t->t_cursor.tp_row >= t->t_scrollreg.ts_end)
		return;

	tr.tr_begin.tp_row = t->t_cursor.tp_row;
	tr.tr_begin.tp_col = 0;
	tr.tr_end.tp_col = t->t_winsize.tp_col;

	if (t->t_cursor.tp_row + nrows >= t->t_scrollreg.ts_end) {
		tr.tr_end.tp_row = t->t_scrollreg.ts_end;
	} else {
		teken_pos_t tp;

		/* Copy lines down. */
		tr.tr_end.tp_row = t->t_scrollreg.ts_end - nrows;
		tp.tp_row = t->t_cursor.tp_row + nrows;
		tp.tp_col = 0;
		teken_funcs_copy(t, &tr, &tp);

		tr.tr_end.tp_row = t->t_cursor.tp_row + nrows;
	}

	/* Blank current location. */
	teken_funcs_fill(t, &tr, BLANK, &t->t_curattr);
}

static void
teken_subr_keypad_application_mode(const teken_t *t)
{

	teken_funcs_param(t, TP_KEYPADAPP, 1);
}

static void
teken_subr_keypad_numeric_mode(const teken_t *t)
{

	teken_funcs_param(t, TP_KEYPADAPP, 0);
}

static void
teken_subr_newline(teken_t *t)
{

	t->t_cursor.tp_row++;

	if (t->t_cursor.tp_row >= t->t_scrollreg.ts_end) {
		teken_subr_do_scroll(t, 1);
		t->t_cursor.tp_row = t->t_scrollreg.ts_end - 1;
	}

	t->t_stateflags &= ~TS_WRAPPED;
	teken_funcs_cursor(t);
}

static void
teken_subr_newpage(teken_t *t)
{

	if (t->t_stateflags & TS_CONS25) {
		teken_rect_t tr;

		/* Clear screen. */
		tr.tr_begin.tp_row = t->t_originreg.ts_begin;
		tr.tr_begin.tp_col = 0;
		tr.tr_end.tp_row = t->t_originreg.ts_end;
		tr.tr_end.tp_col = t->t_winsize.tp_col;
		teken_funcs_fill(t, &tr, BLANK, &t->t_curattr);

		/* Cursor at top left. */
		t->t_cursor.tp_row = t->t_originreg.ts_begin;
		t->t_cursor.tp_col = 0;
		t->t_stateflags &= ~TS_WRAPPED;
		teken_funcs_cursor(t);
	} else {
		teken_subr_newline(t);
	}
}

static void
teken_subr_next_line(teken_t *t)
{

	t->t_cursor.tp_col = 0;
	teken_subr_newline(t);
}

static void
teken_subr_operating_system_command(teken_t *t)
{

	teken_printf("Unsupported operating system command\n");
	t->t_stateflags |= TS_INSTRING;
}

static void
teken_subr_pan_down(const teken_t *t, unsigned int nrows)
{

	teken_subr_do_scroll(t, (int)nrows);
}

static void
teken_subr_pan_up(const teken_t *t, unsigned int nrows)
{

	teken_subr_do_scroll(t, -(int)nrows);
}

static void
teken_subr_primary_device_attributes(const teken_t *t, unsigned int request)
{

	if (request == 0) {
		const char response[] = "\x1B[?1;2c";

		teken_funcs_respond(t, response, sizeof response - 1);
	} else {
		teken_printf("Unknown DA1\n");
	}
}

static void
teken_subr_do_putchar(teken_t *t, const teken_pos_t *tp, teken_char_t c,
    int width)
{

	t->t_last = c;
	if (t->t_stateflags & TS_INSERT &&
	    tp->tp_col < t->t_winsize.tp_col - width) {
		teken_rect_t ctr;
		teken_pos_t ctp;

		/* Insert mode. Move existing characters to the right. */
		ctr.tr_begin = *tp;
		ctr.tr_end.tp_row = tp->tp_row + 1;
		ctr.tr_end.tp_col = t->t_winsize.tp_col - width;
		ctp.tp_row = tp->tp_row;
		ctp.tp_col = tp->tp_col + width;
		teken_funcs_copy(t, &ctr, &ctp);
	}

	teken_funcs_putchar(t, tp, c, &t->t_curattr);

	if (width == 2 && tp->tp_col + 1 < t->t_winsize.tp_col) {
		teken_pos_t tp2;
		teken_attr_t attr;

		/* Print second half of CJK fullwidth character. */
		tp2.tp_row = tp->tp_row;
		tp2.tp_col = tp->tp_col + 1;
		attr = t->t_curattr;
		attr.ta_format |= TF_CJK_RIGHT;
		teken_funcs_putchar(t, &tp2, c, &attr);
	}
}

static void
teken_subr_regular_character(teken_t *t, teken_char_t c)
{
	int width;

	if (t->t_stateflags & TS_8BIT) {
		if (!(t->t_stateflags & TS_CONS25) && (c <= 0x1b || c == 0x7f))
			return;
		c = teken_scs_process(t, c);
		width = 1;
	} else {
		c = teken_scs_process(t, c);
		width = teken_wcwidth(c);
		/* XXX: Don't process zero-width characters yet. */
		if (width <= 0)
			return;
	}

	if (t->t_stateflags & TS_CONS25) {
		teken_subr_do_putchar(t, &t->t_cursor, c, width);
		t->t_cursor.tp_col += width;

		if (t->t_cursor.tp_col >= t->t_winsize.tp_col) {
			if (t->t_cursor.tp_row == t->t_scrollreg.ts_end - 1) {
				/* Perform scrolling. */
				teken_subr_do_scroll(t, 1);
			} else {
				/* No scrolling needed. */
				if (t->t_cursor.tp_row <
				    t->t_winsize.tp_row - 1)
					t->t_cursor.tp_row++;
			}
			t->t_cursor.tp_col = 0;
		}
	} else if (t->t_stateflags & TS_AUTOWRAP &&
	    ((t->t_stateflags & TS_WRAPPED &&
	    t->t_cursor.tp_col + 1 == t->t_winsize.tp_col) ||
	    t->t_cursor.tp_col + width > t->t_winsize.tp_col)) {
		teken_pos_t tp;

		/*
		 * Perform line wrapping, if:
		 * - Autowrapping is enabled, and
		 *   - We're in the wrapped state at the last column, or
		 *   - The character to be printed does not fit anymore.
		 */
		if (t->t_cursor.tp_row == t->t_scrollreg.ts_end - 1) {
			/* Perform scrolling. */
			teken_subr_do_scroll(t, 1);
			tp.tp_row = t->t_scrollreg.ts_end - 1;
		} else {
			/* No scrolling needed. */
			tp.tp_row = t->t_cursor.tp_row + 1;
			if (tp.tp_row == t->t_winsize.tp_row) {
				/*
				 * Corner case: regular character
				 * outside scrolling region, but at the
				 * bottom of the screen.
				 */
				teken_subr_do_putchar(t, &t->t_cursor,
				    c, width);
				return;
			}
		}

		tp.tp_col = 0;
		teken_subr_do_putchar(t, &tp, c, width);

		t->t_cursor.tp_row = tp.tp_row;
		t->t_cursor.tp_col = width;
		t->t_stateflags &= ~TS_WRAPPED;
	} else {
		/* No line wrapping needed. */
		teken_subr_do_putchar(t, &t->t_cursor, c, width);
		t->t_cursor.tp_col += width;

		if (t->t_cursor.tp_col >= t->t_winsize.tp_col) {
			t->t_stateflags |= TS_WRAPPED;
			t->t_cursor.tp_col = t->t_winsize.tp_col - 1;
		} else {
			t->t_stateflags &= ~TS_WRAPPED;
		}
	}

	teken_funcs_cursor(t);
}

static void
teken_subr_reset_dec_mode(teken_t *t, unsigned int cmd)
{

	switch (cmd) {
	case 1: /* Cursor keys mode. */
		t->t_stateflags &= ~TS_CURSORKEYS;
		break;
	case 2: /* DECANM: ANSI/VT52 mode. */
		teken_printf("DECRST VT52\n");
		break;
	case 3: /* 132 column mode. */
		teken_funcs_param(t, TP_132COLS, 0);
		teken_subr_reset_to_initial_state(t);
		break;
	case 5: /* Inverse video. */
		teken_printf("DECRST inverse video\n");
		break;
	case 6: /* Origin mode. */
		t->t_stateflags &= ~TS_ORIGIN;
		t->t_originreg.ts_begin = 0;
		t->t_originreg.ts_end = t->t_winsize.tp_row;
		t->t_cursor.tp_row = t->t_cursor.tp_col = 0;
		t->t_stateflags &= ~TS_WRAPPED;
		teken_funcs_cursor(t);
		break;
	case 7: /* Autowrap mode. */
		t->t_stateflags &= ~TS_AUTOWRAP;
		break;
	case 8: /* Autorepeat mode. */
		teken_funcs_param(t, TP_AUTOREPEAT, 0);
		break;
	case 25: /* Hide cursor. */
		teken_funcs_param(t, TP_SHOWCURSOR, 0);
		break;
	case 40: /* Disallow 132 columns. */
		teken_printf("DECRST allow 132\n");
		break;
	case 45: /* Disable reverse wraparound. */
		teken_printf("DECRST reverse wraparound\n");
		break;
	case 47: /* Switch to alternate buffer. */
		teken_printf("Switch to alternate buffer\n");
		break;
	case 1000: /* Mouse input. */
		teken_funcs_param(t, TP_MOUSE, 0);
		break;
	default:
		teken_printf("Unknown DECRST: %u\n", cmd);
	}
}

static void
teken_subr_reset_mode(teken_t *t, unsigned int cmd)
{

	switch (cmd) {
	case 4:
		t->t_stateflags &= ~TS_INSERT;
		break;
	default:
		teken_printf("Unknown reset mode: %u\n", cmd);
	}
}

static void
teken_subr_do_resize(teken_t *t)
{

	t->t_scrollreg.ts_begin = 0;
	t->t_scrollreg.ts_end = t->t_winsize.tp_row;
	t->t_originreg = t->t_scrollreg;
}

static void
teken_subr_do_reset(teken_t *t)
{

	t->t_curattr = t->t_defattr;
	t->t_cursor.tp_row = t->t_cursor.tp_col = 0;
	t->t_scrollreg.ts_begin = 0;
	t->t_scrollreg.ts_end = t->t_winsize.tp_row;
	t->t_originreg = t->t_scrollreg;
	t->t_stateflags &= TS_8BIT | TS_CONS25 | TS_CONS25KEYS;
	t->t_stateflags |= TS_AUTOWRAP;

	t->t_scs[0] = teken_scs_us_ascii;
	t->t_scs[1] = teken_scs_us_ascii;
	t->t_curscs = 0;

	teken_subr_save_cursor(t);
	teken_tab_default(t);
}

static void
teken_subr_reset_to_initial_state(teken_t *t)
{

	teken_subr_do_reset(t);
	teken_subr_erase_display(t, 2);
	teken_funcs_param(t, TP_SHOWCURSOR, 1);
	teken_funcs_cursor(t);
}

static void
teken_subr_restore_cursor(teken_t *t)
{

	t->t_cursor = t->t_saved_cursor;
	t->t_curattr = t->t_saved_curattr;
	t->t_scs[t->t_curscs] = t->t_saved_curscs;
	t->t_stateflags &= ~TS_WRAPPED;

	/* Get out of origin mode when the cursor is moved outside. */
	if (t->t_cursor.tp_row < t->t_originreg.ts_begin ||
	    t->t_cursor.tp_row >= t->t_originreg.ts_end) {
		t->t_stateflags &= ~TS_ORIGIN;
		t->t_originreg.ts_begin = 0;
		t->t_originreg.ts_end = t->t_winsize.tp_row;
	}

	teken_funcs_cursor(t);
}

static void
teken_subr_reverse_index(teken_t *t)
{

	if (t->t_cursor.tp_row > t->t_scrollreg.ts_begin) {
		t->t_cursor.tp_row--;
		t->t_stateflags &= ~TS_WRAPPED;
		teken_funcs_cursor(t);
	} else {
		teken_subr_do_scroll(t, -1);
	}
}

static void
teken_subr_save_cursor(teken_t *t)
{

	t->t_saved_cursor = t->t_cursor;
	t->t_saved_curattr = t->t_curattr;
	t->t_saved_curscs = t->t_scs[t->t_curscs];
}

static void
teken_subr_secondary_device_attributes(const teken_t *t, unsigned int request)
{

	if (request == 0) {
		const char response[] = "\x1B[>0;10;0c";
		teken_funcs_respond(t, response, sizeof response - 1);
	} else {
		teken_printf("Unknown DA2\n");
	}
}

static void
teken_subr_set_dec_mode(teken_t *t, unsigned int cmd)
{

	switch (cmd) {
	case 1: /* Cursor keys mode. */
		t->t_stateflags |= TS_CURSORKEYS;
		break;
	case 2: /* DECANM: ANSI/VT52 mode. */
		teken_printf("DECSET VT52\n");
		break;
	case 3: /* 132 column mode. */
		teken_funcs_param(t, TP_132COLS, 1);
		teken_subr_reset_to_initial_state(t);
		break;
	case 5: /* Inverse video. */
		teken_printf("DECSET inverse video\n");
		break;
	case 6: /* Origin mode. */
		t->t_stateflags |= TS_ORIGIN;
		t->t_originreg = t->t_scrollreg;
		t->t_cursor.tp_row = t->t_scrollreg.ts_begin;
		t->t_cursor.tp_col = 0;
		t->t_stateflags &= ~TS_WRAPPED;
		teken_funcs_cursor(t);
		break;
	case 7: /* Autowrap mode. */
		t->t_stateflags |= TS_AUTOWRAP;
		break;
	case 8: /* Autorepeat mode. */
		teken_funcs_param(t, TP_AUTOREPEAT, 1);
		break;
	case 25: /* Display cursor. */
		teken_funcs_param(t, TP_SHOWCURSOR, 1);
		break;
	case 40: /* Allow 132 columns. */
		teken_printf("DECSET allow 132\n");
		break;
	case 45: /* Enable reverse wraparound. */
		teken_printf("DECSET reverse wraparound\n");
		break;
	case 47: /* Switch to alternate buffer. */
		teken_printf("Switch away from alternate buffer\n");
		break;
	case 1000: /* Mouse input. */
		teken_funcs_param(t, TP_MOUSE, 1);
		break;
	default:
		teken_printf("Unknown DECSET: %u\n", cmd);
	}
}

static void
teken_subr_set_mode(teken_t *t, unsigned int cmd)
{

	switch (cmd) {
	case 4:
		teken_printf("Insert mode\n");
		t->t_stateflags |= TS_INSERT;
		break;
	default:
		teken_printf("Unknown set mode: %u\n", cmd);
	}
}

static void
teken_subr_set_graphic_rendition(teken_t *t, unsigned int ncmds,
    const unsigned int cmds[])
{
	unsigned int i, n;

	/* No attributes means reset. */
	if (ncmds == 0) {
		t->t_curattr = t->t_defattr;
		return;
	}

	for (i = 0; i < ncmds; i++) {
		n = cmds[i];

		switch (n) {
		case 0: /* Reset. */
			t->t_curattr = t->t_defattr;
			break;
		case 1: /* Bold. */
			t->t_curattr.ta_format |= TF_BOLD;
			break;
		case 4: /* Underline. */
			t->t_curattr.ta_format |= TF_UNDERLINE;
			break;
		case 5: /* Blink. */
			t->t_curattr.ta_format |= TF_BLINK;
			break;
		case 7: /* Reverse. */
			t->t_curattr.ta_format |= TF_REVERSE;
			break;
		case 22: /* Remove bold. */
			t->t_curattr.ta_format &= ~TF_BOLD;
			break;
		case 24: /* Remove underline. */
			t->t_curattr.ta_format &= ~TF_UNDERLINE;
			break;
		case 25: /* Remove blink. */
			t->t_curattr.ta_format &= ~TF_BLINK;
			break;
		case 27: /* Remove reverse. */
			t->t_curattr.ta_format &= ~TF_REVERSE;
			break;
		case 30: /* Set foreground color: black */
		case 31: /* Set foreground color: red */
		case 32: /* Set foreground color: green */
		case 33: /* Set foreground color: brown */
		case 34: /* Set foreground color: blue */
		case 35: /* Set foreground color: magenta */
		case 36: /* Set foreground color: cyan */
		case 37: /* Set foreground color: white */
			t->t_curattr.ta_fgcolor = n - 30;
			break;
		case 38: /* Set foreground color: 256 color mode */
			if (i + 2 >= ncmds || cmds[i + 1] != 5)
				continue;
			t->t_curattr.ta_fgcolor = cmds[i + 2];
			i += 2;
			break;
		case 39: /* Set default foreground color. */
			t->t_curattr.ta_fgcolor = t->t_defattr.ta_fgcolor;
			break;
		case 40: /* Set background color: black */
		case 41: /* Set background color: red */
		case 42: /* Set background color: green */
		case 43: /* Set background color: brown */
		case 44: /* Set background color: blue */
		case 45: /* Set background color: magenta */
		case 46: /* Set background color: cyan */
		case 47: /* Set background color: white */
			t->t_curattr.ta_bgcolor = n - 40;
			break;
		case 48: /* Set background color: 256 color mode */
			if (i + 2 >= ncmds || cmds[i + 1] != 5)
				continue;
			t->t_curattr.ta_bgcolor = cmds[i + 2];
			i += 2;
			break;
		case 49: /* Set default background color. */
			t->t_curattr.ta_bgcolor = t->t_defattr.ta_bgcolor;
			break;
		case 90: /* Set bright foreground color: black */
		case 91: /* Set bright foreground color: red */
		case 92: /* Set bright foreground color: green */
		case 93: /* Set bright foreground color: brown */
		case 94: /* Set bright foreground color: blue */
		case 95: /* Set bright foreground color: magenta */
		case 96: /* Set bright foreground color: cyan */
		case 97: /* Set bright foreground color: white */
			t->t_curattr.ta_fgcolor = (n - 90) + 8;
			break;
		case 100: /* Set bright background color: black */
		case 101: /* Set bright background color: red */
		case 102: /* Set bright background color: green */
		case 103: /* Set bright background color: brown */
		case 104: /* Set bright background color: blue */
		case 105: /* Set bright background color: magenta */
		case 106: /* Set bright background color: cyan */
		case 107: /* Set bright background color: white */
			t->t_curattr.ta_bgcolor = (n - 100) + 8;
			break;
		default:
			teken_printf("unsupported attribute %u\n", n);
		}
	}
}

static void
teken_subr_set_top_and_bottom_margins(teken_t *t, unsigned int top,
    unsigned int bottom)
{

	/* Adjust top row number. */
	if (top > 0)
		top--;
	/* Adjust bottom row number. */
	if (bottom == 0 || bottom > t->t_winsize.tp_row)
		bottom = t->t_winsize.tp_row;

	/* Invalid arguments. */
	if (top >= bottom - 1) {
		top = 0;
		bottom = t->t_winsize.tp_row;
	}

	/* Apply scrolling region. */
	t->t_scrollreg.ts_begin = top;
	t->t_scrollreg.ts_end = bottom;
	if (t->t_stateflags & TS_ORIGIN)
		t->t_originreg = t->t_scrollreg;

	/* Home cursor to the top left of the scrolling region. */
	t->t_cursor.tp_row = t->t_originreg.ts_begin;
	t->t_cursor.tp_col = 0;
	t->t_stateflags &= ~TS_WRAPPED;
	teken_funcs_cursor(t);
}

static void
teken_subr_single_height_double_width_line(const teken_t *t)
{

	(void)t;
	teken_printf("single height double width???\n");
}

static void
teken_subr_single_height_single_width_line(const teken_t *t)
{

	(void)t;
	teken_printf("single height single width???\n");
}

static void
teken_subr_string_terminator(const teken_t *t)
{

	(void)t;
	/*
	 * Strings are already terminated in teken_input_char() when ^[
	 * is inserted.
	 */
}

static void
teken_subr_tab_clear(teken_t *t, unsigned int cmd)
{

	switch (cmd) {
	case 0:
		teken_tab_clear(t, t->t_cursor.tp_col);
		break;
	case 3:
		memset(t->t_tabstops, 0, T_NUMCOL / 8);
		break;
	default:
		break;
	}
}

static void
teken_subr_vertical_position_absolute(teken_t *t, unsigned int row)
{

	row = (row - 1) + t->t_originreg.ts_begin;
	t->t_cursor.tp_row = row < t->t_originreg.ts_end ?
	    row : t->t_originreg.ts_end - 1;

	t->t_stateflags &= ~TS_WRAPPED;
	teken_funcs_cursor(t);
}

static void
teken_subr_repeat_last_graphic_char(teken_t *t, unsigned int rpts)
{
	unsigned int max_repetitions;

	max_repetitions = t->t_winsize.tp_row * t->t_winsize.tp_col;
	if (rpts > max_repetitions)
		rpts = max_repetitions;
	for (; t->t_last != 0 && rpts > 0; rpts--)
		teken_subr_regular_character(t, t->t_last);
}
