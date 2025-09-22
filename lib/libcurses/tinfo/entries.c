/* $OpenBSD: entries.c,v 1.2 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2019-2022,2023 Thomas E. Dickey                                *
 * Copyright 2006-2012,2017 Free Software Foundation, Inc.                  *
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
 *     and: Juergen Pfeifer                                                 *
 ****************************************************************************/

#include <curses.priv.h>

#include <ctype.h>

#include <tic.h>

MODULE_ID("$Id: entries.c,v 1.2 2023/10/17 09:52:09 nicm Exp $")

/****************************************************************************
 *
 * Entry queue handling
 *
 ****************************************************************************/
/*
 *  The entry list is a doubly linked list with NULLs terminating the lists:
 *
 *	  ---------   ---------   ---------
 *	  |       |   |       |   |       |   offset
 *        |-------|   |-------|   |-------|
 *	  |   ----+-->|   ----+-->|  NULL |   next
 *	  |-------|   |-------|   |-------|
 *	  |  NULL |<--+----   |<--+----   |   last
 *	  ---------   ---------   ---------
 *	      ^                       ^
 *	      |                       |
 *	      |                       |
 *	   _nc_head                _nc_tail
 */

NCURSES_EXPORT_VAR(ENTRY *) _nc_head = 0;
NCURSES_EXPORT_VAR(ENTRY *) _nc_tail = 0;

static ENTRY *
_nc_delink_entry(ENTRY * headp, const TERMTYPE2 *const tterm)
/* delink the allocated storage for the given list entry */
{
    ENTRY *ep, *last;

    for (last = 0, ep = headp; ep != 0; last = ep, ep = ep->next) {
	if (&(ep->tterm) == tterm) {
	    if (last != 0) {
		last->next = ep->next;
	    }
	    if (ep->next != 0) {
		ep->next->last = last;
	    }
	    if (ep == _nc_head) {
		_nc_head = ep->next;
	    }
	    if (ep == _nc_tail) {
		_nc_tail = last;
	    }
	    break;
	}
    }
    return ep;
}

NCURSES_EXPORT(void)
_nc_free_entry(ENTRY * headp, TERMTYPE2 *tterm)
/* free the allocated storage consumed by the given list entry */
{
    ENTRY *ep;

    if ((ep = _nc_delink_entry(headp, tterm)) != 0) {
	free(ep);
    }
}

NCURSES_EXPORT(void)
_nc_free_entries(ENTRY * headp)
/* free the allocated storage consumed by list entries */
{
    (void) headp;		/* unused - _nc_head is altered here! */

    while (_nc_head != 0) {
	_nc_free_termtype2(&(_nc_head->tterm));
    }
}

NCURSES_EXPORT(void)
_nc_leaks_tinfo(void)
{
#if NO_LEAKS
    char *s;
#endif

    T((T_CALLED("_nc_leaks_tinfo()")));
#if NO_LEAKS
    _nc_globals.leak_checking = TRUE;
    _nc_free_tparm(cur_term);
    _nc_tgetent_leaks();

#ifdef USE_PTHREADS
    /*
     * Discard any prescreen data which is not used for the current screen.
     */
    _nc_lock_global(screen);
    {
	PRESCREEN_LIST *p;
	pthread_t id = GetThreadID();
	for (p = _nc_prescreen.allocated; p != 0; p = p->next) {
	    if (p->id == id && p->sp != CURRENT_SCREEN) {
		FreeAndNull(p->sp);
	    }
	}
    }
    _nc_unlock_global(screen);
#endif
    if (TerminalOf(CURRENT_SCREEN) != 0) {
	del_curterm(TerminalOf(CURRENT_SCREEN));
    }
    _nc_forget_prescr();

    _nc_comp_captab_leaks();
    _nc_comp_userdefs_leaks();
    _nc_free_entries(_nc_head);
    _nc_get_type(0);
    _nc_first_name(0);
    _nc_db_iterator_leaks();
    _nc_keyname_leaks();
#if BROKEN_LINKER || USE_REENTRANT
    _nc_names_leaks();
    _nc_codes_leaks();
    FreeIfNeeded(_nc_prescreen.real_acs_map);
#endif
    _nc_comp_error_leaks();

    if ((s = _nc_home_terminfo()) != 0)
	free(s);

#ifdef TRACE
    T((T_RETURN("")));
    curses_trace(0);
    _nc_trace_buf(-1, (size_t) 0);
#endif

#endif /* NO_LEAKS */
    returnVoid;
}

#if NO_LEAKS
NCURSES_EXPORT(void)
_nc_free_tinfo(int code)
{
    T((T_CALLED("_nc_free_tinfo(%d)"), code));
    _nc_leaks_tinfo();
    exit(code);
}
#endif

NCURSES_EXPORT(void)
exit_terminfo(int code)
{
    T((T_CALLED("exit_terminfo(%d)"), code));
#if NO_LEAKS
    _nc_leaks_tinfo();
#endif
    exit(code);
}
