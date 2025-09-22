/* $OpenBSD: lib_scanw.c,v 1.4 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2019,2020 Thomas E. Dickey                                *
 * Copyright 1998-2009,2011 Free Software Foundation, Inc.                  *
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
 ****************************************************************************/

/*
**	lib_scanw.c
**
**	The routines scanw(), wscanw() and friends.
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_scanw.c,v 1.4 2023/10/17 09:52:09 nicm Exp $")

NCURSES_EXPORT(int)
vwscanw(WINDOW *win, const char *fmt, va_list argp)
{
    char buf[BUFSIZ];
    int code = ERR;

    if (wgetnstr(win, buf, (int) sizeof(buf) - 1) != ERR) {
	if ((code = vsscanf(buf, fmt, argp)) == EOF) {
	    code = ERR;
	}
    }

    return code;
}

NCURSES_EXPORT(int)
vw_scanw(WINDOW *win, const char *fmt, va_list argp)
{
    char buf[BUFSIZ];
    int code = ERR;

    if (wgetnstr(win, buf, (int) sizeof(buf) - 1) != ERR) {
	if ((code = vsscanf(buf, fmt, argp)) == EOF) {
	    code = ERR;
	}
    }

    return code;
}

NCURSES_EXPORT(int)
scanw(const char *fmt, ...)
{
    int code;
    va_list ap;

    T(("scanw(\"%s\",...) called", fmt));

    va_start(ap, fmt);
    code = vw_scanw(stdscr, fmt, ap);
    va_end(ap);
    return (code);
}

NCURSES_EXPORT(int)
wscanw(WINDOW *win, const char *fmt, ...)
{
    int code;
    va_list ap;

    T(("wscanw(%p,\"%s\",...) called", (void *) win, fmt));

    va_start(ap, fmt);
    code = vw_scanw(win, fmt, ap);
    va_end(ap);
    return (code);
}

NCURSES_EXPORT(int)
mvscanw(int y, int x, const char *fmt, ...)
{
    int code;
    va_list ap;

    va_start(ap, fmt);
    code = (move(y, x) == OK) ? vw_scanw(stdscr, fmt, ap) : ERR;
    va_end(ap);
    return (code);
}

NCURSES_EXPORT(int)
mvwscanw(WINDOW *win, int y, int x, const char *fmt, ...)
{
    int code;
    va_list ap;

    va_start(ap, fmt);
    code = (wmove(win, y, x) == OK) ? vw_scanw(win, fmt, ap) : ERR;
    va_end(ap);
    return (code);
}
