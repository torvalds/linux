/****************************************************************************
 * Copyright 2018-2020,2021 Thomas E. Dickey                                *
 * Copyright 2017 Free Software Foundation, Inc.                            *
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
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: report_offsets.c,v 1.1 2023/10/17 09:52:08 nicm Exp $")

#define show_size(type) \
	flag = 0; \
	last = 0; \
	printf("%5lu   " #type "\n", (unsigned long)sizeof(type))
#define show_offset(type,member) \
	next = (unsigned long)offsetof(type,member); \
	if (last > next) \
		printf("?? incorrect order for " #type "." #member "\n"); \
	printf("%5lu %c " #type "." #member "\n", next, flag ? *flag : ' '); \
	last = next; \
	flag = 0

#if NCURSES_WIDECHAR && NCURSES_EXT_COLORS
#define show_COLORS(type,member) { flag = "c"; show_offset(type,member); }
#else
#define show_COLORS(type,member)	/* nothing */
#endif

#ifdef USE_TERM_DRIVER
#define show_DRIVER(type,member) { flag = "d"; show_offset(type,member); }
#else
#define show_DRIVER(type,member)	/* nothing */
#endif

#if NO_LEAKS
#define show_MLEAKS(type,member) { flag = "L"; show_offset(type,member); }
#else
#define show_MLEAKS(type,member)	/* nothing */
#endif

#ifdef USE_TERM_DRIVER
#define show_NORMAL(type,member)	/* nothing */
#else
#define show_NORMAL(type,member) { flag = "n"; show_offset(type,member); }
#endif

#define show_OPTION(type,member) { flag = "+"; show_offset(type,member); }

#if USE_REENTRANT
#define show_REENTR(type,member) { flag = "r"; show_offset(type,member); }
#else
#define show_REENTR(type,member)	/* nothing */
#endif

#if NCURSES_SP_FUNCS
#define show_SPFUNC(type,member) { flag = "s"; show_offset(type,member); }
#else
#define show_SPFUNC(type,member)	/* nothing */
#endif

#ifdef USE_PTHREADS
#define show_THREAD(type,member) { flag = "t"; show_offset(type,member); }
#else
#define show_THREAD(type,member)	/* nothing */
#endif

#ifdef TRACE
#define show_TRACES(type,member) { flag = "T"; show_offset(type,member); }
#else
#define show_TRACES(type,member)	/* nothing */
#endif

#if USE_WIDEC_SUPPORT
#define show_WIDECH(type,member) { flag = "w"; show_offset(type,member); }
#else
#define show_WIDECH(type,member)	/* nothing */
#endif

