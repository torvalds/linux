/* $OpenBSD: lib_freeall.c,v 1.6 2023/10/17 09:52:08 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2020,2021 Thomas E. Dickey                                *
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
 *  Author: Thomas E. Dickey                    1996-on                     *
 ****************************************************************************/

#include <curses.priv.h>
#include <tic.h>

#if HAVE_NC_FREEALL

#if HAVE_LIBDBMALLOC
extern int malloc_errfd;	/* FIXME */
#endif

MODULE_ID("$Id: lib_freeall.c,v 1.6 2023/10/17 09:52:08 nicm Exp $")

/*
 * Free all ncurses data.  This is used for testing only (there's no practical
 * use for it as an extension).
 */
NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_freeall) (NCURSES_SP_DCL0)
{
    static va_list empty_va;

    T((T_CALLED("_nc_freeall()")));
#if NO_LEAKS
    _nc_globals.leak_checking = TRUE;
    if (SP_PARM != 0) {
	if (SP_PARM->_oldnum_list != 0) {
	    FreeAndNull(SP_PARM->_oldnum_list);
	}
	if (SP_PARM->_panelHook.destroy != 0) {
	    SP_PARM->_panelHook.destroy(SP_PARM->_panelHook.stdscr_pseudo_panel);
	}
#if NCURSES_EXT_COLORS
	_nc_new_pair_leaks(SP_PARM);
#endif
    }
#endif
    if (SP_PARM != 0) {
	_nc_lock_global(curses);

	while (WindowList(SP_PARM) != 0) {
	    WINDOWLIST *p, *q;
	    bool deleted = FALSE;

	    /* Delete only windows that're not a parent */
	    for (each_window(SP_PARM, p)) {
		WINDOW *p_win = &(p->win);
		bool found = FALSE;

		if (IS_PAD(p_win))
		    continue;

#ifndef USE_SP_WINDOWLIST
		if (p->screen != SP_PARM)
		    continue;
#endif

		for (each_window(SP_PARM, q)) {
		    WINDOW *q_win = &(q->win);

#ifndef USE_SP_WINDOWLIST
		    if (q->screen != SP_PARM)
			continue;
#endif

		    if ((p != q)
			&& IS_SUBWIN(q_win)
			&& (p_win == q_win->_parent)) {
			found = TRUE;
			break;
		    }
		}

		if (!found) {
		    if (delwin(p_win) != ERR)
			deleted = TRUE;
		    break;
		}
	    }

	    /*
	     * Don't continue to loop if the list is trashed.
	     */
	    if (!deleted)
		break;
	}
	delscreen(SP_PARM);
	_nc_unlock_global(curses);
    }

    (void) _nc_printf_string(0, empty_va);
#ifdef TRACE
    (void) _nc_trace_buf(-1, (size_t) 0);
#endif
#if USE_WIDEC_SUPPORT
    FreeIfNeeded(_nc_wacs);
#endif
    _nc_leaks_tinfo();

#if HAVE_LIBDBMALLOC
    malloc_dump(malloc_errfd);
#elif HAVE_LIBDMALLOC
#elif HAVE_LIBMPATROL
    __mp_summary();
#elif HAVE_PURIFY
    purify_all_inuse();
#endif
    returnVoid;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_freeall(void)
{
    NCURSES_SP_NAME(_nc_freeall) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_free_and_exit) (NCURSES_SP_DCLx int code)
{
    T((T_CALLED("_nc_free_and_exit(%d)"), code));
    NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_ARG);
    NCURSES_SP_NAME(_nc_freeall) (NCURSES_SP_ARG);
#ifdef TRACE
    curses_trace(0);		/* close trace file, freeing its setbuf */
    {
	static va_list fake;
	free(_nc_varargs("?", fake));
    }
#endif
    exit(code);
}

#else /* !HAVE_NC_FREEALL */
NCURSES_EXPORT(void)
_nc_freeall(void)
{
}

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_free_and_exit) (NCURSES_SP_DCLx int code)
{
    if (SP_PARM) {
	delscreen(SP_PARM);
    }
    exit(code);
}
#endif /* HAVE_NC_FREEALL */

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_free_and_exit(int code)
{
    NCURSES_SP_NAME(_nc_free_and_exit) (CURRENT_SCREEN, code);
}
#endif

NCURSES_EXPORT(void)
exit_curses(int code)
{
#if NO_LEAKS
#if NCURSES_SP_FUNCS
    NCURSES_SP_NAME(_nc_free_and_exit) (CURRENT_SCREEN, code);
#else
    _nc_free_and_exit(code);	/* deprecated... */
#endif
#endif
    exit(code);
}
