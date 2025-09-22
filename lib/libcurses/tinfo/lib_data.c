/* $OpenBSD: lib_data.c,v 1.5 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2021,2022 Thomas E. Dickey                                *
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
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 *     and: Juergen Pfeifer                                                 *
 ****************************************************************************/

/*
**	lib_data.c
**
**	Common data that may/may not be allocated, but is referenced globally
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_data.c,v 1.5 2023/10/17 09:52:09 nicm Exp $")

/*
 * OS/2's native linker complains if we don't initialize public data when
 * constructing a dll (reported by J.J.G.Ripoll).
 */
#if USE_REENTRANT
NCURSES_EXPORT(WINDOW *)
NCURSES_PUBLIC_VAR(stdscr) (void)
{
    return CURRENT_SCREEN ? StdScreen(CURRENT_SCREEN) : 0;
}
NCURSES_EXPORT(WINDOW *)
NCURSES_PUBLIC_VAR(curscr) (void)
{
    return CURRENT_SCREEN ? CurScreen(CURRENT_SCREEN) : 0;
}
NCURSES_EXPORT(WINDOW *)
NCURSES_PUBLIC_VAR(newscr) (void)
{
    return CURRENT_SCREEN ? NewScreen(CURRENT_SCREEN) : 0;
}
#else
NCURSES_EXPORT_VAR(WINDOW *) stdscr = 0;
NCURSES_EXPORT_VAR(WINDOW *) curscr = 0;
NCURSES_EXPORT_VAR(WINDOW *) newscr = 0;
#endif

NCURSES_EXPORT_VAR(SCREEN *) _nc_screen_chain = 0;

/*
 * The variable 'SP' will be defined as a function on systems that cannot link
 * data-only modules, since it is used in a lot of places within ncurses and we
 * cannot guarantee that any application will use any particular function.  We
 * put the WINDOW variables in this module, because it appears that any
 * application that uses them will also use 'SP'.
 *
 * This module intentionally does not reference other ncurses modules, to avoid
 * module coupling that increases the size of the executable.
 */
#if BROKEN_LINKER
static SCREEN *my_screen;

NCURSES_EXPORT(SCREEN *)
_nc_screen(void)
{
    return my_screen;
}

NCURSES_EXPORT(int)
_nc_alloc_screen(void)
{
    my_screen = _nc_alloc_screen_sp();
    T(("_nc_alloc_screen_sp %p", my_screen));
    return (my_screen != 0);
}

NCURSES_EXPORT(void)
_nc_set_screen(SCREEN *sp)
{
    my_screen = sp;
}

#else

NCURSES_EXPORT_VAR(SCREEN *) SP = NULL; /* Some linkers require initialized data... */
#endif
/* *INDENT-OFF* */
#define CHARS_0s { '\0' }

#define TGETENT_0 { 0L, FALSE, NULL, NULL, NULL }
#define TGETENT_0s { TGETENT_0, TGETENT_0, TGETENT_0, TGETENT_0 }

