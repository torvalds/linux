/****************************************************************************
 * Copyright 2018-2021,2022 Thomas E. Dickey                                *
 * Copyright 2008-2016,2017 Free Software Foundation, Inc.                  *
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
 *  Author: Juergen Pfeifer                                                 *
 *     and: Thomas E. Dickey                                                *
 ****************************************************************************/

#include <curses.priv.h>
#define CUR TerminalType((TERMINAL*)TCB).
#include <tic.h>
#include <termcap.h>		/* ospeed */

#if HAVE_NANOSLEEP
#include <time.h>
#if HAVE_SYS_TIME_H
#include <sys/time.h>		/* needed for MacOS X DP3 */
#endif
#endif

#if HAVE_SIZECHANGE
# if !defined(sun) || !TERMIOS
#  if HAVE_SYS_IOCTL_H
#   include <sys/ioctl.h>
#  endif
# endif
#endif

MODULE_ID("$Id: tinfo_driver.c,v 1.1 2023/10/17 09:52:09 nicm Exp $")

/*
 * SCO defines TIOCGSIZE and the corresponding struct.  Other systems (SunOS,
 * Solaris, IRIX) define TIOCGWINSZ and struct winsize.
 */
#ifdef TIOCGSIZE
# define IOCTL_WINSIZE TIOCGSIZE
# define STRUCT_WINSIZE struct ttysize
# define WINSIZE_ROWS(n) (int)n.ts_lines
# define WINSIZE_COLS(n) (int)n.ts_cols
#else
# ifdef TIOCGWINSZ
#  define IOCTL_WINSIZE TIOCGWINSZ
#  define STRUCT_WINSIZE struct winsize
#  define WINSIZE_ROWS(n) (int)n.ws_row
#  define WINSIZE_COLS(n) (int)n.ws_col
# endif
#endif

/*
 * These should be screen structure members.  They need to be globals for
 * historical reasons.  So we assign them in start_color() and also in
 * set_term()'s screen-switching logic.
 */
#if USE_REENTRANT
NCURSES_EXPORT(int)
NCURSES_PUBLIC_VAR(COLOR_PAIRS) (void)
{
    return CURRENT_SCREEN ? CURRENT_SCREEN->_pair_count : -1;
}
NCURSES_EXPORT(int)
NCURSES_PUBLIC_VAR(COLORS) (void)
{
    return CURRENT_SCREEN ? CURRENT_SCREEN->_color_count : -1;
}
#else
NCURSES_EXPORT_VAR(int) COLOR_PAIRS = 0;
NCURSES_EXPORT_VAR(int) COLORS = 0;
#endif

#define TCBMAGIC NCDRV_MAGIC(NCDRV_TINFO)
#define AssertTCB() assert(TCB!=0 && TCB->magic==TCBMAGIC)
#define SetSP() assert(TCB->csp!=0); sp = TCB->csp; (void) sp

/*
 * This routine needs to do all the work to make curscr look
 * like newscr.
 */
static int
drv_doupdate(TERMINAL_CONTROL_BLOCK * TCB)
{
    AssertTCB();
    return TINFO_DOUPDATE(TCB->csp);
}

static const char *
drv_Name(TERMINAL_CONTROL_BLOCK * TCB)
{
    (void) TCB;
    return "tinfo";
}

static void
get_baudrate(TERMINAL *termp)
{
    int my_ospeed;
    int result;
    if (GET_TTY(termp->Filedes, &termp->Nttyb) == OK) {
#ifdef TERMIOS
	termp->Nttyb.c_oflag &= (unsigned) (~OFLAGS_TABS);
#elif defined(EXP_WIN32_DRIVER)
	/* noop */
#else
	termp->Nttyb.sg_flags &= (unsigned) (~XTABS);
#endif
    }
#ifdef USE_OLD_TTY
    result = (int) cfgetospeed(&(termp->Nttyb));
    my_ospeed = (NCURSES_OSPEED) _nc_ospeed(result);
#else /* !USE_OLD_TTY */
#ifdef TERMIOS
    my_ospeed = (NCURSES_OSPEED) cfgetospeed(&(termp->Nttyb));
#elif defined(EXP_WIN32_DRIVER)
    /* noop */
    my_ospeed = 0;
#else
    my_ospeed = (NCURSES_OSPEED) termp->Nttyb.sg_ospeed;
#endif
    result = _nc_baudrate(my_ospeed);
#endif
    termp->_baudrate = result;
    ospeed = (NCURSES_OSPEED) my_ospeed;
}

#undef SETUP_FAIL
#define SETUP_FAIL FALSE

#define NO_COPY {}

