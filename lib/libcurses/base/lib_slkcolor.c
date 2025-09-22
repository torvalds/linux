/* $OpenBSD: lib_slkcolor.c,v 1.5 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2018,2020 Thomas E. Dickey                                     *
 * Copyright 1998-2014,2017 Free Software Foundation, Inc.                  *
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
 *  Author:  Juergen Pfeifer, 1998,2009                                     *
 *     and:  Thomas E. Dickey 2005-on                                       *
 ****************************************************************************/

/*
 *	lib_slkcolor.c
 *	Soft key routines.
 *	Set the label's color
 */
#include <curses.priv.h>

MODULE_ID("$Id: lib_slkcolor.c,v 1.5 2023/10/17 09:52:09 nicm Exp $")

static int
_nc_slk_color(SCREEN *sp, int pair_arg)
{
    int code = ERR;

    T((T_CALLED("slk_color(%p,%d)"), (void *) sp, pair_arg));

    if (sp != 0
	&& sp->_slk != 0
	&& pair_arg >= 0
	&& pair_arg < sp->_pair_limit) {
	TR(TRACE_ATTRS, ("... current is %s", _tracech_t(CHREF(sp->_slk->attr))));
	SetPair(sp->_slk->attr, pair_arg);
	TR(TRACE_ATTRS, ("new attribute is %s", _tracech_t(CHREF(sp->_slk->attr))));
	code = OK;
    }
    returnCode(code);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(slk_color) (NCURSES_SP_DCLx NCURSES_PAIRS_T pair_arg)
{
    return _nc_slk_color(SP_PARM, pair_arg);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
slk_color(NCURSES_PAIRS_T pair_arg)
{
    return NCURSES_SP_NAME(slk_color) (CURRENT_SCREEN, pair_arg);
}
#endif

#if NCURSES_EXT_COLORS
NCURSES_EXPORT(int)
NCURSES_SP_NAME(extended_slk_color) (NCURSES_SP_DCLx int pair_arg)
{
    return _nc_slk_color(SP_PARM, pair_arg);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
extended_slk_color(int pair_arg)
{
    return NCURSES_SP_NAME(extended_slk_color) (CURRENT_SCREEN, pair_arg);
}
#endif
#endif
