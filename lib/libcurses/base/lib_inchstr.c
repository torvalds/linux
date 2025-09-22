/* $OpenBSD: lib_inchstr.c,v 1.4 2023/10/17 09:52:08 nicm Exp $ */

/****************************************************************************
 * Copyright 2020 Thomas E. Dickey                                          *
 * Copyright 1998-2010,2017 Free Software Foundation, Inc.                  *
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
 ****************************************************************************/

/*
**	lib_inchstr.c
**
**	The routine winchnstr().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_inchstr.c,v 1.4 2023/10/17 09:52:08 nicm Exp $")

NCURSES_EXPORT(int)
winchnstr(WINDOW *win, chtype *str, int n)
{
    int i = 0;

    T((T_CALLED("winchnstr(%p,%p,%d)"), (void *) win, (void *) str, n));

    if (!win || !str) {
	i = ERR;
    } else {
	int row = win->_cury;
	int col = win->_curx;
	NCURSES_CH_T *text = win->_line[row].text;

	for (; (n < 0 || (i < n)) && (col + i <= win->_maxx); i++) {
	    str[i] = (((chtype) CharOf(text[col + i]) & A_CHARTEXT) |
		      AttrOf(text[col + i]));
	}
	str[i] = (chtype) 0;
    }

    returnCode(i);
}