static bool
drv_CanHandle(TERMINAL_CONTROL_BLOCK * TCB, const char *tname, int *errret)
{
    bool result = FALSE;
    int status;
    TERMINAL *termp;
    SCREEN *sp;

    START_TRACE();
    T((T_CALLED("tinfo::drv_CanHandle(%p)"), (void *) TCB));

    assert(TCB != 0 && tname != 0);
    termp = (TERMINAL *) TCB;
    sp = TCB->csp;
    TCB->magic = TCBMAGIC;

#if (NCURSES_USE_DATABASE || NCURSES_USE_TERMCAP)
    status = _nc_setup_tinfo(tname, &TerminalType(termp));
    T(("_nc_setup_tinfo returns %d", status));
#else
    T(("no database available"));
    status = TGETENT_NO;
#endif

    /* try fallback list if entry on disk */
    if (status != TGETENT_YES) {
	const TERMTYPE2 *fallback = _nc_fallback2(tname);

	if (fallback) {
	    T(("found fallback entry"));
	    TerminalType(termp) = *fallback;
	    status = TGETENT_YES;
	}
    }

    if (status != TGETENT_YES) {
	NCURSES_SP_NAME(del_curterm) (NCURSES_SP_ARGx termp);
	if (status == TGETENT_ERR) {
	    ret_error0(status, "terminals database is inaccessible\n");
	} else if (status == TGETENT_NO) {
	    ret_error1(status, "unknown terminal type.\n",
		       tname, NO_COPY);
	} else {
	    ret_error0(status, "unexpected return-code\n");
	}
    }
    result = TRUE;
#if NCURSES_EXT_NUMBERS
    _nc_export_termtype2(&termp->type, &TerminalType(termp));
#endif
#if !USE_REENTRANT
    save_ttytype(termp);
#endif

    if (command_character)
	_nc_tinfo_cmdch(termp, *command_character);

    /*
     * If an application calls setupterm() rather than initscr() or
     * newterm(), we will not have the def_prog_mode() call in
     * _nc_setupscreen().  Do it now anyway, so we can initialize the
     * baudrate.
     */
    if (sp == 0 && NC_ISATTY(termp->Filedes)) {
	get_baudrate(termp);
    }
#if NCURSES_EXT_NUMBERS
#define cleanup_termtype() \
    _nc_free_termtype2(&TerminalType(termp)); \
    _nc_free_termtype(&termp->type)
#else
#define cleanup_termtype() \
    _nc_free_termtype2(&TerminalType(termp))
#endif

    if (generic_type) {
	/*
	 * BSD 4.3's termcap contains mis-typed "gn" for wy99.  Do a sanity
	 * check before giving up.
	 */
	if ((VALID_STRING(cursor_address)
	     || (VALID_STRING(cursor_down) && VALID_STRING(cursor_home)))
	    && VALID_STRING(clear_screen)) {
	    cleanup_termtype();
	    ret_error1(TGETENT_YES, "terminal is not really generic.\n",
		       tname, NO_COPY);
	} else {
	    cleanup_termtype();
	    ret_error1(TGETENT_NO, "I need something more specific.\n",
		       tname, NO_COPY);
	}
    }
    if (hard_copy) {
	cleanup_termtype();
	ret_error1(TGETENT_YES, "I can't handle hardcopy terminals.\n",
		   tname, NO_COPY);
    }

    returnBool(result);
}

static int
drv_dobeepflash(TERMINAL_CONTROL_BLOCK * TCB, int beepFlag)
{
    SCREEN *sp;
    int res = ERR;

    AssertTCB();
    SetSP();

    /* FIXME: should make sure that we are not in altchar mode */
    if (beepFlag) {
	if (bell) {
	    res = NCURSES_PUTP2("bell", bell);
	    NCURSES_SP_NAME(_nc_flush) (sp);
	} else if (flash_screen) {
	    res = NCURSES_PUTP2("flash_screen", flash_screen);
	    NCURSES_SP_NAME(_nc_flush) (sp);
	}
    } else {
	if (flash_screen) {
	    res = NCURSES_PUTP2("flash_screen", flash_screen);
	    NCURSES_SP_NAME(_nc_flush) (sp);
	} else if (bell) {
	    res = NCURSES_PUTP2("bell", bell);
	    NCURSES_SP_NAME(_nc_flush) (sp);
	}
    }
    return res;
}

/*
 * SVr4 curses is known to interchange color codes (1,4) and (3,6), possibly
 * to maintain compatibility with a pre-ANSI scheme.  The same scheme is
 * also used in the FreeBSD syscons.
 */
static int
toggled_colors(int c)
{
    if (c < 16) {
	static const int table[] =
	{0, 4, 2, 6, 1, 5, 3, 7,
	 8, 12, 10, 14, 9, 13, 11, 15};
	c = table[c];
    }
    return c;
}

static int
drv_print(TERMINAL_CONTROL_BLOCK * TCB, char *data, int len)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();
#if NCURSES_EXT_FUNCS
    return NCURSES_SP_NAME(mcprint) (TCB->csp, data, len);
#else
    return ERR;
#endif
}

static int
drv_defaultcolors(TERMINAL_CONTROL_BLOCK * TCB, int fg, int bg)
{
    SCREEN *sp;
    int code = ERR;

    AssertTCB();
    SetSP();

    if (sp != 0 && orig_pair && orig_colors && (initialize_pair != 0)) {
#if NCURSES_EXT_FUNCS
	sp->_default_color = isDefaultColor(fg) || isDefaultColor(bg);
	sp->_has_sgr_39_49 = (NCURSES_SP_NAME(tigetflag) (NCURSES_SP_ARGx
							  "AX")
			      == TRUE);
	sp->_default_fg = isDefaultColor(fg) ? COLOR_DEFAULT : fg;
	sp->_default_bg = isDefaultColor(bg) ? COLOR_DEFAULT : bg;
	if (sp->_color_pairs != 0) {
	    bool save = sp->_default_color;
	    sp->_default_color = TRUE;
	    NCURSES_SP_NAME(init_pair) (NCURSES_SP_ARGx
					0,
					(short)fg,
					(short)bg);
	    sp->_default_color = save;
	}
#endif
	code = OK;
    }
    return (code);
}

static void
drv_setcolor(TERMINAL_CONTROL_BLOCK * TCB,
	     int fore,
	     int color,
	     NCURSES_SP_OUTC outc)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();

    if (fore) {
	if (set_a_foreground) {
	    TPUTS_TRACE("set_a_foreground");
	    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				    TIPARM_1(set_a_foreground, color), 1, outc);
	} else {
	    TPUTS_TRACE("set_foreground");
	    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				    TIPARM_1(set_foreground,
					     toggled_colors(color)), 1, outc);
	}
    } else {
	if (set_a_background) {
	    TPUTS_TRACE("set_a_background");
	    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				    TIPARM_1(set_a_background, color), 1, outc);
	} else {
	    TPUTS_TRACE("set_background");
	    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				    TIPARM_1(set_background,
					     toggled_colors(color)), 1, outc);
	}
    }
}

static bool
drv_rescol(TERMINAL_CONTROL_BLOCK * TCB)
{
    bool result = FALSE;
    SCREEN *sp;

    AssertTCB();
    SetSP();

    if (orig_pair != 0) {
	NCURSES_PUTP2("orig_pair", orig_pair);
	result = TRUE;
    }
    return result;
}

