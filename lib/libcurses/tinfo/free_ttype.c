/* $OpenBSD: free_ttype.c,v 1.8 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2020-2022,2023 Thomas E. Dickey                                *
 * Copyright 1999-2011,2017 Free Software Foundation, Inc.                  *
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
 *  Author: Thomas E. Dickey                    1999-on                     *
 ****************************************************************************/

/*
 * free_ttype.c -- allocation functions for TERMTYPE
 *
 *	_nc_free_termtype()
 *	use_extended_names()
 *
 */

#include <curses.priv.h>

#include <tic.h>

MODULE_ID("$Id: free_ttype.c,v 1.8 2023/10/17 09:52:09 nicm Exp $")

static void
really_free_termtype(TERMTYPE2 *ptr, bool freeStrings)
{
    T(("really_free_termtype(%s) %d", ptr->term_names, freeStrings));

    if (freeStrings) {
	FreeIfNeeded(ptr->str_table);
    }
    FreeIfNeeded(ptr->Booleans);
    FreeIfNeeded(ptr->Numbers);
    FreeIfNeeded(ptr->Strings);
#if NCURSES_XNAMES
    if (freeStrings) {
	FreeIfNeeded(ptr->ext_str_table);
    }
    FreeIfNeeded(ptr->ext_Names);
#endif
    memset(ptr, 0, sizeof(TERMTYPE));
    _nc_free_entry(_nc_head, ptr);
}

NCURSES_EXPORT(void)
_nc_free_termtype(TERMTYPE *ptr)
{
    really_free_termtype((TERMTYPE2 *) ptr, !NCURSES_EXT_NUMBERS);
}

/*
 * These similar entrypoints are not used outside of ncurses.
 */
NCURSES_EXPORT(void)
_nc_free_termtype1(TERMTYPE *ptr)
{
    really_free_termtype((TERMTYPE2 *) ptr, TRUE);
}

#if NCURSES_EXT_NUMBERS
NCURSES_EXPORT(void)
_nc_free_termtype2(TERMTYPE2 *ptr)
{
    really_free_termtype(ptr, TRUE);
}
#endif

#if NCURSES_XNAMES
NCURSES_EXPORT_VAR(bool) _nc_user_definable = TRUE;

NCURSES_EXPORT(int)
use_extended_names(bool flag)
{
    int oldflag = _nc_user_definable;

    START_TRACE();
    T((T_CALLED("use_extended_names(%d)"), flag));
    _nc_user_definable = flag;
    returnBool(oldflag);
}
#endif
