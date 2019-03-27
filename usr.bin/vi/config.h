/* $Id: config.h.in,v 9.5 2013/03/11 01:20:53 zy Exp $ */
/* $FreeBSD$ */

/* Define if you want a debugging version. */
/* #undef DEBUG */

/* Define when using wide characters */
/* #define USE_WIDECHAR set by Makefile */

/* Define when iconv can be used */
/* #define USE_ICONV set by Makefile */

/* Define when the 2nd argument of iconv(3) is not const */
/* #undef ICONV_TRADITIONAL */

/* Define if you have <libutil.h> */
#define HAVE_LIBUTIL_H

/* Define if you have <ncurses.h> */
#define HAVE_NCURSES_H

/* Define if you have <term.h> */
#define HAVE_TERM_H
