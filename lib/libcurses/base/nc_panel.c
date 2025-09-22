/* $OpenBSD: nc_panel.c,v 1.4 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2020 Thomas E. Dickey                                          *
 * Copyright 1998-2000,2009 Free Software Foundation, Inc.                  *
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
 *  Author: Thomas E. Dickey <dickey@clark.net> 1997                        *
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: nc_panel.c,v 1.4 2023/10/17 09:52:09 nicm Exp $")

NCURSES_EXPORT(struct panelhook *)
NCURSES_SP_NAME(_nc_panelhook) (NCURSES_SP_DCL0)
{
    return (SP_PARM
	    ? &(SP_PARM->_panelHook)
	    : (CURRENT_SCREEN
	       ? &(CURRENT_SCREEN->_panelHook)
	       : 0));
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(struct panelhook *)
_nc_panelhook(void)
{
    return NCURSES_SP_NAME(_nc_panelhook) (CURRENT_SCREEN);
}
#endif