NCURSES_EXPORT_VAR(NCURSES_GLOBALS) _nc_globals = {
    0,				/* have_sigtstp */
    0,				/* have_sigwinch */
    0,				/* cleanup_nested */

    FALSE,			/* init_signals */
    FALSE,			/* init_screen */

    NULL,			/* comp_sourcename */
    NULL,			/* comp_termtype */

    FALSE,			/* have_tic_directory */
    FALSE,			/* keep_tic_directory */
    0,				/* tic_directory */

    NULL,			/* dbi_list */
    0,				/* dbi_size */

    NULL,			/* first_name */
    NULL,			/* keyname_table */
    0,				/* init_keyname */

    0,				/* slk_format */

    2048,			/* getstr_limit */

    NULL,			/* safeprint_buf */
    0,				/* safeprint_used */

    TGETENT_0s,			/* tgetent_cache */
    0,				/* tgetent_index */
    0,				/* tgetent_sequence */
    0,				/* terminal_count */

    0,				/* dbd_blob */
    0,				/* dbd_list */
    0,				/* dbd_size */
    0,				/* dbd_time */
    { { 0, 0 } },		/* dbd_vars */

#if HAVE_TSEARCH
    NULL,			/* cached_tparm */
    0,				/* count_tparm */
#endif /* HAVE_TSEARCH */

#ifdef USE_TERM_DRIVER
    0,				/* term_driver */
#endif

#ifndef USE_SP_WINDOWLIST
    0,				/* _nc_windowlist */
#endif

#if USE_HOME_TERMINFO
    NULL,			/* home_terminfo */
#endif

#if !USE_SAFE_SPRINTF
    0,				/* safeprint_cols */
    0,				/* safeprint_rows */
#endif

#ifdef USE_PTHREADS
    PTHREAD_MUTEX_INITIALIZER,	/* mutex_curses */
    PTHREAD_MUTEX_INITIALIZER,	/* mutex_prescreen */
    PTHREAD_MUTEX_INITIALIZER,	/* mutex_screen */
    PTHREAD_MUTEX_INITIALIZER,	/* mutex_update */
    PTHREAD_MUTEX_INITIALIZER,	/* mutex_tst_tracef */
    PTHREAD_MUTEX_INITIALIZER,	/* mutex_tracef */
    0,				/* nested_tracef */
    0,				/* use_pthreads */
#if USE_PTHREADS_EINTR
    0,				/* read_thread */
#endif
#endif
#if USE_WIDEC_SUPPORT
    CHARS_0s,			/* key_name */
#endif
#ifdef TRACE
    FALSE,			/* trace_opened */
    CHARS_0s,			/* trace_fname */
    0,				/* trace_level */
    NULL,			/* trace_fp */
    -1,				/* trace_fd */

    NULL,			/* tracearg_buf */
    0,				/* tracearg_used */

    NULL,			/* tracebuf_ptr */
    0,				/* tracebuf_used */

    CHARS_0s,			/* tracechr_buf */

    NULL,			/* tracedmp_buf */
    0,				/* tracedmp_used */

    NULL,			/* tracetry_buf */
    0,				/* tracetry_used */

    { CHARS_0s, CHARS_0s },	/* traceatr_color_buf */
    0,				/* traceatr_color_sel */
    -1,				/* traceatr_color_last */
#if !defined(USE_PTHREADS) && USE_REENTRANT
    0,				/* nested_tracef */
#endif
#endif /* TRACE */
#if NO_LEAKS
    FALSE,			/* leak_checking */
#endif
};

#define STACK_FRAME_0	{ { 0 }, 0 }
#define STACK_FRAME_0s	{ STACK_FRAME_0 }
#define NUM_VARS_0s	{ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }

#define RIPOFF_0	{ 0,0,0 }
#define RIPOFF_0s	{ RIPOFF_0 }

NCURSES_EXPORT_VAR(NCURSES_PRESCREEN) _nc_prescreen = {
    NULL,			/* allocated */
    TRUE,			/* use_env */
    FALSE,			/* filter_mode */
    A_NORMAL,			/* previous_attr */
    {				/* tparm_state */
	NULL,			/* tparam_base */

	STACK_FRAME_0s,		/* stack */
	0,			/* stack_ptr */

	NULL,			/* out_buff */
	0,			/* out_size */
	0,			/* out_used */

	NULL,			/* fmt_buff */
	0,			/* fmt_size */

	NUM_VARS_0s,		/* static_vars */
#ifdef TRACE
	NULL,			/* tname */
#endif
    },
    NULL,			/* saved_tty */
    FALSE,			/* use_tioctl */
    0,				/* _outch */
#ifndef USE_SP_RIPOFF
    RIPOFF_0s,			/* ripoff */
    NULL,			/* rsp */
#endif
#if NCURSES_NO_PADDING
    FALSE,			/* flag to set if padding disabled  */
#endif
#if BROKEN_LINKER || USE_REENTRANT
    NULL,			/* real_acs_map */
    0,				/* LINES */
    0,				/* COLS */
    8,				/* TABSIZE */
    1000,			/* ESCDELAY */
    0,				/* cur_term */
#endif
#ifdef TRACE
#if BROKEN_LINKER || USE_REENTRANT
    0L,				/* _outchars */
    NULL,			/* _tputs_trace */
#endif
#endif
};
/* *INDENT-ON* */

