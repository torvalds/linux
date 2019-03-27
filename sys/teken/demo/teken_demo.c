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

#include <sys/ioctl.h>

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <ncurses.h>
#if defined(__FreeBSD__)
#include <libutil.h>
#elif defined(__linux__)
#include <pty.h>
#else
#include <util.h>
#endif

#include <teken.h>

static tf_bell_t	test_bell;
static tf_cursor_t	test_cursor;
static tf_putchar_t	test_putchar;
static tf_fill_t	test_fill;
static tf_copy_t	test_copy;
static tf_param_t	test_param;
static tf_respond_t	test_respond;

static teken_funcs_t tf = {
	.tf_bell	= test_bell,
	.tf_cursor	= test_cursor,
	.tf_putchar	= test_putchar,
	.tf_fill	= test_fill,
	.tf_copy	= test_copy,
	.tf_param	= test_param,
	.tf_respond	= test_respond,
};

struct pixel {
	teken_char_t	c;
	teken_attr_t	a;
};

#define NCOLS	80
#define NROWS	24
static struct pixel buffer[NCOLS][NROWS];

static int ptfd;

static void
printchar(const teken_pos_t *p)
{
	int y, x, attr = 0;
	struct pixel *px;
	char str[5] = { 0 };

	assert(p->tp_row < NROWS);
	assert(p->tp_col < NCOLS);

	px = &buffer[p->tp_col][p->tp_row];
	/* No need to print right hand side of CJK character manually. */
	if (px->a.ta_format & TF_CJK_RIGHT)
		return;

	/* Convert Unicode to UTF-8. */
	if (px->c < 0x80) {
		str[0] = px->c;
	} else if (px->c < 0x800) {
		str[0] = 0xc0 | (px->c >> 6);
		str[1] = 0x80 | (px->c & 0x3f);
	} else if (px->c < 0x10000) {
		str[0] = 0xe0 | (px->c >> 12);
		str[1] = 0x80 | ((px->c >> 6) & 0x3f);
		str[2] = 0x80 | (px->c & 0x3f);
	} else {
		str[0] = 0xf0 | (px->c >> 18);
		str[1] = 0x80 | ((px->c >> 12) & 0x3f);
		str[2] = 0x80 | ((px->c >> 6) & 0x3f);
		str[3] = 0x80 | (px->c & 0x3f);
	}

	if (px->a.ta_format & TF_BOLD)
		attr |= A_BOLD;
	if (px->a.ta_format & TF_UNDERLINE)
		attr |= A_UNDERLINE;
	if (px->a.ta_format & TF_BLINK)
		attr |= A_BLINK;
	if (px->a.ta_format & TF_REVERSE)
		attr |= A_REVERSE;

	bkgdset(attr | COLOR_PAIR(teken_256to8(px->a.ta_fgcolor) +
	      8 * teken_256to8(px->a.ta_bgcolor)));
	getyx(stdscr, y, x);
	mvaddstr(p->tp_row, p->tp_col, str);
	move(y, x);
}

static void
test_bell(void *s __unused)
{

	beep();
}

static void
test_cursor(void *s __unused, const teken_pos_t *p)
{

	move(p->tp_row, p->tp_col);
}

static void
test_putchar(void *s __unused, const teken_pos_t *p, teken_char_t c,
    const teken_attr_t *a)
{

	buffer[p->tp_col][p->tp_row].c = c;
	buffer[p->tp_col][p->tp_row].a = *a;
	printchar(p);
}

static void
test_fill(void *s, const teken_rect_t *r, teken_char_t c,
    const teken_attr_t *a)
{
	teken_pos_t p;

	/* Braindead implementation of fill() - just call putchar(). */
	for (p.tp_row = r->tr_begin.tp_row; p.tp_row < r->tr_end.tp_row; p.tp_row++)
		for (p.tp_col = r->tr_begin.tp_col; p.tp_col < r->tr_end.tp_col; p.tp_col++)
			test_putchar(s, &p, c, a);
}

