/* $OpenBSD: lib_slkatr_set.c,v 1.5 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2020 Thomas E. Dickey                                          *
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
 *  Author:  Juergen Pfeifer, 1998                                          *
 *     and:  Thomas E. Dickey 2005-on                                       *
 ****************************************************************************/

/*
 *	lib_slkatr_set.c
 *	Soft key routines.
 *	Set the label's attributes
 */
#include <curses.priv.h>

MODULE_ID("$Id: lib_slkatr_set.c,v 1.5 2023/10/17 09:52:09 nicm Exp $")

NCURSES_EXPORT(int)
NCURSES_SP_NAME(slk_attr_set) (NCURSES_SP_DCLx
			       const attr_t attr,
			       NCURSES_PAIRS_T pair_arg,
			       void *opts)
{
    int code = ERR;
    int color_pair = pair_arg;

    T((T_CALLED("slk_attr_set(%p,%s,%d)"),
       (void *) SP_PARM,
       _traceattr(attr),
       color_pair));

    set_extended_pair(opts, color_pair);
    if (SP_PARM != 0
	&& SP_PARM->_slk != 0
	&& color_pair >= 0
	&& color_pair < SP_PARM->_pair_limit) {
	TR(TRACE_ATTRS, ("... current %s", _tracech_t(CHREF(SP_PARM->_slk->attr))));
	SetAttr(SP_PARM->_slk->attr, attr);
	if (color_pair > 0) {
	    SetPair(SP_PARM->_slk->attr, color_pair);
	}
	TR(TRACE_ATTRS, ("new attribute is %s", _tracech_t(CHREF(SP_PARM->_slk->attr))));
	code = OK;
    }
    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
slk_attr_set(const attr_t attr, NCURSES_COLOR_T pair_arg, void *opts)
{
    return NCURSES_SP_NAME(slk_attr_set) (CURRENT_SCREEN, attr,
					  pair_arg, opts);
}
#endif
