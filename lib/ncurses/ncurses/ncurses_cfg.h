/* include/ncurses_cfg.h.  Generated automatically by configure.  */
/****************************************************************************
 * Copyright (c) 1998-2004,2005 Free Software Foundation, Inc.              *
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
 * $Id: ncurses_cfg.hin,v 1.7 2005/01/02 01:26:58 tom Exp $
 *
 * This is a template-file used to generate the "ncurses_cfg.h" file.
 *
 * Rather than list every definition, the configuration script substitutes the
 * definitions that it finds using 'sed'.  You need a patch (original date
 * 971222) to autoconf 2.12 or 2.13 to do this.
 *
 * See:
 *	http://invisible-island.net/autoconf/
 *	ftp://invisible-island.net/autoconf/
 */

/* $FreeBSD$ */

#ifndef NC_CONFIG_H
#define NC_CONFIG_H

#define SYSTEM_NAME "FreeBSD"
#if 0
#include <stdlib.h>
#endif
#define HAVE_LONG_FILE_NAMES 1
#define MIXEDCASE_FILENAMES 1
#define USE_SYSMOUSE 1
#define HAVE_BIG_CORE 1
#define TERMPATH "/etc/termcap:/usr/share/misc/termcap"
#define USE_GETCAP 1
#define HAVE_REMOVE 1
#define HAVE_UNLINK 1
#define HAVE_LINK 1
#define HAVE_SYMLINK 1
#define USE_LINKS 1
#define BSD_TPUTS 1
#define HAVE_LANGINFO_CODESET 1
#define HAVE_FSEEKO 1
#define HAVE_ASSUME_DEFAULT_COLORS 1
#define HAVE_CURSES_VERSION 1
#define HAVE_HAS_KEY 1
#define HAVE_RESIZETERM 1
#define HAVE_RESIZE_TERM 1
#define HAVE_TERM_ENTRY_H 1
#define HAVE_USE_DEFAULT_COLORS 1
#define HAVE_USE_SCREEN 1
#define HAVE_USE_WINDOW 1
#define HAVE_WRESIZE 1
#define NCURSES_EXT_FUNCS 1
#define NCURSES_NO_PADDING 1
#define STDC_HEADERS 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STRINGS_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDINT_H 1
#define HAVE_UNISTD_H 1
#define SIZEOF_SIGNED_CHAR 1
#define USE_SIGWINCH 1
#define USE_ASSUMED_COLOR 1
#define USE_HASHMAP 1
#define USE_COLORFGBG 1
#define NCURSES_WRAP_PREFIX "_nc_"
#define GCC_SCANF 1
#define GCC_SCANFLIKE(fmt,var) __attribute__((format(scanf,fmt,var)))
#define GCC_PRINTF 1
#define GCC_PRINTFLIKE(fmt,var) __attribute__((format(printf,fmt,var)))
#define GCC_UNUSED __attribute__((unused))
#define GCC_NORETURN __attribute__((noreturn))
#define HAVE_NC_ALLOC_H 1
#define HAVE_GETTIMEOFDAY 1
#define STDC_HEADERS 1
#define HAVE_DIRENT_H 1
#define TIME_WITH_SYS_TIME 1
#define HAVE_REGEX_H_FUNCS 1
#define HAVE_FCNTL_H 1
#define HAVE_GETOPT_H 1
#define HAVE_LIMITS_H 1
#define HAVE_LOCALE_H 1
#define HAVE_MATH_H 1
#define HAVE_POLL_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_POLL_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TIMES_H 1
#define HAVE_TTYENT_H 1
#define HAVE_UNISTD_H 1
#define HAVE_WCTYPE_H 1
#define HAVE_UNISTD_H 1
#define HAVE_GETOPT_H 1
#define HAVE_GETOPT_HEADER 1
#define HAVE_SYS_TIME_SELECT 1
#define SIG_ATOMIC_T volatile sig_atomic_t
#define TYPEOF_CHTYPE int
#define HAVE_ERRNO 1
#define HAVE_GETCWD 1
#define HAVE_GETEGID 1
#define HAVE_GETEUID 1
#define HAVE_GETTTYNAM 1
#define HAVE_ISSETUGID 1
#define HAVE_POLL 1
#define HAVE_PUTENV 1
#define HAVE_REMOVE 1
#define HAVE_SELECT 1
#define HAVE_SETBUF 1
#define HAVE_SETBUFFER 1
#define HAVE_SETENV 1
#define HAVE_SETVBUF 1
#define HAVE_SIGACTION 1
#define HAVE_SIGVEC 1
#define HAVE_STRDUP 1
#define HAVE_STRSTR 1
#define HAVE_TCGETPGRP 1
#define HAVE_TIMES 1
#define HAVE_VSNPRINTF 1
#define HAVE_BSD_CGETENT 1
#define CGETENT_CONST const
#define HAVE_ISASCII 1
#define HAVE_NANOSLEEP 1
#define HAVE_TERMIOS_H 1
#define HAVE_UNISTD_H 1
#define HAVE_TCGETATTR 1
#define HAVE_VSSCANF 1
#define HAVE_MKSTEMP 1
#define HAVE_SIZECHANGE 1
#define HAVE_WORKING_POLL 1
#define HAVE_VA_COPY 1
#define HAVE___VA_COPY 1
#define HAVE_UNISTD_H 1
#define HAVE_FORK 1
#define HAVE_VFORK 1
#define HAVE_WORKING_VFORK 1
#define HAVE_WORKING_FORK 1
#define USE_OPENPTY_HEADER <libutil.h>
#define USE_XTERM_PTY 1
#define HAVE_TYPEINFO 1
#define HAVE_IOSTREAM 1
#define IOSTREAM_NAMESPACE 1
#define CPP_HAS_STATIC_CAST 1
#define HAVE_SLK_COLOR 1
#define HAVE_PANEL_H 1
#define HAVE_LIBPANEL 1
#define HAVE_MENU_H 1
#define HAVE_LIBMENU 1
#define HAVE_FORM_H 1
#define HAVE_LIBFORM 1
#define NCURSES_PATHSEP ':'
#define NCURSES_VERSION_STRING "5.9.20140222"
#define NCURSES_OSPEED_COMPAT 1