static bool
drv_rescolors(TERMINAL_CONTROL_BLOCK * TCB)
{
    int result = FALSE;
    SCREEN *sp;

    AssertTCB();
    SetSP();

    if (orig_colors != 0) {
	NCURSES_PUTP2("orig_colors", orig_colors);
	result = TRUE;
    }
    return result;
}

static int
drv_size(TERMINAL_CONTROL_BLOCK * TCB, int *linep, int *colp)
{
    SCREEN *sp;
    bool useEnv = TRUE;
    bool useTioctl = TRUE;

    AssertTCB();
    sp = TCB->csp;		/* can be null here */

    if (sp) {
	useEnv = sp->_use_env;
	useTioctl = sp->use_tioctl;
    } else {
	useEnv = _nc_prescreen.use_env;
	useTioctl = _nc_prescreen.use_tioctl;
    }

#ifdef EXP_WIN32_DRIVER
    /* If we are here, then Windows console is used in terminfo mode.
       We need to figure out the size using the console API
     */
    _nc_console_size(linep, colp);
    T(("screen size: winconsole lines = %d columns = %d", *linep, *colp));
#else
    /* figure out the size of the screen */
    T(("screen size: terminfo lines = %d columns = %d", lines, columns));

    *linep = (int) lines;
    *colp = (int) columns;
#endif
    if (useEnv || useTioctl) {
	int value;

#ifdef __EMX__
	{
	    int screendata[2];
	    _scrsize(screendata);
	    *colp = screendata[0];
	    *linep = ((sp != 0 && sp->_filtered)
		      ? 1
		      : screendata[1]);
	    T(("EMX screen size: environment LINES = %d COLUMNS = %d",
	       *linep, *colp));
	}
#endif
#if HAVE_SIZECHANGE
	/* try asking the OS */
	{
	    TERMINAL *termp = (TERMINAL *) TCB;
	    if (NC_ISATTY(termp->Filedes)) {
		STRUCT_WINSIZE size;

		errno = 0;
		do {
		    if (ioctl(termp->Filedes, IOCTL_WINSIZE, &size) >= 0) {
			*linep = ((sp != 0 && sp->_filtered)
				  ? 1
				  : WINSIZE_ROWS(size));
			*colp = WINSIZE_COLS(size);
			T(("SYS screen size: environment LINES = %d COLUMNS = %d",
			   *linep, *colp));
			break;
		    }
		} while
		    (errno == EINTR);
	    }
	}
#endif /* HAVE_SIZECHANGE */

	if (useEnv) {
	    if (useTioctl) {
		/*
		 * If environment variables are used, update them.
		 */
		if ((sp == 0 || !sp->_filtered) && _nc_getenv_num("LINES") > 0) {
		    _nc_setenv_num("LINES", *linep);
		}
		if (_nc_getenv_num("COLUMNS") > 0) {
		    _nc_setenv_num("COLUMNS", *colp);
		}
	    }

	    /*
	     * Finally, look for environment variables.
	     *
	     * Solaris lets users override either dimension with an environment
	     * variable.
	     */
	    if ((value = _nc_getenv_num("LINES")) > 0) {
		*linep = value;
		T(("screen size: environment LINES = %d", *linep));
	    }
	    if ((value = _nc_getenv_num("COLUMNS")) > 0) {
		*colp = value;
		T(("screen size: environment COLUMNS = %d", *colp));
	    }
	}

	/* if we can't get dynamic info about the size, use static */
	if (*linep <= 0) {
	    *linep = (int) lines;
	}
	if (*colp <= 0) {
	    *colp = (int) columns;
	}

	/* the ultimate fallback, assume fixed 24x80 size */
	if (*linep <= 0) {
	    *linep = 24;
	}
	if (*colp <= 0) {
	    *colp = 80;
	}

	/*
	 * Put the derived values back in the screen-size caps, so
	 * tigetnum() and tgetnum() will do the right thing.
	 */
	lines = (short) (*linep);
	columns = (short) (*colp);
    }

    T(("screen size is %dx%d", *linep, *colp));
    return OK;
}

static int
drv_getsize(TERMINAL_CONTROL_BLOCK * TCB, int *l, int *c)
{
    AssertTCB();
    assert(l != 0 && c != 0);
    *l = lines;
    *c = columns;
    return OK;
}

static int
drv_setsize(TERMINAL_CONTROL_BLOCK * TCB, int l, int c)
{
    AssertTCB();
    lines = (short) l;
    columns = (short) c;
    return OK;
}

static int
drv_sgmode(TERMINAL_CONTROL_BLOCK * TCB, int setFlag, TTY * buf)
{
    SCREEN *sp = TCB->csp;
    TERMINAL *_term = (TERMINAL *) TCB;
    int result = OK;

    AssertTCB();
    if (setFlag) {
	for (;;) {
	    if (SET_TTY(_term->Filedes, buf) != 0) {
		if (errno == EINTR)
		    continue;
		if (errno == ENOTTY) {
		    if (sp)
			sp->_notty = TRUE;
		}
		result = ERR;
	    }
	    break;
	}
    } else {
	for (;;) {
	    if (GET_TTY(_term->Filedes, buf) != 0) {
		if (errno == EINTR)
		    continue;
		result = ERR;
	    }
	    break;
	}
    }
    return result;
}

