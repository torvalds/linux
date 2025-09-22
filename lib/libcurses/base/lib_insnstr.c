/* $OpenBSD: lib_insnstr.c,v 1.2 2023/10/17 09:52:08 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2020,2022 Thomas E. Dickey                                *
 * Copyright 2004-2009,2016 Free Software Foundation, Inc.                  *
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
**	lib_insnstr.c
**
**	The routine winsnstr().
**
*/

#include <curses.priv.h>
#include <ctype.h>

MODULE_ID("$Id: lib_insnstr.c,v 1.2 2023/10/17 09:52:08 nicm Exp $")

NCURSES_EXPORT(int)
winsnstr(WINDOW *win, const char *s, int n)
{
    int code = ERR;
    const unsigned char *str = (const unsigned char *) s;

    T((T_CALLED("winsnstr(%p,%s,%d)"), (void *) win, _nc_visbufn(s, n), n));

    if (win != 0 && str != 0) {
	SCREEN *sp = _nc_screen_of(win);
#if USE_WIDEC_SUPPORT
	/*
	 * If the output contains "wide" (multibyte) characters, we will not
	 * really know the width of a character until we get the last byte
	 * of the character.  Since the preceding byte(s) may use more columns
	 * on the screen than the final character, it is best to route the
	 * call to the wins_nwstr() function.
	 */
	if (sp->_screen_unicode) {
	    size_t nn = (n > 0) ? (size_t) n : strlen(s);
	    wchar_t *buffer = typeMalloc(wchar_t, nn + 1);
	    if (buffer != 0) {
		mbstate_t state;
		size_t n3;
		init_mb(state);
		n3 = mbstowcs(buffer, s, nn);
		if (n3 != (size_t) (-1)) {
		    buffer[n3] = '\0';
		    code = wins_nwstr(win, buffer, (int) n3);
		}
		free(buffer);
	    }
	}
	if (code == ERR)
#endif
	{
	    NCURSES_SIZE_T oy = win->_cury;
	    NCURSES_SIZE_T ox = win->_curx;
	    const unsigned char *cp;

	    for (cp = str; (n <= 0 || (cp - str) < n) && *cp; cp++) {
		_nc_insert_ch(sp, win, (chtype) UChar(*cp));
	    }
	    win->_curx = ox;
	    win->_cury = oy;
	    _nc_synchook(win);
	    code = OK;
	}
    }
    returnCode(code);
}