/*
 * wgetch() and other functions with a WINDOW* parameter may use a SCREEN*
 * internally, and it is useful to allow those to be invoked without switching
 * SCREEN's, e.g., for multi-threaded applications.
 */
NCURSES_EXPORT(SCREEN *)
_nc_screen_of(WINDOW *win)
{
    SCREEN *sp = 0;

    if (win != 0) {
	sp = WINDOW_EXT(win, screen);
    }
    return (sp);
}

/******************************************************************************/
#ifdef USE_PTHREADS
static void
init_global_mutexes(void)
{
    static bool initialized = FALSE;

    if (!initialized) {
	initialized = TRUE;
	_nc_mutex_init(&_nc_globals.mutex_curses);
	_nc_mutex_init(&_nc_globals.mutex_prescreen);
	_nc_mutex_init(&_nc_globals.mutex_screen);
	_nc_mutex_init(&_nc_globals.mutex_update);
	_nc_mutex_init(&_nc_globals.mutex_tst_tracef);
	_nc_mutex_init(&_nc_globals.mutex_tracef);
    }
}

NCURSES_EXPORT(void)
_nc_init_pthreads(void)
{
    if (_nc_use_pthreads)
	return;
# if USE_WEAK_SYMBOLS
    if ((pthread_mutex_init) == 0)
	return;
    if ((pthread_mutex_lock) == 0)
	return;
    if ((pthread_mutex_unlock) == 0)
	return;
    if ((pthread_mutex_trylock) == 0)
	return;
    if ((pthread_mutexattr_settype) == 0)
	return;
# endif
    _nc_use_pthreads = 1;
    init_global_mutexes();
}

/*
 * Use recursive mutexes if we have them - they're part of Unix98.
 * For the cases where we do not, _nc_mutex_trylock() is used to avoid a
 * deadlock, at the expense of memory leaks and unexpected failures that
 * may not be handled by typical clients.
 *
 * FIXME - need configure check for PTHREAD_MUTEX_RECURSIVE, define it to
 * PTHREAD_MUTEX_NORMAL if not supported.
 */
NCURSES_EXPORT(void)
_nc_mutex_init(pthread_mutex_t * obj)
{
    pthread_mutexattr_t recattr;

    if (_nc_use_pthreads) {
	pthread_mutexattr_init(&recattr);
	pthread_mutexattr_settype(&recattr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(obj, &recattr);
    }
}

NCURSES_EXPORT(int)
_nc_mutex_lock(pthread_mutex_t * obj)
{
    int rc = 0;
    if (_nc_use_pthreads != 0)
	rc = pthread_mutex_lock(obj);
    return rc;
}

NCURSES_EXPORT(int)
_nc_mutex_trylock(pthread_mutex_t * obj)
{
    int rc = 0;
    if (_nc_use_pthreads != 0)
	rc = pthread_mutex_trylock(obj);
    return rc;
}

NCURSES_EXPORT(int)
_nc_mutex_unlock(pthread_mutex_t * obj)
{
    int rc = 0;
    if (_nc_use_pthreads != 0)
	rc = pthread_mutex_unlock(obj);
    return rc;
}
#endif /* USE_PTHREADS */

#if defined(USE_PTHREADS) || USE_PTHREADS_EINTR
#if USE_WEAK_SYMBOLS
/*
 * NB: sigprocmask(2) is global but pthread_sigmask(3p)
 * only for the calling thread.
 */
NCURSES_EXPORT(int)
_nc_sigprocmask(int how, const sigset_t * newmask, sigset_t * oldmask)
{
    if ((pthread_sigmask))
	return pthread_sigmask(how, newmask, oldmask);
    else
	return (sigprocmask) (how, newmask, oldmask);
}
#endif
#endif /* USE_PTHREADS */