static int
drv_mode(TERMINAL_CONTROL_BLOCK * TCB, int progFlag, int defFlag)
{
    SCREEN *sp;
    TERMINAL *_term = (TERMINAL *) TCB;
    int code = ERR;

    AssertTCB();
    sp = TCB->csp;

    if (progFlag)		/* prog mode */
    {
	if (defFlag) {
	    /* def_prog_mode */
	    /*
	     * Turn off the XTABS bit in the tty structure if it was on.
	     */
	    if ((drv_sgmode(TCB, FALSE, &(_term->Nttyb)) == OK)) {
#ifdef TERMIOS
		_term->Nttyb.c_oflag &= (unsigned) ~OFLAGS_TABS;
#elif defined(EXP_WIN32_DRIVER)
		/* noop */
#else
		_term->Nttyb.sg_flags &= (unsigned) ~XTABS;
#endif
		code = OK;
	    }
	} else {
	    /* reset_prog_mode */
	    if (drv_sgmode(TCB, TRUE, &(_term->Nttyb)) == OK) {
		if (sp) {
		    if (sp->_keypad_on)
			_nc_keypad(sp, TRUE);
		}
		code = OK;
	    }
	}
    } else {			/* shell mode */
	if (defFlag) {
	    /* def_shell_mode */
	    /*
	     * If XTABS was on, remove the tab and backtab capabilities.
	     */
	    if (drv_sgmode(TCB, FALSE, &(_term->Ottyb)) == OK) {
#ifdef TERMIOS
		if (_term->Ottyb.c_oflag & OFLAGS_TABS)
		    tab = back_tab = NULL;
#elif defined(EXP_WIN32_DRIVER)
		/* noop */
#else
		if (_term->Ottyb.sg_flags & XTABS)
		    tab = back_tab = NULL;
#endif
		code = OK;
	    }
	} else {
	    /* reset_shell_mode */
	    if (sp) {
		_nc_keypad(sp, FALSE);
		NCURSES_SP_NAME(_nc_flush) (sp);
	    }
	    code = drv_sgmode(TCB, TRUE, &(_term->Ottyb));
	}
    }
    return (code);
}

static void
drv_wrap(SCREEN *sp)
{
    if (sp) {
	sp->_mouse_wrap(sp);
	NCURSES_SP_NAME(_nc_screen_wrap) (sp);
	NCURSES_SP_NAME(_nc_mvcur_wrap) (sp);	/* wrap up cursor addressing */
    }
}

static void
drv_release(TERMINAL_CONTROL_BLOCK * TCB GCC_UNUSED)
{
}

#  define SGR0_TEST(mode) (mode != 0) && (exit_attribute_mode == 0 || strcmp(mode, exit_attribute_mode))

static void
drv_screen_init(SCREEN *sp)
{
    TERMINAL_CONTROL_BLOCK *TCB = TCBOf(sp);

    AssertTCB();

    /*
     * Check for mismatched graphic-rendition capabilities.  Most SVr4
     * terminfo trees contain entries that have rmul or rmso equated to
     * sgr0 (Solaris curses copes with those entries).  We do this only
     * for curses, since many termcap applications assume that
     * smso/rmso and smul/rmul are paired, and will not function
     * properly if we remove rmso or rmul.  Curses applications
     * shouldn't be looking at this detail.
     */
    sp->_use_rmso = SGR0_TEST(exit_standout_mode);
    sp->_use_rmul = SGR0_TEST(exit_underline_mode);

    /*
     * Check whether we can optimize scrolling under dumb terminals in
     * case we do not have any of these capabilities, scrolling
     * optimization will be useless.
     */
    sp->_scrolling = ((scroll_forward && scroll_reverse) ||
		      ((parm_rindex ||
			parm_insert_line ||
			insert_line) &&
		       (parm_index ||
			parm_delete_line ||
			delete_line)));

    NCURSES_SP_NAME(baudrate) (sp);

    NCURSES_SP_NAME(_nc_mvcur_init) (sp);
    /* initialize terminal to a sane state */
    NCURSES_SP_NAME(_nc_screen_init) (sp);
}

static void
drv_init(TERMINAL_CONTROL_BLOCK * TCB)
{
    TERMINAL *trm;

    AssertTCB();

    trm = (TERMINAL *) TCB;

    TCB->info.initcolor = VALID_STRING(initialize_color);
    TCB->info.canchange = can_change;
    TCB->info.hascolor = ((VALID_NUMERIC(max_colors) && VALID_NUMERIC(max_pairs)
			   && (((set_foreground != NULL)
				&& (set_background != NULL))
			       || ((set_a_foreground != NULL)
				   && (set_a_background != NULL))
			       || set_color_pair)) ? TRUE : FALSE);

    TCB->info.caninit = !(exit_ca_mode && non_rev_rmcup);

    TCB->info.maxpairs = VALID_NUMERIC(max_pairs) ? max_pairs : 0;
    TCB->info.maxcolors = VALID_NUMERIC(max_colors) ? max_colors : 0;
    TCB->info.numlabels = VALID_NUMERIC(num_labels) ? num_labels : 0;
    TCB->info.labelwidth = VALID_NUMERIC(label_width) ? label_width : 0;
    TCB->info.labelheight = VALID_NUMERIC(label_height) ? label_height : 0;
    TCB->info.nocolorvideo = VALID_NUMERIC(no_color_video) ? no_color_video
	: 0;
    TCB->info.tabsize = VALID_NUMERIC(init_tabs) ? (int) init_tabs : 8;

    TCB->info.defaultPalette = hue_lightness_saturation ? _nc_hls_palette : _nc_cga_palette;

    /*
     * If an application calls setupterm() rather than initscr() or
     * newterm(), we will not have the def_prog_mode() call in
     * _nc_setupscreen().  Do it now anyway, so we can initialize the
     * baudrate.
     */
    if (NC_ISATTY(trm->Filedes)) {
	TCB->drv->td_mode(TCB, TRUE, TRUE);
    }
}

#define MAX_PALETTE	8
#define InPalette(n)	((n) >= 0 && (n) < MAX_PALETTE)

static void
drv_initpair(TERMINAL_CONTROL_BLOCK * TCB, int pair, int f, int b)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();

    if ((initialize_pair != NULL) && InPalette(f) && InPalette(b)) {
	const color_t *tp = InfoOf(sp).defaultPalette;

	TR(TRACE_ATTRS,
	   ("initializing pair: pair = %d, fg=(%d,%d,%d), bg=(%d,%d,%d)",
	    pair,
	    tp[f].red, tp[f].green, tp[f].blue,
	    tp[b].red, tp[b].green, tp[b].blue));

	NCURSES_PUTP2("initialize_pair",
		      TIPARM_7(initialize_pair,
			       pair,
			       tp[f].red, tp[f].green, tp[f].blue,
			       tp[b].red, tp[b].green, tp[b].blue));
    }
}

