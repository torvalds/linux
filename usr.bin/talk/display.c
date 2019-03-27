/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>

__FBSDID("$FreeBSD$");

#ifndef lint
static const char sccsid[] = "@(#)display.c	8.1 (Berkeley) 6/6/93";
#endif

/*
 * The window 'manager', initializes curses and handles the actual
 * displaying of text
 */
#include <ctype.h>
#include <unistd.h>
#include <wctype.h>
#define _XOPEN_SOURCE_EXTENDED
#include <curses.h>

#include "talk.h"

void	display(xwin_t *, wchar_t *);

xwin_t	my_win;
xwin_t	his_win;
WINDOW	*line_win;

int	curses_initialized = 0;

/*
 * max HAS to be a function, it is called with
 * an argument of the form --foo at least once.
 */
int
max(int a, int b)
{

	return (a > b ? a : b);
}

static cchar_t *
makecchar(wchar_t in)
{
	static cchar_t cc;
	wchar_t wc[2];

	wc[0] = in;
	wc[1] = L'\0';

	if (setcchar(&cc, wc, A_NORMAL, 0, NULL) != OK)
		p_error("settchar(3) failure");

	return (&cc);
}

/*
 * Display a symbol on somebody's window, processing some control
 * characters while we are at it.
 */
void
display(xwin_t *win, wchar_t *wc)
{

	/*
	 * Alas, can't use variables in C switch statement.
	 * Workaround these 3 cases with goto.
	 */
	if (*wc == win->kill)
		goto kill;
	else if (*wc == win->cerase)
		goto cerase;
	else if (*wc == win->werase)
		goto werase;

	switch (*wc) {
	case L'\n':
	case L'\r':
		wadd_wch(win->x_win, makecchar(L'\n'));
		getyx(win->x_win, win->x_line, win->x_col);
		wrefresh(win->x_win);
		return;

	case 004:
		if (win == &my_win) {
			/* Ctrl-D clears the screen. */
			werase(my_win.x_win);
			getyx(my_win.x_win, my_win.x_line, my_win.x_col);
			wrefresh(my_win.x_win);
			werase(his_win.x_win);
			getyx(his_win.x_win, his_win.x_line, his_win.x_col);
			wrefresh(his_win.x_win);
		}
		return;

	/* Erase character. */
	case 010:	/* BS */
	case 0177:	/* DEL */
cerase:
		wmove(win->x_win, win->x_line, max(--win->x_col, 0));
		getyx(win->x_win, win->x_line, win->x_col);
		waddch(win->x_win, ' ');
		wmove(win->x_win, win->x_line, win->x_col);
		getyx(win->x_win, win->x_line, win->x_col);
		wrefresh(win->x_win);
		return;

	case 027:	/* ^W */
werase:
	    {
		/*
		 * On word erase search backwards until we find
		 * the beginning of a word or the beginning of
		 * the line.
		 */
		int endcol, xcol, c;

		endcol = win->x_col;
		xcol = endcol - 1;
		while (xcol >= 0) {
			c = readwin(win->x_win, win->x_line, xcol);
			if (c != ' ')
				break;
			xcol--;
		}
		while (xcol >= 0) {
			c = readwin(win->x_win, win->x_line, xcol);
			if (c == ' ')
				break;
			xcol--;
		}
		wmove(win->x_win, win->x_line, xcol + 1);
		for (int i = xcol + 1; i < endcol; i++)
			waddch(win->x_win, ' ');
		wmove(win->x_win, win->x_line, xcol + 1);
		getyx(win->x_win, win->x_line, win->x_col);
		wrefresh(win->x_win);
		return;
	    }

	case 025:	/* ^U */
kill:
		wmove(win->x_win, win->x_line, 0);
		wclrtoeol(win->x_win);
		getyx(win->x_win, win->x_line, win->x_col);
		wrefresh(win->x_win);
		return;

	case L'\f':
		if (win == &my_win)
			wrefresh(curscr);
		return;

	case L'\7':
		write(STDOUT_FILENO, wc, sizeof(*wc));
		return;
	}


	if (iswprint(*wc) || *wc == L'\t')
		wadd_wch(win->x_win, makecchar(*wc));
	else
		beep();

	getyx(win->x_win, win->x_line, win->x_col);
	wrefresh(win->x_win);
}

/*
 * Read the character at the indicated position in win
 */
int
readwin(WINDOW *win, int line, int col)
{
	int oldline, oldcol;
	int c;

	getyx(win, oldline, oldcol);
	wmove(win, line, col);
	c = winch(win);
	wmove(win, oldline, oldcol);
	return (c);
}
