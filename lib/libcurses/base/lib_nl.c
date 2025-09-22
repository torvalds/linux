/* $OpenBSD: lib_nl.c,v 1.7 2023/10/17 09:52:08 nicm Exp $ */

/****************************************************************************
 * Copyright 2020,2023 Thomas E. Dickey                                     *
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
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/*
 *	nl.c
 *
 *	Routines:
 *		nl()
 *		nonl()
 *
 */

#include <curses.priv.h>

MODULE_ID("$Id: lib_nl.c,v 1.7 2023/10/17 09:52:08 nicm Exp $")

#ifdef __EMX__
#include <io.h>
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(nl) (NCURSES_SP_DCL0)
{
    T((T_CALLED("nl(%p)"), (void *) SP_PARM));
    if (0 == SP_PARM)
	returnCode(ERR);
    IsNl(SP_PARM) = TRUE;
#ifdef __EMX__
    _nc_flush();
    _fsetmode(NC_OUTPUT(SP_PARM), "t");
#endif
    returnCode(OK);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
nl(void)
{
    return NCURSES_SP_NAME(nl) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(nonl) (NCURSES_SP_DCL0)
{
    T((T_CALLED("nonl(%p)"), (void *) SP_PARM));
    if (0 == SP_PARM)
	returnCode(ERR);
    IsNl(SP_PARM) = FALSE;
#ifdef __EMX__
    _nc_flush();
    _fsetmode(NC_OUTPUT(SP_PARM), "b");
#endif
    returnCode(OK);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
nonl(void)
{
    return NCURSES_SP_NAME(nonl) (CURRENT_SCREEN);
}
#endif
