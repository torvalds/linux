/* $OpenBSD: lib_initscr.c,v 1.4 2023/10/17 09:52:08 nicm Exp $ */

/****************************************************************************
 * Copyright 2019,2020 Thomas E. Dickey                                     *
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
 ****************************************************************************/

/*
**	lib_initscr.c
**
**	The routines initscr(), and termname().
**
*/

#include <curses.priv.h>

#if HAVE_SYS_TERMIO_H
#include <sys/termio.h>		/* needed for ISC */
#endif

MODULE_ID("$Id: lib_initscr.c,v 1.4 2023/10/17 09:52:08 nicm Exp $")

NCURSES_EXPORT(WINDOW *)
initscr(void)
{
    WINDOW *result;

    START_TRACE();
    T((T_CALLED("initscr()")));

    _nc_init_pthreads();
    _nc_lock_global(curses);

    /* Portable applications must not call initscr() more than once */
    if (!_nc_globals.init_screen) {
	const char *env;
	char *name;

	_nc_globals.init_screen = TRUE;

	env = getenv("TERM");
	(void) VALID_TERM_ENV(env, "unknown");

	if ((name = strdup(env)) == NULL) {
	    fprintf(stderr, "Error opening allocating $TERM.\n");
	    ExitProgram(EXIT_FAILURE);
	}
#ifdef __CYGWIN__
	/*
	 * 2002/9/21
	 * Work around a bug in Cygwin.  Full-screen subprocesses run from
	 * bash, in turn spawned from another full-screen process, will dump
	 * core when attempting to write to stdout.  Opening /dev/tty
	 * explicitly seems to fix the problem.
	 */
	if (NC_ISATTY(fileno(stdout))) {
	    FILE *fp = fopen("/dev/tty", "w");
	    if (fp != 0 && NC_ISATTY(fileno(fp))) {
		fclose(stdout);
		dup2(fileno(fp), STDOUT_FILENO);
		stdout = fdopen(STDOUT_FILENO, "w");
	    }
	}
#endif
	if (newterm(name, stdout, stdin) == 0) {
	    fprintf(stderr, "Error opening terminal: %s.\n", name);
	    ExitProgram(EXIT_FAILURE);
	}

	/* def_shell_mode - done in newterm/_nc_setupscreen */
#if NCURSES_SP_FUNCS
	NCURSES_SP_NAME(def_prog_mode) (CURRENT_SCREEN);
#else
	def_prog_mode();
#endif
	free(name);
    }
    result = stdscr;
    _nc_unlock_global(curses);

    returnWin(result);
}