static int
default_fg(SCREEN *sp)
{
#if NCURSES_EXT_FUNCS
    return (sp != 0) ? sp->_default_fg : COLOR_WHITE;
#else
    return COLOR_WHITE;
#endif
}

static int
default_bg(SCREEN *sp)
{
#if NCURSES_EXT_FUNCS
    return sp != 0 ? sp->_default_bg : COLOR_BLACK;
#else
    return COLOR_BLACK;
#endif
}

static void
drv_initcolor(TERMINAL_CONTROL_BLOCK * TCB,
	      int color, int r, int g, int b)
{
    SCREEN *sp = TCB->csp;

    AssertTCB();
    if (initialize_color != NULL) {
	NCURSES_PUTP2("initialize_color",
		      TIPARM_4(initialize_color, color, r, g, b));
    }
}

static void
drv_do_color(TERMINAL_CONTROL_BLOCK * TCB,
	     int old_pair,
	     int pair,
	     int reverse,
	     NCURSES_SP_OUTC outc)
{
    SCREEN *sp = TCB->csp;
    int fg = COLOR_DEFAULT;
    int bg = COLOR_DEFAULT;
    int old_fg, old_bg;

    AssertTCB();
    if (sp == 0)
	return;

    if (pair < 0 || pair >= COLOR_PAIRS) {
	return;
    } else if (pair != 0) {
	if (set_color_pair) {
	    TPUTS_TRACE("set_color_pair");
	    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				    TIPARM_1(set_color_pair, pair), 1, outc);
	    return;
	} else if (sp != 0) {
	    _nc_pair_content(SP_PARM, pair, &fg, &bg);
	}
    }

    if (old_pair >= 0
	&& sp != 0
	&& _nc_pair_content(SP_PARM, old_pair, &old_fg, &old_bg) != ERR) {
	if ((isDefaultColor(fg) && !isDefaultColor(old_fg))
	    || (isDefaultColor(bg) && !isDefaultColor(old_bg))) {
#if NCURSES_EXT_FUNCS
	    /*
	     * A minor optimization - but extension.  If "AX" is specified in
	     * the terminal description, treat it as screen's indicator of ECMA
	     * SGR 39 and SGR 49, and assume the two sequences are independent.
	     */
	    if (sp->_has_sgr_39_49
		&& isDefaultColor(old_bg)
		&& !isDefaultColor(old_fg)) {
		NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx "\033[39m", 1, outc);
	    } else if (sp->_has_sgr_39_49
		       && isDefaultColor(old_fg)
		       && !isDefaultColor(old_bg)) {
		NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx "\033[49m", 1, outc);
	    } else
#endif
		drv_rescol(TCB);
	}
    } else {
	drv_rescol(TCB);
	if (old_pair < 0)
	    return;
    }

#if NCURSES_EXT_FUNCS
    if (isDefaultColor(fg))
	fg = default_fg(sp);
    if (isDefaultColor(bg))
	bg = default_bg(sp);
#endif

    if (reverse) {
	int xx = fg;
	fg = bg;
	bg = xx;
    }

    TR(TRACE_ATTRS, ("setting colors: pair = %d, fg = %d, bg = %d", pair,
		     fg, bg));

    if (!isDefaultColor(fg)) {
	drv_setcolor(TCB, TRUE, fg, outc);
    }
    if (!isDefaultColor(bg)) {
	drv_setcolor(TCB, FALSE, bg, outc);
    }
}

#define xterm_kmous "\033[M"
static void
init_xterm_mouse(SCREEN *sp)
{
    sp->_mouse_type = M_XTERM;
    sp->_mouse_xtermcap = NCURSES_SP_NAME(tigetstr) (NCURSES_SP_ARGx "XM");
    if (!VALID_STRING(sp->_mouse_xtermcap))
	sp->_mouse_xtermcap = "\033[?1000%?%p1%{1}%=%th%el%;";
}

static void
drv_initmouse(TERMINAL_CONTROL_BLOCK * TCB)
{
    SCREEN *sp;

    AssertTCB();
    SetSP();

    /* we know how to recognize mouse events under "xterm" */
    if (sp != 0) {
	if (NonEmpty(key_mouse)) {
	    init_xterm_mouse(sp);
	} else if (strstr(SP_TERMTYPE term_names, "xterm") != 0) {
	    if (_nc_add_to_try(&(sp->_keytry), xterm_kmous, KEY_MOUSE) == OK)
		init_xterm_mouse(sp);
	}
    }
}

static int
drv_testmouse(TERMINAL_CONTROL_BLOCK * TCB,
	      int delay
	      EVENTLIST_2nd(_nc_eventlist * evl))
{
    int rc = 0;
    SCREEN *sp;

    AssertTCB();
    SetSP();

#if USE_SYSMOUSE
    if ((sp->_mouse_type == M_SYSMOUSE)
	&& (sp->_sysmouse_head < sp->_sysmouse_tail)) {
	rc = TW_MOUSE;
    } else
#endif
    {
#ifdef EXP_WIN32_DRIVER
	rc = _nc_console_testmouse(sp,
				   _nc_console_handle(sp->_ifd),
				   delay
				   EVENTLIST_2nd(evl));
#else
	rc = TCBOf(sp)->drv->td_twait(TCBOf(sp),
				      TWAIT_MASK,
				      delay,
				      (int *) 0
				      EVENTLIST_2nd(evl));
#endif
#if USE_SYSMOUSE
	if ((sp->_mouse_type == M_SYSMOUSE)
	    && (sp->_sysmouse_head < sp->_sysmouse_tail)
	    && (rc == 0)
	    && (errno == EINTR)) {
	    rc |= TW_MOUSE;
	}
#endif
    }
    return rc;
}

static int
drv_mvcur(TERMINAL_CONTROL_BLOCK * TCB, int yold, int xold, int ynew, int xnew)
{
    SCREEN *sp = TCB->csp;
    AssertTCB();
    return NCURSES_SP_NAME(_nc_mvcur) (sp, yold, xold, ynew, xnew);
}