int
main(void)
{
    const char *flag = 0;
    unsigned long last, next;

    printf("Size/offsets of data structures:\n");

    show_size(attr_t);
    show_size(chtype);
#if USE_WIDEC_SUPPORT
    show_size(cchar_t);
#endif
    show_size(mmask_t);
    show_size(MEVENT);
    show_size(NCURSES_BOOL);

    printf("\n");
    show_size(SCREEN);
    show_offset(SCREEN, _ifd);
    show_offset(SCREEN, _fifo);
    show_offset(SCREEN, _fifohead);
    show_offset(SCREEN, _direct_color);
    show_offset(SCREEN, _panelHook);
    show_offset(SCREEN, jump);
    show_offset(SCREEN, rsp);
#if NCURSES_NO_PADDING
    show_OPTION(SCREEN, _no_padding);
#endif
#if USE_HARD_TABS
    show_OPTION(SCREEN, _ht_cost);
#endif
#if USE_ITALIC
    show_OPTION(SCREEN, _use_ritm);
#endif
#if USE_KLIBC_KBD
    show_OPTION(SCREEN, _extended_key);
#endif
#if NCURSES_EXT_FUNCS
    show_OPTION(SCREEN, _assumed_color);
#endif
#if USE_GPM_SUPPORT
    show_OPTION(SCREEN, _mouse_gpm_loaded);
#ifdef HAVE_LIBDL
    show_OPTION(SCREEN, _dlopen_gpm);
#endif
#endif
#if USE_EMX_MOUSE
    show_OPTION(SCREEN, _emxmouse_wfd);
#endif
#if USE_SYSMOUSE
    show_OPTION(SCREEN, _sysmouse_fifo);
#endif
    show_DRIVER(SCREEN, _drv_mouse_fifo);
#if USE_SIZECHANGE
    show_OPTION(SCREEN, _resize);
#endif
    show_DRIVER(SCREEN, _windowlist);
    show_REENTR(SCREEN, _ttytype);
    show_SPFUNC(SCREEN, use_tioctl);
    show_WIDECH(SCREEN, _screen_acs_fix);
    show_COLORS(SCREEN, _ordered_pairs);
    show_TRACES(SCREEN, tracechr_buf);

    printf("\n");
    show_size(TERMINAL);
    show_offset(TERMINAL, type);
    show_offset(TERMINAL, Filedes);
    show_offset(TERMINAL, Ottyb);
    show_offset(TERMINAL, Nttyb);
    show_offset(TERMINAL, _baudrate);
    show_offset(TERMINAL, _termname);
    show_offset(TERMINAL, tparm_state);
#if HAVE_INIT_EXTENDED_COLOR
    show_COLORS(TERMINAL, type2);
#endif

    printf("\n");
    show_size(TERMTYPE);
#if NCURSES_XNAMES
    show_OPTION(TERMTYPE, ext_str_table);
    show_OPTION(TERMTYPE, ext_Strings);
#endif

    printf("\n");
    show_size(TPARM_STATE);
    show_offset(TPARM_STATE, stack);
    show_offset(TPARM_STATE, stack_ptr);
    show_offset(TPARM_STATE, out_buff);
    show_offset(TPARM_STATE, fmt_buff);
    show_offset(TPARM_STATE, static_vars);
    show_TRACES(TPARM_STATE, tname);

    printf("\n");
    show_size(WINDOW);
    show_WIDECH(WINDOW, _bkgrnd);
    show_COLORS(WINDOW, _color);

    printf("\n");
    show_size(NCURSES_GLOBALS);
    show_offset(NCURSES_GLOBALS, init_signals);
    show_offset(NCURSES_GLOBALS, tgetent_cache);
    show_offset(NCURSES_GLOBALS, dbd_vars);
#if HAVE_TSEARCH
    show_offset(NCURSES_GLOBALS, cached_tparm);
#endif
    show_DRIVER(NCURSES_GLOBALS, term_driver);
    show_NORMAL(NCURSES_GLOBALS, _nc_windowlist);
#if USE_HOME_TERMINFO
    show_OPTION(NCURSES_GLOBALS, home_terminfo);
#endif
#if !USE_SAFE_SPRINTF
    show_OPTION(NCURSES_GLOBALS, safeprint_rows);
#endif
    show_THREAD(NCURSES_GLOBALS, mutex_curses);
#if USE_PTHREADS_EINTR
    show_THREAD(NCURSES_GLOBALS, read_thread);
#endif
    show_WIDECH(NCURSES_GLOBALS, key_name);
    show_TRACES(NCURSES_GLOBALS, trace_opened);
    show_MLEAKS(NCURSES_GLOBALS, leak_checking);

    printf("\n");
    show_size(NCURSES_PRESCREEN);
    show_offset(NCURSES_PRESCREEN, tparm_state);
    show_offset(NCURSES_PRESCREEN, saved_tty);
    show_offset(NCURSES_PRESCREEN, use_tioctl);
    show_offset(NCURSES_PRESCREEN, _outch);
#ifndef USE_SP_RIPOFF
    show_NORMAL(NCURSES_PRESCREEN, rippedoff);
#endif
#if NCURSES_NO_PADDING
    show_OPTION(NCURSES_PRESCREEN, _no_padding);
#endif
#if BROKEN_LINKER
    show_offset(NCURSES_PRESCREEN, real_acs_map);
#else
    show_REENTR(NCURSES_PRESCREEN, real_acs_map);
#endif
#if BROKEN_LINKER || USE_REENTRANT
    show_TRACES(NCURSES_PRESCREEN, _outchars);
#endif

    return EXIT_SUCCESS;
}
