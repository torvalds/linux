/* $OpenBSD: lib_get_wstr.c,v 1.2 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2021,2023 Thomas E. Dickey                                *
 * Copyright 2002-2009,2011 Free Software Foundation, Inc.                  *
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
 *  Author: Thomas E. Dickey                                                *
 ****************************************************************************/

/*
**	lib_get_wstr.c
**
**	The routine wgetn_wstr().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_get_wstr.c,v 1.2 2023/10/17 09:52:09 nicm Exp $")

static int
wadd_wint(WINDOW *win, wint_t *src)
{
    cchar_t tmp;
    wchar_t wch[2];

    wch[0] = (wchar_t) (*src);
    wch[1] = 0;
    setcchar(&tmp, wch, A_NORMAL, (short) 0, NULL);
    return wadd_wch(win, &tmp);
}

/*
 * This wipes out the last character, no matter whether it was a tab, control
 * or other character, and handles reverse wraparound.
 */
static wint_t *
WipeOut(WINDOW *win, int y, int x, wint_t *first, wint_t *last, int echoed)
{
    if (last > first) {
	*--last = '\0';
	if (echoed) {
	    int y1 = win->_cury;
	    int x1 = win->_curx;
	    int n;

	    wmove(win, y, x);
	    for (n = 0; first[n] != 0; ++n) {
		wadd_wint(win, first + n);
	    }
	    getyx(win, y, x);
	    while (win->_cury < y1
		   || (win->_cury == y1 && win->_curx < x1))
		waddch(win, (chtype) ' ');

	    wmove(win, y, x);
	}
    }
    return last;
}

NCURSES_EXPORT(int)
wgetn_wstr(WINDOW *win, wint_t *str, int maxlen)
{
    SCREEN *sp = _nc_screen_of(win);
    TTY buf;
    TTY_FLAGS save_flags;
    wchar_t erasec = 0;
    wchar_t killc = 0;
    wint_t *oldstr = str;
    wint_t *tmpstr = str;
    wint_t ch;
    int y, x, code;

    T((T_CALLED("wgetn_wstr(%p,%p, %d)"), (void *) win, (void *) str, maxlen));

    if (!win)
	returnCode(ERR);

    maxlen = _nc_getstr_limit(maxlen);

    _nc_get_tty_mode(&buf);

    save_flags = sp->_tty_flags;
    NCURSES_SP_NAME(nl) (NCURSES_SP_ARG);
    NCURSES_SP_NAME(noecho) (NCURSES_SP_ARG);
    if (!save_flags._raw)
	NCURSES_SP_NAME(cbreak) (NCURSES_SP_ARG);

    NCURSES_SP_NAME(erasewchar) (NCURSES_SP_ARGx &erasec);
    NCURSES_SP_NAME(killwchar) (NCURSES_SP_ARGx &killc);

    getyx(win, y, x);

    if (is_wintouched(win) || (win->_flags & _HASMOVED))
	wrefresh(win);

    while ((code = wget_wch(win, &ch)) != ERR) {
	/*
	 * Map special characters into key-codes.
	 */
	if (ch == '\r')
	    ch = '\n';
	if (ch == '\n') {
	    code = KEY_CODE_YES;
	    ch = KEY_ENTER;
	}
	if (ch != 0 && ch < KEY_MIN) {
	    if (ch == (wint_t) erasec) {
		ch = KEY_BACKSPACE;
		code = KEY_CODE_YES;
	    }
	    if (ch == (wint_t) killc) {
		ch = KEY_EOL;
		code = KEY_CODE_YES;
	    }
	}
	if (code == KEY_CODE_YES) {
	    /*
	     * Some terminals (the Wyse-50 is the most common) generate a \n
	     * from the down-arrow key.  With this logic, it is the user's
	     * choice whether to set kcud=\n for wget_wch(); terminating
	     * *getn_wstr() with \n should work either way.
	     */
	    if (ch == KEY_DOWN || ch == KEY_ENTER) {
		if (save_flags._echo == TRUE
		    && win->_cury == win->_maxy
		    && win->_scroll)
		    wechochar(win, (chtype) '\n');
		break;
	    }
	    if (ch == KEY_LEFT || ch == KEY_BACKSPACE) {
		if (tmpstr > oldstr) {
		    tmpstr = WipeOut(win, y, x, oldstr, tmpstr, save_flags._echo);
		}
	    } else if (ch == KEY_EOL) {
		while (tmpstr > oldstr) {
		    tmpstr = WipeOut(win, y, x, oldstr, tmpstr, save_flags._echo);
		}
	    } else {
		beep();
	    }
	} else if (tmpstr - oldstr >= maxlen) {
	    beep();
	} else {
	    *tmpstr++ = ch;
	    *tmpstr = 0;
	    if (save_flags._echo == TRUE) {
		int oldy = win->_cury;

		if (wadd_wint(win, tmpstr - 1) == ERR) {
		    /*
		     * We can't really use the lower-right corner for input,
		     * since it'll mess up bookkeeping for erases.
		     */
		    win->_flags &= ~_WRAPPED;
		    waddch(win, (chtype) ' ');
		    tmpstr = WipeOut(win, y, x, oldstr, tmpstr, save_flags._echo);
		    continue;
		} else if (IS_WRAPPED(win)) {
		    /*
		     * If the last waddch forced a wrap & scroll, adjust our
		     * reference point for erasures.
		     */
		    if (win->_scroll
			&& oldy == win->_maxy
			&& win->_cury == win->_maxy) {
			if (--y <= 0) {
			    y = 0;
			}
		    }
		    win->_flags &= ~_WRAPPED;
		}
		wrefresh(win);
	    }
	}
    }

    win->_curx = 0;
    win->_flags &= ~_WRAPPED;
    if (win->_cury < win->_maxy)
	win->_cury++;
    wrefresh(win);

    /* Restore with a single I/O call, to fix minor asymmetry between
     * raw/noraw, etc.
     */
    sp->_tty_flags = save_flags;
    (void) _nc_set_tty_mode(&buf);

    *tmpstr = 0;
    if (code == ERR) {
	if (tmpstr == oldstr) {
	    *tmpstr++ = WEOF;
	    *tmpstr = 0;
	}
	returnCode(ERR);
    }

    T(("wgetn_wstr returns %s", _nc_viswibuf(oldstr)));

    returnCode(OK);
}