static void
drv_hwlabel(TERMINAL_CONTROL_BLOCK * TCB, int labnum, char *text)
{
    SCREEN *sp = TCB->csp;

    AssertTCB();
    if (labnum > 0 && labnum <= num_labels) {
	NCURSES_PUTP2("plab_norm",
		      TPARM_2(plab_norm, labnum, text));
    }
}

static void
drv_hwlabelOnOff(TERMINAL_CONTROL_BLOCK * TCB, int OnFlag)
{
    SCREEN *sp = TCB->csp;

    AssertTCB();
    if (OnFlag) {
	NCURSES_PUTP2("label_on", label_on);
    } else {
	NCURSES_PUTP2("label_off", label_off);
    }
}

static chtype
drv_conattr(TERMINAL_CONTROL_BLOCK * TCB)
{
    SCREEN *sp = TCB->csp;
    chtype attrs = A_NORMAL;

    AssertTCB();
    if (enter_alt_charset_mode)
	attrs |= A_ALTCHARSET;

    if (enter_blink_mode)
	attrs |= A_BLINK;

    if (enter_bold_mode)
	attrs |= A_BOLD;

    if (enter_dim_mode)
	attrs |= A_DIM;

    if (enter_reverse_mode)
	attrs |= A_REVERSE;

    if (enter_standout_mode)
	attrs |= A_STANDOUT;

    if (enter_protected_mode)
	attrs |= A_PROTECT;

    if (enter_secure_mode)
	attrs |= A_INVIS;

    if (enter_underline_mode)
	attrs |= A_UNDERLINE;

    if (sp && sp->_coloron)
	attrs |= A_COLOR;

#if USE_ITALIC
    if (enter_italics_mode)
	attrs |= A_ITALIC;
#endif

    return (attrs);
}

static void
drv_setfilter(TERMINAL_CONTROL_BLOCK * TCB)
{
    AssertTCB();

    /* *INDENT-EQLS* */
    clear_screen     = ABSENT_STRING;
    cursor_address   = ABSENT_STRING;
    cursor_down      = ABSENT_STRING;
    cursor_up        = ABSENT_STRING;
    parm_down_cursor = ABSENT_STRING;
    parm_up_cursor   = ABSENT_STRING;
    row_address      = ABSENT_STRING;
    cursor_home      = carriage_return;

    if (back_color_erase)
	clr_eos = ABSENT_STRING;
}

static void
drv_initacs(TERMINAL_CONTROL_BLOCK * TCB, chtype *real_map, chtype *fake_map)
{
    SCREEN *sp = TCB->csp;

    AssertTCB();
    assert(sp != 0);
    if (ena_acs != NULL) {
	NCURSES_PUTP2("ena_acs", ena_acs);
    }
#if NCURSES_EXT_FUNCS
    /*
     * Linux console "supports" the "PC ROM" character set by the coincidence
     * that smpch/rmpch and smacs/rmacs have the same values.  ncurses has
     * no codepage support (see SCO Merge for an example).  Outside of the
     * values defined in acsc, there are no definitions for the "PC ROM"
     * character set (assumed by some applications to be codepage 437), but we
     * allow those applications to use those codepoints.
     *
     * test/blue.c uses this feature.
     */
#define PCH_KLUDGE(a,b) (a != 0 && b != 0 && !strcmp(a,b))
    if (PCH_KLUDGE(enter_pc_charset_mode, enter_alt_charset_mode) &&
	PCH_KLUDGE(exit_pc_charset_mode, exit_alt_charset_mode)) {
	size_t i;
	for (i = 1; i < ACS_LEN; ++i) {
	    if (real_map[i] == 0) {
		real_map[i] = (chtype) i;
		if (real_map != fake_map) {
		    if (sp != 0)
			sp->_screen_acs_map[i] = TRUE;
		}
	    }
	}
    }
#endif

    if (acs_chars != NULL) {
	size_t i = 0;
	size_t length = strlen(acs_chars);

	while (i + 1 < length) {
	    if (acs_chars[i] != 0 && UChar(acs_chars[i]) < ACS_LEN) {
		real_map[UChar(acs_chars[i])] = UChar(acs_chars[i + 1]) | A_ALTCHARSET;
		T(("#%d real_map[%s] = %s",
		   (int) i,
		   _tracechar(UChar(acs_chars[i])),
		   _tracechtype(real_map[UChar(acs_chars[i])])));
		if (sp != 0) {
		    sp->_screen_acs_map[UChar(acs_chars[i])] = TRUE;
		}
	    }
	    i += 2;
	}
    }
#ifdef TRACE
    /* Show the equivalent mapping, noting if it does not match the
     * given attribute, whether by re-ordering or duplication.
     */
    if (USE_TRACEF(TRACE_CALLS)) {
	size_t n, m;
	char show[ACS_LEN * 2 + 1];
	for (n = 1, m = 0; n < ACS_LEN; n++) {
	    if (real_map[n] != 0) {
		show[m++] = (char) n;
		show[m++] = (char) ChCharOf(real_map[n]);
	    }
	}
	show[m] = 0;
	if (acs_chars == NULL || strcmp(acs_chars, show))
	    _tracef("%s acs_chars %s",
		    (acs_chars == NULL) ? "NULL" : "READ",
		    _nc_visbuf(acs_chars));
	_tracef("%s acs_chars %s",
		(acs_chars == NULL)
		? "NULL"
		: (strcmp(acs_chars, show)
		   ? "DIFF"
		   : "SAME"),
		_nc_visbuf(show));
	_nc_unlock_global(tracef);
    }
#endif /* TRACE */
}

#define ENSURE_TINFO(sp) (TCBOf(sp)->drv->isTerminfo)

