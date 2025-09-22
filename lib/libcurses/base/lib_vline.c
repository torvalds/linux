/* $OpenBSD: lib_vline.c,v 1.5 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2020 Thomas E. Dickey                                          *
 * Copyright 1998-2016,2017 Free Software Foundation, Inc.                  *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 *     and: Sven Verdoolaege                        2001                    *
 ****************************************************************************/

/*
**	lib_vline.c
**
**	The routine wvline().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_vline.c,v 1.5 2023/10/17 09:52:09 nicm Exp $")

NCURSES_EXPORT(int)
wvline(WINDOW *win, chtype ch, int n)
{
    int code = ERR;

    T((T_CALLED("wvline(%p,%s,%d)"), (void *) win, _tracechtype(ch), n));

    if (win) {
	NCURSES_CH_T wch;
	int row = win->_cury;
	int col = win->_curx;
	int end = row + n - 1;

	if (end > win->_maxy)
	    end = win->_maxy;

	if (ch == 0)
	    SetChar2(wch, ACS_VLINE);
	else
	    SetChar2(wch, ch);
	wch = _nc_render(win, wch);

	while (end >= row) {
	    struct ldat *line = &(win->_line[end]);
#if USE_WIDEC_SUPPORT
	    if (col > 0 && isWidecExt(line->text[col])) {
		SetChar2(line->text[col - 1], ' ');
	    }
	    if (col < win->_maxx && isWidecExt(line->text[col + 1])) {
		SetChar2(line->text[col + 1], ' ');
	    }
#endif
	    line->text[col] = wch;
	    CHANGED_CELL(line, col);
	    end--;
	}

	_nc_synchook(win);
	code = OK;
    }
    returnCode(code);
}
