/* $OpenBSD: lib_cur_term.c,v 1.7 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
,* Copyright 2020-2021,2022 Thomas E. Dickey                                *
 * Copyright 1998-2016,2017 Free Software Foundation, Inc.                  *
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
/*
 * Module that "owns" the 'cur_term' variable:
 *
 *	TERMINAL *set_curterm(TERMINAL *)
 *	int del_curterm(TERMINAL *)
 */

#include <curses.priv.h>
#include <termcap.h>		/* ospeed */
#include <tic.h>		/* VALID_STRING */

MODULE_ID("$Id: lib_cur_term.c,v 1.7 2023/10/17 09:52:09 nicm Exp $")

#undef CUR
#define CUR TerminalType(termp).

#if USE_REENTRANT

NCURSES_EXPORT(TERMINAL *)
NCURSES_SP_NAME(_nc_get_cur_term) (NCURSES_SP_DCL0)
{
    return ((0 != TerminalOf(SP_PARM)) ? TerminalOf(SP_PARM) : CurTerm);
}

#if NCURSES_SP_FUNCS

NCURSES_EXPORT(TERMINAL *)
_nc_get_cur_term(void)
{
    return NCURSES_SP_NAME(_nc_get_cur_term) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(TERMINAL *)
NCURSES_PUBLIC_VAR(cur_term) (void)
{
#if NCURSES_SP_FUNCS
    return NCURSES_SP_NAME(_nc_get_cur_term) (CURRENT_SCREEN);
#else
    return NCURSES_SP_NAME(_nc_get_cur_term) (NCURSES_SP_ARG);
#endif
}

#else
NCURSES_EXPORT_VAR(TERMINAL *) cur_term = 0;
#endif

NCURSES_EXPORT(TERMINAL *)
NCURSES_SP_NAME(set_curterm) (NCURSES_SP_DCLx TERMINAL *termp)
{
    TERMINAL *oldterm;

    T((T_CALLED("set_curterm(%p)"), (void *) termp));

    _nc_lock_global(curses);
    oldterm = cur_term;
    if (SP_PARM)
	SP_PARM->_term = termp;
#if USE_REENTRANT
    CurTerm = termp;
#else
    cur_term = termp;
#endif
    if (termp != 0) {
#ifdef USE_TERM_DRIVER
	TERMINAL_CONTROL_BLOCK *TCB = (TERMINAL_CONTROL_BLOCK *) termp;
	ospeed = (NCURSES_OSPEED) _nc_ospeed(termp->_baudrate);
	if (TCB->drv &&
	    TCB->drv->isTerminfo &&
	    TerminalType(termp).Strings) {
	    PC = (char) (VALID_STRING(pad_char) ? pad_char[0] : 0);
	}
	TCB->csp = SP_PARM;
#else
	ospeed = (NCURSES_OSPEED) _nc_ospeed(termp->_baudrate);
	if (TerminalType(termp).Strings) {
	    PC = (char) (VALID_STRING(pad_char) ? pad_char[0] : 0);
	}
#endif
#if !USE_REENTRANT
	save_ttytype(termp);
#endif
    }
    _nc_unlock_global(curses);

    T((T_RETURN("%p"), (void *) oldterm));
    return (oldterm);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(TERMINAL *)
set_curterm(TERMINAL *termp)
{
    return NCURSES_SP_NAME(set_curterm) (CURRENT_SCREEN, termp);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(del_curterm) (NCURSES_SP_DCLx TERMINAL *termp)
{
    int rc = ERR;

    T((T_CALLED("del_curterm(%p, %p)"), (void *) SP_PARM, (void *) termp));

    if (termp != 0) {
#ifdef USE_TERM_DRIVER
	TERMINAL_CONTROL_BLOCK *TCB = (TERMINAL_CONTROL_BLOCK *) termp;
#endif
	TERMINAL *cur = (
#if USE_REENTRANT
			    NCURSES_SP_NAME(_nc_get_cur_term) (NCURSES_SP_ARG)
#else
			    cur_term
#endif
	);

#if NCURSES_EXT_NUMBERS
#if NCURSES_EXT_COLORS
	_nc_free_termtype1(&termp->type);
#else
	_nc_free_termtype2(&termp->type);
#endif
#endif
	_nc_free_termtype2(&TerminalType(termp));
	if (termp == cur)
	    NCURSES_SP_NAME(set_curterm) (NCURSES_SP_ARGx 0);

	FreeIfNeeded(termp->_termname);
#if USE_HOME_TERMINFO
	if (_nc_globals.home_terminfo != 0) {
	    FreeAndNull(_nc_globals.home_terminfo);
	}
#endif
#ifdef USE_TERM_DRIVER
	if (TCB->drv)
	    TCB->drv->td_release(TCB);
#endif
#if NO_LEAKS
	/* discard memory used in tgetent's cache for this terminal */
	_nc_tgetent_leak(termp);
#endif
	if (--_nc_globals.terminal_count == 0) {
	    _nc_free_tparm(termp);
	}

	free(termp->tparm_state.fmt_buff);
	free(termp->tparm_state.out_buff);
	free(termp);

	rc = OK;
    }

    returnCode(rc);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
del_curterm(TERMINAL *termp)
{
    int rc;

    _nc_lock_global(curses);
    rc = NCURSES_SP_NAME(del_curterm) (CURRENT_SCREEN, termp);
    _nc_unlock_global(curses);

    return (rc);
}
#endif