NCURSES_EXPORT(void)
_nc_cookie_init(SCREEN *sp)
{
    bool support_cookies = USE_XMC_SUPPORT;
    TERMINAL_CONTROL_BLOCK *TCB = (TERMINAL_CONTROL_BLOCK *) (sp->_term);

    if (sp == 0 || !ENSURE_TINFO(sp))
	return;

#if USE_XMC_SUPPORT
    /*
     * If we have no magic-cookie support compiled-in, or if it is suppressed
     * in the environment, reset the support-flag.
     */
    if (magic_cookie_glitch >= 0) {
	if (getenv("NCURSES_NO_MAGIC_COOKIE") != 0) {
	    support_cookies = FALSE;
	}
    }
#endif

    if (!support_cookies && magic_cookie_glitch >= 0) {
	T(("will disable attributes to work w/o magic cookies"));
    }

    if (magic_cookie_glitch > 0) {	/* tvi, wyse */

	sp->_xmc_triggers = sp->_ok_attributes & XMC_CONFLICT;
#if 0
	/*
	 * We "should" treat colors as an attribute.  The wyse350 (and its
	 * clones) appear to be the only ones that have both colors and magic
	 * cookies.
	 */
	if (has_colors()) {
	    sp->_xmc_triggers |= A_COLOR;
	}
#endif
	sp->_xmc_suppress = sp->_xmc_triggers & (chtype) ~(A_BOLD);

	T(("magic cookie attributes %s", _traceattr(sp->_xmc_suppress)));
	/*
	 * Supporting line-drawing may be possible.  But make the regular
	 * video attributes work first.
	 */
	acs_chars = ABSENT_STRING;
	ena_acs = ABSENT_STRING;
	enter_alt_charset_mode = ABSENT_STRING;
	exit_alt_charset_mode = ABSENT_STRING;
#if USE_XMC_SUPPORT
	/*
	 * To keep the cookie support simple, suppress all of the optimization
	 * hooks except for clear_screen and the cursor addressing.
	 */
	if (support_cookies) {
	    clr_eol = ABSENT_STRING;
	    clr_eos = ABSENT_STRING;
	    set_attributes = ABSENT_STRING;
	}
#endif
    } else if (magic_cookie_glitch == 0) {	/* hpterm */
    }

    /*
     * If magic cookies are not supported, cancel the strings that set
     * video attributes.
     */
    if (!support_cookies && magic_cookie_glitch >= 0) {
	magic_cookie_glitch = ABSENT_NUMERIC;
	set_attributes = ABSENT_STRING;
	enter_blink_mode = ABSENT_STRING;
	enter_bold_mode = ABSENT_STRING;
	enter_dim_mode = ABSENT_STRING;
	enter_reverse_mode = ABSENT_STRING;
	enter_standout_mode = ABSENT_STRING;
	enter_underline_mode = ABSENT_STRING;
    }

    /* initialize normal acs before wide, since we use mapping in the latter */
#if !USE_WIDEC_SUPPORT
    if (_nc_unicode_locale() && _nc_locale_breaks_acs(sp->_term)) {
	acs_chars = NULL;
	ena_acs = NULL;
	enter_alt_charset_mode = NULL;
	exit_alt_charset_mode = NULL;
	set_attributes = NULL;
    }
#endif
}

static int
drv_twait(TERMINAL_CONTROL_BLOCK * TCB,
	  int mode,
	  int milliseconds,
	  int *timeleft
	  EVENTLIST_2nd(_nc_eventlist * evl))
{
    SCREEN *sp;

    AssertTCB();
    SetSP();
#ifdef EXP_WIN32_DRIVER
    return _nc_console_twait(sp,
			     _nc_console_handle(sp->_ifd),
			     mode,
			     milliseconds,
			     timeleft EVENTLIST_2nd(evl));
#else
    return _nc_timed_wait(sp, mode, milliseconds, timeleft EVENTLIST_2nd(evl));
#endif
}

static int
drv_read(TERMINAL_CONTROL_BLOCK * TCB, int *buf)
{
    SCREEN *sp;
    int n;
#ifndef EXP_WIN32_DRIVER
    unsigned char c2 = 0;
#endif

    AssertTCB();
    assert(buf);
    SetSP();

    _nc_set_read_thread(TRUE);
#ifdef EXP_WIN32_DRIVER
    n = _nc_console_read(sp,
			 _nc_console_handle(sp->_ifd),
			 buf);
#else
    n = (int) read(sp->_ifd, &c2, (size_t) 1);
#endif
    _nc_set_read_thread(FALSE);
#ifndef EXP_WIN32_DRIVER
    *buf = (int) c2;
#endif
    return n;
}

static int
drv_nap(TERMINAL_CONTROL_BLOCK * TCB GCC_UNUSED, int ms)
{
#if HAVE_NANOSLEEP
    {
	struct timespec request, remaining;
	request.tv_sec = ms / 1000;
	request.tv_nsec = (ms % 1000) * 1000000;
	while (nanosleep(&request, &remaining) == -1
	       && errno == EINTR) {
	    request = remaining;
	}
    }
#elif defined(EXP_WIN32_DRIVER)
    Sleep((DWORD) ms);
#else
    _nc_timed_wait(0, 0, ms, (int *) 0 EVENTLIST_2nd(0));
#endif
    return OK;
}

static int
__nc_putp(SCREEN *sp, const char *name GCC_UNUSED, const char *value)
{
    int rc = ERR;

    if (value) {
	rc = NCURSES_PUTP2(name, value);
    }
    return rc;
}

static int
__nc_putp_flush(SCREEN *sp, const char *name, const char *value)
{
    int rc = __nc_putp(sp, name, value);
    if (rc != ERR) {
	NCURSES_SP_NAME(_nc_flush) (sp);
    }
    return rc;
}

static int
drv_kpad(TERMINAL_CONTROL_BLOCK * TCB, int flag)
{
    int ret = ERR;
    SCREEN *sp;

    AssertTCB();

    sp = TCB->csp;

    if (sp) {
	if (flag) {
	    (void) __nc_putp_flush(sp, "keypad_xmit", keypad_xmit);
	} else if (!flag && keypad_local) {
	    (void) __nc_putp_flush(sp, "keypad_local", keypad_local);
	}
	if (flag && !sp->_tried) {
	    _nc_init_keytry(sp);
	    sp->_tried = TRUE;
	}
	ret = OK;
    }

    return ret;
}

