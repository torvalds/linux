/* $OpenBSD: lib_erasewchar.c,v 1.2 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2020,2021 Thomas E. Dickey                                     *
 * Copyright 2002-2010,2014 Free Software Foundation, Inc.                  *
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
 *  Author: Thomas E. Dickey 2002                                           *
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: lib_erasewchar.c,v 1.2 2023/10/17 09:52:09 nicm Exp $")

/*
 *	erasewchar()
 *
 *	Return erase character as given in cur_term->Ottyb.
 */

NCURSES_EXPORT(int)
NCURSES_SP_NAME(erasewchar) (NCURSES_SP_DCLx wchar_t *wch);
NCURSES_EXPORT(int)
NCURSES_SP_NAME(erasewchar) (NCURSES_SP_DCLx wchar_t *wch)
{
    int value;
    int result = ERR;

    T((T_CALLED("erasewchar()")));
    if ((value = NCURSES_SP_NAME(erasechar) (NCURSES_SP_ARG)) != ERR) {
	*wch = (wchar_t) value;
	result = OK;
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
erasewchar(wchar_t *wch)
{
    return NCURSES_SP_NAME(erasewchar) (CURRENT_SCREEN, wch);
}
#endif

/*
 *	killwchar()
 *
 *	Return kill character as given in cur_term->Ottyb.
 */

NCURSES_EXPORT(int)
NCURSES_SP_NAME(killwchar) (NCURSES_SP_DCLx wchar_t *wch);
NCURSES_EXPORT(int)
NCURSES_SP_NAME(killwchar) (NCURSES_SP_DCLx wchar_t *wch)
{
    int value;
    int result = ERR;

    T((T_CALLED("killwchar()")));
    if ((value = NCURSES_SP_NAME(killchar) (NCURSES_SP_ARG)) != ERR) {
	*wch = (wchar_t) value;
	result = OK;
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
killwchar(wchar_t *wch)
{
    return NCURSES_SP_NAME(killwchar) (CURRENT_SCREEN, wch);
}
#endif