static void
test_copy(void *s __unused, const teken_rect_t *r, const teken_pos_t *p)
{
	int nrow, ncol, x, y; /* Has to be signed - >= 0 comparison */
	teken_pos_t d;

	/*
	 * Copying is a little tricky. We must make sure we do it in
	 * correct order, to make sure we don't overwrite our own data.
	 */

	nrow = r->tr_end.tp_row - r->tr_begin.tp_row;
	ncol = r->tr_end.tp_col - r->tr_begin.tp_col;

	if (p->tp_row < r->tr_begin.tp_row) {
		/* Copy from top to bottom. */
		if (p->tp_col < r->tr_begin.tp_col) {
			/* Copy from left to right. */
			for (y = 0; y < nrow; y++) {
				d.tp_row = p->tp_row + y;
				for (x = 0; x < ncol; x++) {
					d.tp_col = p->tp_col + x;
					buffer[d.tp_col][d.tp_row] =
					    buffer[r->tr_begin.tp_col + x][r->tr_begin.tp_row + y];
					printchar(&d);
				}
			}
		} else {
			/* Copy from right to left. */
			for (y = 0; y < nrow; y++) {
				d.tp_row = p->tp_row + y;
				for (x = ncol - 1; x >= 0; x--) {
					d.tp_col = p->tp_col + x;
					buffer[d.tp_col][d.tp_row] =
					    buffer[r->tr_begin.tp_col + x][r->tr_begin.tp_row + y];
					printchar(&d);
				}
			}
		}
	} else {
		/* Copy from bottom to top. */
		if (p->tp_col < r->tr_begin.tp_col) {
			/* Copy from left to right. */
			for (y = nrow - 1; y >= 0; y--) {
				d.tp_row = p->tp_row + y;
				for (x = 0; x < ncol; x++) {
					d.tp_col = p->tp_col + x;
					buffer[d.tp_col][d.tp_row] =
					    buffer[r->tr_begin.tp_col + x][r->tr_begin.tp_row + y];
					printchar(&d);
				}
			}
		} else {
			/* Copy from right to left. */
			for (y = nrow - 1; y >= 0; y--) {
				d.tp_row = p->tp_row + y;
				for (x = ncol - 1; x >= 0; x--) {
					d.tp_col = p->tp_col + x;
					buffer[d.tp_col][d.tp_row] =
					    buffer[r->tr_begin.tp_col + x][r->tr_begin.tp_row + y];
					printchar(&d);
				}
			}
		}
	}
}

static void
test_param(void *s __unused, int cmd, unsigned int value)
{

	switch (cmd) {
	case TP_SHOWCURSOR:
		curs_set(value);
		break;
	case TP_KEYPADAPP:
		keypad(stdscr, value ? TRUE : FALSE);
		break;
	}
}

static void
test_respond(void *s __unused, const void *buf, size_t len)
{

	write(ptfd, buf, len);
}

static void
redraw_border(void)
{
	unsigned int i;

	for (i = 0; i < NROWS; i++)
		mvaddch(i, NCOLS, '|');
	for (i = 0; i < NCOLS; i++)
		mvaddch(NROWS, i, '-');

	mvaddch(NROWS, NCOLS, '+');
}

static void
redraw_all(void)
{
	teken_pos_t tp;

	for (tp.tp_row = 0; tp.tp_row < NROWS; tp.tp_row++)
		for (tp.tp_col = 0; tp.tp_col < NCOLS; tp.tp_col++)
			printchar(&tp);

	redraw_border();
}

int
main(int argc __unused, char *argv[] __unused)
{
	struct winsize ws;
	teken_t t;
	teken_pos_t tp;
	fd_set rfds;
	char b[256];
	ssize_t bl;
	const int ccolors[8] = {
	    COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
	    COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
	};
	int i, j;

	setlocale(LC_CTYPE, "UTF-8");

	tp.tp_row = ws.ws_row = NROWS;
	tp.tp_col = ws.ws_col = NCOLS;

	switch (forkpty(&ptfd, NULL, NULL, &ws)) {
	case -1:
		perror("forkpty");
		exit(1);
	case 0:
		setenv("TERM", "xterm", 1);
		setenv("LC_CTYPE", "UTF-8", 0);
		execlp("zsh", "-zsh", NULL);
		execlp("bash", "-bash", NULL);
		execlp("sh", "-sh", NULL);
		_exit(1);
	}

	teken_init(&t, &tf, NULL);
	teken_set_winsize(&t, &tp);

	initscr();
	raw();
	start_color();
	for (i = 0; i < 8; i++)
		for (j = 0; j < 8; j++)
			init_pair(i + 8 * j, ccolors[i], ccolors[j]);

	redraw_border();

	FD_ZERO(&rfds);

	for (;;) {
		FD_SET(STDIN_FILENO, &rfds);
		FD_SET(ptfd, &rfds);

		if (select(ptfd + 1, &rfds, NULL, NULL, NULL) < 0) {
			if (errno == EINTR) {
				redraw_all();
				refresh();
				continue;
			}
			break;
		}

		if (FD_ISSET(STDIN_FILENO, &rfds)) {
			bl = read(STDIN_FILENO, b, sizeof b);
			if (bl <= 0)
				break;
			write(ptfd, b, bl);
		}

		if (FD_ISSET(ptfd, &rfds)) {
			bl = read(ptfd, b, sizeof b);
			if (bl <= 0)
				break;
			teken_input(&t, b, bl);
			refresh();
		}
	}

	endwin();

	return (0);
}