static int
drv_keyok(TERMINAL_CONTROL_BLOCK * TCB, int c, int flag)
{
    SCREEN *sp;
    int code = ERR;
    int count = 0;
    char *s;

    AssertTCB();
    SetSP();

    if (c >= 0) {
	unsigned ch = (unsigned) c;
	if (flag) {
	    while ((s = _nc_expand_try(sp->_key_ok,
				       ch, &count, (size_t) 0)) != 0) {
		if (_nc_remove_key(&(sp->_key_ok), ch)) {
		    code = _nc_add_to_try(&(sp->_keytry), s, ch);
		    free(s);
		    count = 0;
		    if (code != OK)
			break;
		} else {
		    free(s);
		}
	    }
	} else {
	    while ((s = _nc_expand_try(sp->_keytry,
				       ch, &count, (size_t) 0)) != 0) {
		if (_nc_remove_key(&(sp->_keytry), ch)) {
		    code = _nc_add_to_try(&(sp->_key_ok), s, ch);
		    free(s);
		    count = 0;
		    if (code != OK)
			break;
		} else {
		    free(s);
		}
	    }
	}
    }
    return (code);
}

static int
drv_cursorSet(TERMINAL_CONTROL_BLOCK * TCB, int vis)
{
    SCREEN *sp;
    int code = ERR;

    AssertTCB();
    SetSP();

    T((T_CALLED("tinfo:drv_cursorSet(%p,%d)"), (void *) SP_PARM, vis));

    if (SP_PARM != 0 && IsTermInfo(SP_PARM)) {
	switch (vis) {
	case 2:
	    code = NCURSES_PUTP2_FLUSH("cursor_visible", cursor_visible);
	    break;
	case 1:
	    code = NCURSES_PUTP2_FLUSH("cursor_normal", cursor_normal);
	    break;
	case 0:
	    code = NCURSES_PUTP2_FLUSH("cursor_invisible", cursor_invisible);
	    break;
	}
    } else {
	code = ERR;
    }
    returnCode(code);
}

static bool
drv_kyExist(TERMINAL_CONTROL_BLOCK * TCB, int key)
{
    bool res = FALSE;

    AssertTCB();
    if (TCB->csp)
	res = TINFO_HAS_KEY(TCB->csp, key) == 0 ? FALSE : TRUE;

    return res;
}

NCURSES_EXPORT_VAR (TERM_DRIVER) _nc_TINFO_DRIVER = {
    TRUE,
	drv_Name,		/* Name */
	drv_CanHandle,		/* CanHandle */
	drv_init,		/* init */
	drv_release,		/* release */
	drv_size,		/* size */
	drv_sgmode,		/* sgmode */
	drv_conattr,		/* conattr */
	drv_mvcur,		/* hwcur */
	drv_mode,		/* mode */
	drv_rescol,		/* rescol */
	drv_rescolors,		/* rescolors */
	drv_setcolor,		/* color */
	drv_dobeepflash,	/* doBeepOrFlash */
	drv_initpair,		/* initpair */
	drv_initcolor,		/* initcolor */
	drv_do_color,		/* docolor */
	drv_initmouse,		/* initmouse */
	drv_testmouse,		/* testmouse */
	drv_setfilter,		/* setfilter */
	drv_hwlabel,		/* hwlabel */
	drv_hwlabelOnOff,	/* hwlabelOnOff */
	drv_doupdate,		/* update */
	drv_defaultcolors,	/* defaultcolors */
	drv_print,		/* print */
	drv_getsize,		/* getsize */
	drv_setsize,		/* setsize */
	drv_initacs,		/* initacs */
	drv_screen_init,	/* scinit */
	drv_wrap,		/* scexit */
	drv_twait,		/* twait  */
	drv_read,		/* read */
	drv_nap,		/* nap */
	drv_kpad,		/* kpad */
	drv_keyok,		/* kyOk */
	drv_kyExist,		/* kyExist */
	drv_cursorSet		/* cursorSet */
};

#ifdef EXP_WIN32_DRIVER
/*
 * The terminfo driver is mandatory and must always be present.
 * So this is the natural place for the driver initialisation
 * logic.
 */

typedef struct DriverEntry {
    const char *name;
    TERM_DRIVER *driver;
} DRIVER_ENTRY;

static DRIVER_ENTRY DriverTable[] =
{
#ifdef _NC_WINDOWS
    {"win32console", &_nc_WIN_DRIVER},
#endif
    {"tinfo", &_nc_TINFO_DRIVER}	/* must be last */
};

NCURSES_EXPORT(int)
_nc_get_driver(TERMINAL_CONTROL_BLOCK * TCB, const char *name, int *errret)
{
    int code = ERR;
    size_t i;
    TERM_DRIVER *res = (TERM_DRIVER *) 0;
    TERM_DRIVER *use = 0;

    T((T_CALLED("_nc_get_driver(%p, %s, %p)"),
       (void *) TCB, NonNull(name), (void *) errret));

    assert(TCB != 0);

    for (i = 0; i < SIZEOF(DriverTable); i++) {
	res = DriverTable[i].driver;
#ifdef _NC_WINDOWS
	if ((i + 1) == SIZEOF(DriverTable)) {
	    /* For Windows >= 10.0.17763 Windows Console interface implements
	       virtual Terminal functionality.
	       If on Windows td_CanHandle returned FALSE although the terminal
	       name is empty, we default to ms-terminal as tinfo TERM type.
	     */
	    if (name == 0 || *name == 0 || (strcmp(name, "unknown") == 0)) {
		name = MS_TERMINAL;
		T(("Set TERM=%s", name));
	    }
	}
#endif
	if (strcmp(DriverTable[i].name, res->td_name(TCB)) == 0) {
	    if (res->td_CanHandle(TCB, name, errret)) {
		use = res;
		break;
	    }
	}
    }
    if (use != 0) {
	TCB->drv = use;
	code = OK;
    }
    returnCode(code);
}
#endif /* EXP_WIN32_DRIVER */