/*
 * Begin FreeBSD-specific changes
 */
/* Support ENABLE_WIDEC */
#ifdef ENABLE_WIDEC
#define USE_WIDEC_SUPPORT 1
#define NCURSES_WIDECHAR 1
#define HAVE_PUTWC 1
#define HAVE_BTOWC 1
#define HAVE_WCTOB 1
#define HAVE_MBTOWC 1
#define HAVE_WCTOMB 1
#define HAVE_MBLEN 1
#define HAVE_MBRLEN 1
#define HAVE_MBRTOWC 1
#define HAVE_WCSRTOMBS 1
#define HAVE_MBSRTOWCS 1
#define HAVE_WCSTOMBS 1
#define HAVE_MBSTOWCS 1
#define NEED_WCHAR_H 1
#define SIZEOF_WCHAR_T 4
#endif
/*
 * End FreeBSD-specific changes
 */

#include <ncurses_def.h>

	/* The C compiler may not treat these properly but C++ has to */
#ifdef __cplusplus
#undef const
#undef inline
#else
#if defined(lint) || defined(TRACE)
#undef inline
#define inline /* nothing */
#endif
#endif

	/* On HP-UX, the C compiler doesn't grok mbstate_t without
	   -D_XOPEN_SOURCE=500. However, this causes problems on
	   IRIX. So, we #define mbstate_t to int in configure.in
	   only for the C compiler if needed. */
#ifndef __cplusplus
#ifdef NEED_MBSTATE_T_DEF
#define mbstate_t int
#endif
#endif

#endif /* NC_CONFIG_H */
