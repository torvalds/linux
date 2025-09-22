/* $OpenBSD: curses.h,v 1.64 2023/10/17 09:52:08 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2021,2023 Thomas E. Dickey                                *
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

/* $Id: curses.h,v 1.64 2023/10/17 09:52:08 nicm Exp $ */

#ifndef __NCURSES_H
#define __NCURSES_H

/*
 The symbols beginning NCURSES_ or USE_ are configuration choices.
 A few of the former can be overridden by applications at compile-time.
 Most of the others correspond to configure-script options (or checks
 by the configure-script for features of the system on which it is built).

 These symbols can be overridden by applications at compile-time:
 NCURSES_NOMACROS suppresses macro definitions in favor of functions
 NCURSES_WATTR_MACROS suppresses wattr_* macro definitions
 NCURSES_WIDECHAR is an alternative for declaring wide-character functions.

 These symbols are used only when building ncurses:
 NCURSES_ATTR_T
 NCURSES_FIELD_INTERNALS
 NCURSES_INTERNALS

 These symbols are set by the configure script:
 NCURSES_ENABLE_STDBOOL_H
 NCURSES_EXPANDED
 NCURSES_EXT_COLORS
 NCURSES_EXT_FUNCS
 NCURSES_EXT_PUTWIN
 NCURSES_NO_PADDING
 NCURSES_OSPEED_COMPAT
 NCURSES_PATHSEP
 NCURSES_REENTRANT
 */

#define CURSES 1
#define CURSES_H 1

/* These are defined only in curses.h, and are used for conditional compiles */
#define NCURSES_VERSION_MAJOR 6
#define NCURSES_VERSION_MINOR 4
#define NCURSES_VERSION_PATCH 20230826

/* This is defined in more than one ncurses header, for identification */
#undef  NCURSES_VERSION
#define NCURSES_VERSION "6.4"

/*
 * Identify the mouse encoding version.
 */
#define NCURSES_MOUSE_VERSION 2

/*
 * Definitions to facilitate DLL's.
 */
#include <ncurses_dll.h>

/*
 * Extra headers.
 */
#if 1
#include <stdint.h>
#endif

#ifdef __cplusplus
#else
#if 0
#include <stdnoreturn.h>
#undef GCC_NORETURN
#define GCC_NORETURN _Noreturn
#endif
#endif

/*
 * User-definable tweak to disable the include of <stdbool.h>.
 */
#ifndef NCURSES_ENABLE_STDBOOL_H
#define NCURSES_ENABLE_STDBOOL_H 1
#endif

/*
 * NCURSES_ATTR_T is used to quiet compiler warnings when building ncurses
 * configured using --disable-macros.
 */
#ifndef NCURSES_ATTR_T
#define NCURSES_ATTR_T int
#endif

/*
 * Expands to 'const' if ncurses is configured using --enable-const.  Note that
 * doing so makes it incompatible with other implementations of X/Open Curses.
 */
#undef  NCURSES_CONST
#define NCURSES_CONST const

#undef NCURSES_INLINE
#define NCURSES_INLINE inline

/*
 * The standard type used for color values, and for color-pairs.  The latter
 * allows the curses library to enumerate the combinations of foreground and
 * background colors used by an application, and is normally the product of the
 * total foreground and background colors.
 *
 * X/Open uses "short" for both of these types, ultimately because they are
 * numbers from the SVr4 terminal database, which uses 16-bit signed values.
 */
#undef	NCURSES_COLOR_T
#define	NCURSES_COLOR_T short

#undef	NCURSES_PAIRS_T
#define	NCURSES_PAIRS_T short

/*
 * Definitions used to make WINDOW and similar structs opaque.
 */
#ifndef NCURSES_INTERNALS
#define NCURSES_OPAQUE       0
#define NCURSES_OPAQUE_FORM  0
#define NCURSES_OPAQUE_MENU  0
#define NCURSES_OPAQUE_PANEL 0
#endif

/*
 * Definition used to optionally suppress wattr* macros to help with the
 * transition from ncurses5 to ncurses6 by allowing the header files to
 * be shared across development packages for ncursesw in both ABIs.
 */
#ifndef NCURSES_WATTR_MACROS
#define NCURSES_WATTR_MACROS 0
#endif

/*
 * The reentrant code relies on the opaque setting, but adds features.
 */
#ifndef NCURSES_REENTRANT
#define NCURSES_REENTRANT 0
#endif

/*
 * In certain environments, we must work around linker problems for data
 */
#undef NCURSES_BROKEN_LINKER
#if 0
#define NCURSES_BROKEN_LINKER 1
#endif

/*
 * Control whether bindings for interop support are added.
 */
#undef	NCURSES_INTEROP_FUNCS
#define	NCURSES_INTEROP_FUNCS 1

/*
 * The internal type used for window dimensions.
 */
#undef	NCURSES_SIZE_T
#define	NCURSES_SIZE_T short

/*
 * Control whether tparm() supports varargs or fixed-parameter list.
 */
#undef NCURSES_TPARM_VARARGS
#define NCURSES_TPARM_VARARGS 1

/*
 * Control type used for tparm's arguments.  While X/Open equates long and
 * char* values, this is not always workable for 64-bit platforms.
 */
#undef NCURSES_TPARM_ARG
#define NCURSES_TPARM_ARG intptr_t

/*
 * Control whether ncurses uses wcwidth() for checking width of line-drawing
 * characters.
 */
#undef NCURSES_WCWIDTH_GRAPHICS
#define NCURSES_WCWIDTH_GRAPHICS 1

/*
 * NCURSES_CH_T is used in building the library, but not used otherwise in
 * this header file, since that would make the normal/wide-character versions
 * of the header incompatible.
 */
#undef	NCURSES_CH_T
#define NCURSES_CH_T cchar_t

#if 1 && defined(_LP64)
typedef unsigned chtype;
typedef unsigned mmask_t;
#else
typedef uint32_t chtype;
typedef uint32_t mmask_t;
#endif

/*
 * We need FILE, etc.  Include this before checking any feature symbols.
 */
#include <stdio.h>

/*
 * With XPG4, you must define _XOPEN_SOURCE_EXTENDED, it is redundant (or
 * conflicting) when _XOPEN_SOURCE is 500 or greater.  If NCURSES_WIDECHAR is
 * not already defined, e.g., if the platform relies upon nonstandard feature
 * test macros, define it at this point if the standard feature test macros
 * indicate that it should be defined.
 */
#ifndef NCURSES_WIDECHAR
#if defined(_XOPEN_SOURCE_EXTENDED) || (defined(_XOPEN_SOURCE) && (_XOPEN_SOURCE - 0 >= 500))
#define NCURSES_WIDECHAR 1
#else
#define NCURSES_WIDECHAR 0
#endif
#endif /* NCURSES_WIDECHAR */

#include <stdarg.h>	/* we need va_list */
#if NCURSES_WIDECHAR
#include <stddef.h>	/* we want wchar_t */
#endif

/* X/Open and SVr4 specify that curses implements 'bool'.  However, C++ may also
 * implement it.  If so, we must use the C++ compiler's type to avoid conflict
 * with other interfaces.
 *
 * A further complication is that <stdbool.h> may declare 'bool' to be a
 * different type, such as an enum which is not necessarily compatible with
 * C++.  If we have <stdbool.h>, make 'bool' a macro, so users may #undef it.
 * Otherwise, let it remain a typedef to avoid conflicts with other #define's.
 * In either case, make a typedef for NCURSES_BOOL which can be used if needed
 * from either C or C++.
 */

#undef TRUE
#define TRUE    1

#undef FALSE
#define FALSE   0

typedef unsigned char NCURSES_BOOL;

#if defined(__cplusplus)	/* __cplusplus, etc. */

/* use the C++ compiler's bool type */
#define NCURSES_BOOL bool

#else			/* c89, c99, etc. */

#if NCURSES_ENABLE_STDBOOL_H
#include <stdbool.h>
/* use whatever the C compiler decides bool really is */
#define NCURSES_BOOL bool
#else
/* there is no predefined bool - use our own */
#undef bool
#define bool NCURSES_BOOL
#endif

#endif /* !__cplusplus, etc. */

#ifdef __cplusplus
extern "C" {
#define NCURSES_CAST(type,value) static_cast<type>(value)
#else
#define NCURSES_CAST(type,value) (type)(value)
#endif

#define NCURSES_OK_ADDR(p) (0 != NCURSES_CAST(const void *, (p)))

/*
 * X/Open attributes.  In the ncurses implementation, they are identical to the
 * A_ attributes.
 */
#define WA_ATTRIBUTES	A_ATTRIBUTES
#define WA_NORMAL	A_NORMAL
#define WA_STANDOUT	A_STANDOUT
#define WA_UNDERLINE	A_UNDERLINE
#define WA_REVERSE	A_REVERSE
#define WA_BLINK	A_BLINK
#define WA_DIM		A_DIM
#define WA_BOLD		A_BOLD
#define WA_ALTCHARSET	A_ALTCHARSET
#define WA_INVIS	A_INVIS
#define WA_PROTECT	A_PROTECT
#define WA_HORIZONTAL	A_HORIZONTAL
#define WA_LEFT		A_LEFT
#define WA_LOW		A_LOW
#define WA_RIGHT	A_RIGHT
#define WA_TOP		A_TOP
#define WA_VERTICAL	A_VERTICAL

#if 1
#define WA_ITALIC	A_ITALIC	/* ncurses extension */
#endif

/* colors */
#define COLOR_BLACK	0
#define COLOR_RED	1
#define COLOR_GREEN	2
#define COLOR_YELLOW	3
#define COLOR_BLUE	4
#define COLOR_MAGENTA	5
#define COLOR_CYAN	6
#define COLOR_WHITE	7

/* line graphics */

#if 0 || NCURSES_REENTRANT
NCURSES_WRAPPED_VAR(chtype*, acs_map);
#define acs_map NCURSES_PUBLIC_VAR(acs_map())
#else
extern NCURSES_EXPORT_VAR(chtype) acs_map[];
#endif

#define NCURSES_ACS(c)	(acs_map[NCURSES_CAST(unsigned char,(c))])

/* VT100 symbols begin here */
#define ACS_ULCORNER	NCURSES_ACS('l') /* upper left corner */
#define ACS_LLCORNER	NCURSES_ACS('m') /* lower left corner */
#define ACS_URCORNER	NCURSES_ACS('k') /* upper right corner */
#define ACS_LRCORNER	NCURSES_ACS('j') /* lower right corner */
#define ACS_LTEE	NCURSES_ACS('t') /* tee pointing right */
#define ACS_RTEE	NCURSES_ACS('u') /* tee pointing left */
#define ACS_BTEE	NCURSES_ACS('v') /* tee pointing up */
#define ACS_TTEE	NCURSES_ACS('w') /* tee pointing down */
#define ACS_HLINE	NCURSES_ACS('q') /* horizontal line */
#define ACS_VLINE	NCURSES_ACS('x') /* vertical line */
#define ACS_PLUS	NCURSES_ACS('n') /* large plus or crossover */
#define ACS_S1		NCURSES_ACS('o') /* scan line 1 */
#define ACS_S9		NCURSES_ACS('s') /* scan line 9 */
#define ACS_DIAMOND	NCURSES_ACS('`') /* diamond */
#define ACS_CKBOARD	NCURSES_ACS('a') /* checker board (stipple) */
#define ACS_DEGREE	NCURSES_ACS('f') /* degree symbol */
#define ACS_PLMINUS	NCURSES_ACS('g') /* plus/minus */
#define ACS_BULLET	NCURSES_ACS('~') /* bullet */
/* Teletype 5410v1 symbols begin here */
#define ACS_LARROW	NCURSES_ACS(',') /* arrow pointing left */
#define ACS_RARROW	NCURSES_ACS('+') /* arrow pointing right */
#define ACS_DARROW	NCURSES_ACS('.') /* arrow pointing down */
#define ACS_UARROW	NCURSES_ACS('-') /* arrow pointing up */
#define ACS_BOARD	NCURSES_ACS('h') /* board of squares */
#define ACS_LANTERN	NCURSES_ACS('i') /* lantern symbol */
#define ACS_BLOCK	NCURSES_ACS('0') /* solid square block */
/*
 * These aren't documented, but a lot of System Vs have them anyway
 * (you can spot pprryyzz{{||}} in a lot of AT&T terminfo strings).
 * The ACS_names may not match AT&T's, our source didn't know them.
 */
#define ACS_S3		NCURSES_ACS('p') /* scan line 3 */
#define ACS_S7		NCURSES_ACS('r') /* scan line 7 */
#define ACS_LEQUAL	NCURSES_ACS('y') /* less/equal */
#define ACS_GEQUAL	NCURSES_ACS('z') /* greater/equal */
#define ACS_PI		NCURSES_ACS('{') /* Pi */
#define ACS_NEQUAL	NCURSES_ACS('|') /* not equal */
#define ACS_STERLING	NCURSES_ACS('}') /* UK pound sign */

/*
 * Line drawing ACS names are of the form ACS_trbl, where t is the top, r
 * is the right, b is the bottom, and l is the left.  t, r, b, and l might
 * be B (blank), S (single), D (double), or T (thick).  The subset defined
 * here only uses B and S.
 */
#define ACS_BSSB	ACS_ULCORNER
#define ACS_SSBB	ACS_LLCORNER
#define ACS_BBSS	ACS_URCORNER
#define ACS_SBBS	ACS_LRCORNER
#define ACS_SBSS	ACS_RTEE
#define ACS_SSSB	ACS_LTEE
#define ACS_SSBS	ACS_BTEE
#define ACS_BSSS	ACS_TTEE
#define ACS_BSBS	ACS_HLINE
#define ACS_SBSB	ACS_VLINE
#define ACS_SSSS	ACS_PLUS

#undef	ERR
#define ERR     (-1)

#undef	OK
#define OK      (0)

/* values for the _flags member */
#define _SUBWIN         0x01	/* is this a sub-window? */
#define _ENDLINE        0x02	/* is the window flush right? */
#define _FULLWIN        0x04	/* is the window full-screen? */
#define _SCROLLWIN      0x08	/* bottom edge is at screen bottom? */
#define _ISPAD	        0x10	/* is this window a pad? */
#define _HASMOVED       0x20	/* has cursor moved since last refresh? */
#define _WRAPPED        0x40	/* cursor was just wrappped */

/*
 * this value is used in the firstchar and lastchar fields to mark
 * unchanged lines
 */
#define _NOCHANGE       -1

/*
 * this value is used in the oldindex field to mark lines created by insertions
 * and scrolls.
 */
#define _NEWINDEX	-1

#ifdef NCURSES_INTERNALS
#undef SCREEN
#define SCREEN struct screen
SCREEN;
#else
typedef struct screen  SCREEN;
#endif

typedef struct _win_st WINDOW;

typedef	chtype	attr_t;		/* ...must be at least as wide as chtype */

#if NCURSES_WIDECHAR

#if 0
#ifdef mblen			/* libutf8.h defines it w/o undefining first */
#undef mblen
#endif
#include <libutf8.h>
#endif

#if 1
#include <wchar.h>		/* ...to get mbstate_t, etc. */
#endif

#if 0
typedef unsigned short wchar_t1;
#endif

#if 0
typedef unsigned int wint_t1;
#endif

/*
 * cchar_t stores an array of CCHARW_MAX wide characters.  The first is
 * normally a spacing character.  The others are non-spacing.  If those
 * (spacing and nonspacing) do not fill the array, a null L'\0' follows.
 * Otherwise, a null is assumed to follow when extracting via getcchar().
 */
#define CCHARW_MAX	5
typedef struct
{
    attr_t	attr;
    wchar_t	chars[CCHARW_MAX];
#if 1
#undef NCURSES_EXT_COLORS
#define NCURSES_EXT_COLORS 20230826
    int		ext_color;	/* color pair, must be more than 16-bits */
#endif
}
cchar_t;

#endif /* NCURSES_WIDECHAR */

#if !NCURSES_OPAQUE
struct ldat;

struct _win_st
{
	NCURSES_SIZE_T _cury, _curx; /* current cursor position */

	/* window location and size */
	NCURSES_SIZE_T _maxy, _maxx; /* maximums of x and y, NOT window size */
	NCURSES_SIZE_T _begy, _begx; /* screen coords of upper-left-hand corner */

	short   _flags;		/* window state flags */

	/* attribute tracking */
	attr_t  _attrs;		/* current attribute for non-space character */
	chtype  _bkgd;		/* current background char/attribute pair */

	/* option values set by user */
	bool	_notimeout;	/* no time out on function-key entry? */
	bool	_clear;		/* consider all data in the window invalid? */
	bool	_leaveok;	/* OK to not reset cursor on exit? */
	bool	_scroll;	/* OK to scroll this window? */
	bool	_idlok;		/* OK to use insert/delete line? */
	bool	_idcok;		/* OK to use insert/delete char? */
	bool	_immed;		/* window in immed mode? (not yet used) */
	bool	_sync;		/* window in sync mode? */
	bool	_use_keypad;	/* process function keys into KEY_ symbols? */
	int	_delay;		/* 0 = nodelay, <0 = blocking, >0 = delay */

	struct ldat *_line;	/* the actual line data */

	/* global screen state */
	NCURSES_SIZE_T _regtop;	/* top line of scrolling region */
	NCURSES_SIZE_T _regbottom; /* bottom line of scrolling region */

	/* these are used only if this is a sub-window */
	int	_parx;		/* x coordinate of this window in parent */
	int	_pary;		/* y coordinate of this window in parent */
	WINDOW	*_parent;	/* pointer to parent if a sub-window */

	/* these are used only if this is a pad */
	struct pdat
	{
	    NCURSES_SIZE_T _pad_y,      _pad_x;
	    NCURSES_SIZE_T _pad_top,    _pad_left;
	    NCURSES_SIZE_T _pad_bottom, _pad_right;
	} _pad;

	NCURSES_SIZE_T _yoffset; /* real begy is _begy + _yoffset */

#if NCURSES_WIDECHAR
	cchar_t  _bkgrnd;	/* current background char/attribute pair */
#if 1
	int	_color;		/* current color-pair for non-space character */
#endif
#endif
};
#endif /* NCURSES_OPAQUE */

/*
 * GCC (and some other compilers) define '__attribute__'; we're using this
 * macro to alert the compiler to flag inconsistencies in printf/scanf-like
 * function calls.  Just in case '__attribute__' isn't defined, make a dummy.
 * Old versions of G++ do not accept it anyway, at least not consistently with
 * GCC.
 */
#if !(defined(__GNUC__) || defined(__GNUG__) || defined(__attribute__))
#define __attribute__(p) /* nothing */
#endif

/*
 * We cannot define these in ncurses_cfg.h, since they require parameters to be
 * passed (that is non-portable).
 */
#ifndef GCC_PRINTFLIKE
#ifndef printf
#define GCC_PRINTFLIKE(fmt,var) __attribute__((format(printf,fmt,var)))
#else
#define GCC_PRINTFLIKE(fmt,var) /*nothing*/
#endif
#endif

#ifndef GCC_SCANFLIKE
#ifndef scanf
#define GCC_SCANFLIKE(fmt,var)  __attribute__((format(scanf,fmt,var)))
#else
#define GCC_SCANFLIKE(fmt,var)  /*nothing*/
#endif
#endif

#ifndef	GCC_NORETURN
#define	GCC_NORETURN /* nothing */
#endif

#ifndef	GCC_UNUSED
#define	GCC_UNUSED /* nothing */
#endif

#undef  GCC_DEPRECATED
#if (__GNUC__ - 0 > 3 || (__GNUC__ - 0 == 3 && __GNUC_MINOR__ - 0 >= 2)) && !defined(NCURSES_INTERNALS)
#define GCC_DEPRECATED(msg) __attribute__((deprecated))
#else
#define GCC_DEPRECATED(msg) /* nothing */
#endif

/*
 * Curses uses a helper function.  Define our type for this to simplify
 * extending it for the sp-funcs feature.
 */
typedef int (*NCURSES_OUTC)(int);

/*
 * Function prototypes.  This is the complete X/Open Curses list of required
 * functions.  Those marked `generated' will have sources generated from the
 * macro definitions later in this file, in order to satisfy XPG4.2
 * requirements.
 */

extern NCURSES_EXPORT(int) addch (const chtype);			/* generated */
extern NCURSES_EXPORT(int) addchnstr (const chtype *, int);		/* generated */
extern NCURSES_EXPORT(int) addchstr (const chtype *);			/* generated */
extern NCURSES_EXPORT(int) addnstr (const char *, int);			/* generated */
extern NCURSES_EXPORT(int) addstr (const char *);			/* generated */
extern NCURSES_EXPORT(int) attroff (NCURSES_ATTR_T);			/* generated */
extern NCURSES_EXPORT(int) attron (NCURSES_ATTR_T);			/* generated */
extern NCURSES_EXPORT(int) attrset (NCURSES_ATTR_T);			/* generated */
extern NCURSES_EXPORT(int) attr_get (attr_t *, NCURSES_PAIRS_T *, void *);	/* generated */
extern NCURSES_EXPORT(int) attr_off (attr_t, void *);			/* generated */
extern NCURSES_EXPORT(int) attr_on (attr_t, void *);			/* generated */
extern NCURSES_EXPORT(int) attr_set (attr_t, NCURSES_PAIRS_T, void *);		/* generated */
extern NCURSES_EXPORT(int) baudrate (void);				/* implemented */
extern NCURSES_EXPORT(int) beep  (void);				/* implemented */
extern NCURSES_EXPORT(int) bkgd (chtype);				/* generated */
extern NCURSES_EXPORT(void) bkgdset (chtype);				/* generated */
extern NCURSES_EXPORT(int) border (chtype,chtype,chtype,chtype,chtype,chtype,chtype,chtype);	/* generated */
extern NCURSES_EXPORT(int) box (WINDOW *, chtype, chtype);		/* generated */
extern NCURSES_EXPORT(bool) can_change_color (void);			/* implemented */
extern NCURSES_EXPORT(int) cbreak (void);				/* implemented */
extern NCURSES_EXPORT(int) chgat (int, attr_t, NCURSES_PAIRS_T, const void *);	/* generated */
extern NCURSES_EXPORT(int) clear (void);				/* generated */
extern NCURSES_EXPORT(int) clearok (WINDOW *,bool);			/* implemented */
extern NCURSES_EXPORT(int) clrtobot (void);				/* generated */
extern NCURSES_EXPORT(int) clrtoeol (void);				/* generated */
extern NCURSES_EXPORT(int) color_content (NCURSES_COLOR_T,NCURSES_COLOR_T*,NCURSES_COLOR_T*,NCURSES_COLOR_T*);	/* implemented */
extern NCURSES_EXPORT(int) color_set (NCURSES_PAIRS_T,void*);			/* generated */
extern NCURSES_EXPORT(int) COLOR_PAIR (int);				/* generated */
extern NCURSES_EXPORT(int) copywin (const WINDOW*,WINDOW*,int,int,int,int,int,int,int);	/* implemented */
extern NCURSES_EXPORT(int) curs_set (int);				/* implemented */
extern NCURSES_EXPORT(int) def_prog_mode (void);			/* implemented */
extern NCURSES_EXPORT(int) def_shell_mode (void);			/* implemented */
extern NCURSES_EXPORT(int) delay_output (int);				/* implemented */
extern NCURSES_EXPORT(int) delch (void);				/* generated */
extern NCURSES_EXPORT(void) delscreen (SCREEN *);			/* implemented */
extern NCURSES_EXPORT(int) delwin (WINDOW *);				/* implemented */
extern NCURSES_EXPORT(int) deleteln (void);				/* generated */
extern NCURSES_EXPORT(WINDOW *) derwin (WINDOW *,int,int,int,int);	/* implemented */
extern NCURSES_EXPORT(int) doupdate (void);				/* implemented */
extern NCURSES_EXPORT(WINDOW *) dupwin (WINDOW *);			/* implemented */
extern NCURSES_EXPORT(int) echo (void);					/* implemented */
extern NCURSES_EXPORT(int) echochar (const chtype);			/* generated */
extern NCURSES_EXPORT(int) erase (void);				/* generated */
extern NCURSES_EXPORT(int) endwin (void);				/* implemented */
extern NCURSES_EXPORT(char) erasechar (void);				/* implemented */
extern NCURSES_EXPORT(void) filter (void);				/* implemented */
extern NCURSES_EXPORT(int) flash (void);				/* implemented */
extern NCURSES_EXPORT(int) flushinp (void);				/* implemented */
extern NCURSES_EXPORT(chtype) getbkgd (WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) getch (void);				/* generated */
extern NCURSES_EXPORT(int) getnstr (char *, int);			/* generated */
extern NCURSES_EXPORT(int) getstr (char *);				/* generated */
extern NCURSES_EXPORT(WINDOW *) getwin (FILE *);			/* implemented */
extern NCURSES_EXPORT(int) halfdelay (int);				/* implemented */
extern NCURSES_EXPORT(bool) has_colors (void);				/* implemented */
extern NCURSES_EXPORT(bool) has_ic (void);				/* implemented */
extern NCURSES_EXPORT(bool) has_il (void);				/* implemented */
extern NCURSES_EXPORT(int) hline (chtype, int);				/* generated */
extern NCURSES_EXPORT(void) idcok (WINDOW *, bool);			/* implemented */
extern NCURSES_EXPORT(int) idlok (WINDOW *, bool);			/* implemented */
extern NCURSES_EXPORT(void) immedok (WINDOW *, bool);			/* implemented */
extern NCURSES_EXPORT(chtype) inch (void);				/* generated */
extern NCURSES_EXPORT(int) inchnstr (chtype *, int);			/* generated */
extern NCURSES_EXPORT(int) inchstr (chtype *);				/* generated */
extern NCURSES_EXPORT(WINDOW *) initscr (void);				/* implemented */
extern NCURSES_EXPORT(int) init_color (NCURSES_COLOR_T,NCURSES_COLOR_T,NCURSES_COLOR_T,NCURSES_COLOR_T);	/* implemented */
extern NCURSES_EXPORT(int) init_pair (NCURSES_PAIRS_T,NCURSES_COLOR_T,NCURSES_COLOR_T);		/* implemented */
extern NCURSES_EXPORT(int) innstr (char *, int);			/* generated */
extern NCURSES_EXPORT(int) insch (chtype);				/* generated */
extern NCURSES_EXPORT(int) insdelln (int);				/* generated */
extern NCURSES_EXPORT(int) insertln (void);				/* generated */
extern NCURSES_EXPORT(int) insnstr (const char *, int);			/* generated */
extern NCURSES_EXPORT(int) insstr (const char *);			/* generated */
extern NCURSES_EXPORT(int) instr (char *);				/* generated */
extern NCURSES_EXPORT(int) intrflush (WINDOW *,bool);			/* implemented */
extern NCURSES_EXPORT(bool) isendwin (void);				/* implemented */
extern NCURSES_EXPORT(bool) is_linetouched (WINDOW *,int);		/* implemented */
extern NCURSES_EXPORT(bool) is_wintouched (WINDOW *);			/* implemented */
extern NCURSES_EXPORT(NCURSES_CONST char *) keyname (int);		/* implemented */
extern NCURSES_EXPORT(int) keypad (WINDOW *,bool);			/* implemented */
extern NCURSES_EXPORT(char) killchar (void);				/* implemented */
extern NCURSES_EXPORT(int) leaveok (WINDOW *,bool);			/* implemented */
extern NCURSES_EXPORT(char *) longname (void);				/* implemented */
extern NCURSES_EXPORT(int) meta (WINDOW *,bool);			/* implemented */
extern NCURSES_EXPORT(int) move (int, int);				/* generated */
extern NCURSES_EXPORT(int) mvaddch (int, int, const chtype);		/* generated */
extern NCURSES_EXPORT(int) mvaddchnstr (int, int, const chtype *, int);	/* generated */
extern NCURSES_EXPORT(int) mvaddchstr (int, int, const chtype *);	/* generated */
extern NCURSES_EXPORT(int) mvaddnstr (int, int, const char *, int);	/* generated */
extern NCURSES_EXPORT(int) mvaddstr (int, int, const char *);		/* generated */
extern NCURSES_EXPORT(int) mvchgat (int, int, int, attr_t, NCURSES_PAIRS_T, const void *);	/* generated */
extern NCURSES_EXPORT(int) mvcur (int,int,int,int);			/* implemented */
extern NCURSES_EXPORT(int) mvdelch (int, int);				/* generated */
extern NCURSES_EXPORT(int) mvderwin (WINDOW *, int, int);		/* implemented */
extern NCURSES_EXPORT(int) mvgetch (int, int);				/* generated */
extern NCURSES_EXPORT(int) mvgetnstr (int, int, char *, int);		/* generated */
extern NCURSES_EXPORT(int) mvgetstr (int, int, char *);			/* generated */
extern NCURSES_EXPORT(int) mvhline (int, int, chtype, int);		/* generated */
extern NCURSES_EXPORT(chtype) mvinch (int, int);			/* generated */
extern NCURSES_EXPORT(int) mvinchnstr (int, int, chtype *, int);	/* generated */
extern NCURSES_EXPORT(int) mvinchstr (int, int, chtype *);		/* generated */
extern NCURSES_EXPORT(int) mvinnstr (int, int, char *, int);		/* generated */
extern NCURSES_EXPORT(int) mvinsch (int, int, chtype);			/* generated */
extern NCURSES_EXPORT(int) mvinsnstr (int, int, const char *, int);	/* generated */
extern NCURSES_EXPORT(int) mvinsstr (int, int, const char *);		/* generated */
extern NCURSES_EXPORT(int) mvinstr (int, int, char *);			/* generated */
extern NCURSES_EXPORT(int) mvprintw (int,int, const char *,...)		/* implemented */
		GCC_PRINTFLIKE(3,4);
extern NCURSES_EXPORT(int) mvscanw (int,int, const char *,...)		/* implemented */
		GCC_SCANFLIKE(3,4);
extern NCURSES_EXPORT(int) mvvline (int, int, chtype, int);		/* generated */
extern NCURSES_EXPORT(int) mvwaddch (WINDOW *, int, int, const chtype);	/* generated */
extern NCURSES_EXPORT(int) mvwaddchnstr (WINDOW *, int, int, const chtype *, int);/* generated */
extern NCURSES_EXPORT(int) mvwaddchstr (WINDOW *, int, int, const chtype *);	/* generated */
extern NCURSES_EXPORT(int) mvwaddnstr (WINDOW *, int, int, const char *, int);	/* generated */
extern NCURSES_EXPORT(int) mvwaddstr (WINDOW *, int, int, const char *);	/* generated */
extern NCURSES_EXPORT(int) mvwchgat (WINDOW *, int, int, int, attr_t, NCURSES_PAIRS_T, const void *);/* generated */
extern NCURSES_EXPORT(int) mvwdelch (WINDOW *, int, int);		/* generated */
extern NCURSES_EXPORT(int) mvwgetch (WINDOW *, int, int);		/* generated */
extern NCURSES_EXPORT(int) mvwgetnstr (WINDOW *, int, int, char *, int);	/* generated */
extern NCURSES_EXPORT(int) mvwgetstr (WINDOW *, int, int, char *);	/* generated */
extern NCURSES_EXPORT(int) mvwhline (WINDOW *, int, int, chtype, int);	/* generated */
extern NCURSES_EXPORT(int) mvwin (WINDOW *,int,int);			/* implemented */
extern NCURSES_EXPORT(chtype) mvwinch (WINDOW *, int, int);			/* generated */
extern NCURSES_EXPORT(int) mvwinchnstr (WINDOW *, int, int, chtype *, int);	/* generated */
extern NCURSES_EXPORT(int) mvwinchstr (WINDOW *, int, int, chtype *);		/* generated */
extern NCURSES_EXPORT(int) mvwinnstr (WINDOW *, int, int, char *, int);		/* generated */
extern NCURSES_EXPORT(int) mvwinsch (WINDOW *, int, int, chtype);		/* generated */
extern NCURSES_EXPORT(int) mvwinsnstr (WINDOW *, int, int, const char *, int);	/* generated */
extern NCURSES_EXPORT(int) mvwinsstr (WINDOW *, int, int, const char *);	/* generated */
extern NCURSES_EXPORT(int) mvwinstr (WINDOW *, int, int, char *);		/* generated */
extern NCURSES_EXPORT(int) mvwprintw (WINDOW*,int,int, const char *,...)	/* implemented */
		GCC_PRINTFLIKE(4,5);
extern NCURSES_EXPORT(int) mvwscanw (WINDOW *,int,int, const char *,...)	/* implemented */
		GCC_SCANFLIKE(4,5);
extern NCURSES_EXPORT(int) mvwvline (WINDOW *,int, int, chtype, int);	/* generated */
extern NCURSES_EXPORT(int) napms (int);					/* implemented */
extern NCURSES_EXPORT(WINDOW *) newpad (int,int);			/* implemented */
extern NCURSES_EXPORT(SCREEN *) newterm (const char *,FILE *,FILE *);	/* implemented */
extern NCURSES_EXPORT(WINDOW *) newwin (int,int,int,int);		/* implemented */
extern NCURSES_EXPORT(int) nl (void);					/* implemented */
extern NCURSES_EXPORT(int) nocbreak (void);				/* implemented */
extern NCURSES_EXPORT(int) nodelay (WINDOW *,bool);			/* implemented */
extern NCURSES_EXPORT(int) noecho (void);				/* implemented */
extern NCURSES_EXPORT(int) nonl (void);					/* implemented */
extern NCURSES_EXPORT(void) noqiflush (void);				/* implemented */
extern NCURSES_EXPORT(int) noraw (void);				/* implemented */
extern NCURSES_EXPORT(int) notimeout (WINDOW *,bool);			/* implemented */
extern NCURSES_EXPORT(int) overlay (const WINDOW*,WINDOW *);		/* implemented */
extern NCURSES_EXPORT(int) overwrite (const WINDOW*,WINDOW *);		/* implemented */
extern NCURSES_EXPORT(int) pair_content (NCURSES_PAIRS_T,NCURSES_COLOR_T*,NCURSES_COLOR_T*);		/* implemented */
extern NCURSES_EXPORT(int) PAIR_NUMBER (int);				/* generated */
extern NCURSES_EXPORT(int) pechochar (WINDOW *, const chtype);		/* implemented */
extern NCURSES_EXPORT(int) pnoutrefresh (WINDOW*,int,int,int,int,int,int);/* implemented */
extern NCURSES_EXPORT(int) prefresh (WINDOW *,int,int,int,int,int,int);	/* implemented */
extern NCURSES_EXPORT(int) printw (const char *,...)			/* implemented */
		GCC_PRINTFLIKE(1,2);
extern NCURSES_EXPORT(int) putwin (WINDOW *, FILE *);			/* implemented */
extern NCURSES_EXPORT(void) qiflush (void);				/* implemented */
extern NCURSES_EXPORT(int) raw (void);					/* implemented */
extern NCURSES_EXPORT(int) redrawwin (WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) refresh (void);				/* generated */
extern NCURSES_EXPORT(int) resetty (void);				/* implemented */
extern NCURSES_EXPORT(int) reset_prog_mode (void);			/* implemented */
extern NCURSES_EXPORT(int) reset_shell_mode (void);			/* implemented */
extern NCURSES_EXPORT(int) ripoffline (int, int (*)(WINDOW *, int));	/* implemented */
extern NCURSES_EXPORT(int) savetty (void);				/* implemented */
extern NCURSES_EXPORT(int) scanw (const char *,...)			/* implemented */
		GCC_SCANFLIKE(1,2);
extern NCURSES_EXPORT(int) scr_dump (const char *);			/* implemented */
extern NCURSES_EXPORT(int) scr_init (const char *);			/* implemented */
extern NCURSES_EXPORT(int) scrl (int);					/* generated */
extern NCURSES_EXPORT(int) scroll (WINDOW *);				/* generated */
extern NCURSES_EXPORT(int) scrollok (WINDOW *,bool);			/* implemented */
extern NCURSES_EXPORT(int) scr_restore (const char *);			/* implemented */
extern NCURSES_EXPORT(int) scr_set (const char *);			/* implemented */
extern NCURSES_EXPORT(int) setscrreg (int,int);				/* generated */
extern NCURSES_EXPORT(SCREEN *) set_term (SCREEN *);			/* implemented */
extern NCURSES_EXPORT(int) slk_attroff (const chtype);			/* implemented */
extern NCURSES_EXPORT(int) slk_attr_off (const attr_t, void *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) slk_attron (const chtype);			/* implemented */
extern NCURSES_EXPORT(int) slk_attr_on (attr_t,void*);			/* generated:WIDEC */
extern NCURSES_EXPORT(int) slk_attrset (const chtype);			/* implemented */
extern NCURSES_EXPORT(attr_t) slk_attr (void);				/* implemented */
extern NCURSES_EXPORT(int) slk_attr_set (const attr_t,NCURSES_PAIRS_T,void*);	/* implemented */
extern NCURSES_EXPORT(int) slk_clear (void);				/* implemented */
extern NCURSES_EXPORT(int) slk_color (NCURSES_PAIRS_T);				/* implemented */
extern NCURSES_EXPORT(int) slk_init (int);				/* implemented */
extern NCURSES_EXPORT(char *) slk_label (int);				/* implemented */
extern NCURSES_EXPORT(int) slk_noutrefresh (void);			/* implemented */
extern NCURSES_EXPORT(int) slk_refresh (void);				/* implemented */
extern NCURSES_EXPORT(int) slk_restore (void);				/* implemented */
extern NCURSES_EXPORT(int) slk_set (int,const char *,int);		/* implemented */
extern NCURSES_EXPORT(int) slk_touch (void);				/* implemented */
extern NCURSES_EXPORT(int) standout (void);				/* generated */
extern NCURSES_EXPORT(int) standend (void);				/* generated */
extern NCURSES_EXPORT(int) start_color (void);				/* implemented */
extern NCURSES_EXPORT(WINDOW *) subpad (WINDOW *, int, int, int, int);	/* implemented */
extern NCURSES_EXPORT(WINDOW *) subwin (WINDOW *, int, int, int, int);	/* implemented */
extern NCURSES_EXPORT(int) syncok (WINDOW *, bool);			/* implemented */
extern NCURSES_EXPORT(chtype) termattrs (void);				/* implemented */
extern NCURSES_EXPORT(char *) termname (void);				/* implemented */
extern NCURSES_EXPORT(void) timeout (int);				/* generated */
extern NCURSES_EXPORT(int) touchline (WINDOW *, int, int);		/* generated */
extern NCURSES_EXPORT(int) touchwin (WINDOW *);				/* generated */
extern NCURSES_EXPORT(int) typeahead (int);				/* implemented */
extern NCURSES_EXPORT(int) ungetch (int);				/* implemented */
extern NCURSES_EXPORT(int) untouchwin (WINDOW *);			/* generated */
extern NCURSES_EXPORT(void) use_env (bool);				/* implemented */
extern NCURSES_EXPORT(void) use_tioctl (bool);				/* implemented */
extern NCURSES_EXPORT(int) vidattr (chtype);				/* implemented */
extern NCURSES_EXPORT(int) vidputs (chtype, NCURSES_OUTC);		/* implemented */
extern NCURSES_EXPORT(int) vline (chtype, int);				/* generated */
extern NCURSES_EXPORT(int) vwprintw (WINDOW *, const char *, va_list) GCC_DEPRECATED(use vw_printw)	/* implemented */
		GCC_PRINTFLIKE(2,0);
extern NCURSES_EXPORT(int) vw_printw (WINDOW *, const char *, va_list)	/* implemented */
		GCC_PRINTFLIKE(2,0);	
extern NCURSES_EXPORT(int) vwscanw (WINDOW *, const char *, va_list) GCC_DEPRECATED(use vw_scanw)	/* implemented */
		GCC_SCANFLIKE(2,0);
extern NCURSES_EXPORT(int) vw_scanw (WINDOW *, const char *, va_list)	/* implemented */
		GCC_SCANFLIKE(2,0);
extern NCURSES_EXPORT(int) waddch (WINDOW *, const chtype);		/* implemented */
extern NCURSES_EXPORT(int) waddchnstr (WINDOW *,const chtype *,int);	/* implemented */
extern NCURSES_EXPORT(int) waddchstr (WINDOW *,const chtype *);		/* generated */
extern NCURSES_EXPORT(int) waddnstr (WINDOW *,const char *,int);	/* implemented */
extern NCURSES_EXPORT(int) waddstr (WINDOW *,const char *);		/* generated */
extern NCURSES_EXPORT(int) wattron (WINDOW *, int);			/* generated */
extern NCURSES_EXPORT(int) wattroff (WINDOW *, int);			/* generated */
extern NCURSES_EXPORT(int) wattrset (WINDOW *, int);			/* generated */
extern NCURSES_EXPORT(int) wattr_get (WINDOW *, attr_t *, NCURSES_PAIRS_T *, void *);	/* generated */
extern NCURSES_EXPORT(int) wattr_on (WINDOW *, attr_t, void *);		/* implemented */
extern NCURSES_EXPORT(int) wattr_off (WINDOW *, attr_t, void *);	/* implemented */
extern NCURSES_EXPORT(int) wattr_set (WINDOW *, attr_t, NCURSES_PAIRS_T, void *);	/* generated */
extern NCURSES_EXPORT(int) wbkgd (WINDOW *, chtype);			/* implemented */
extern NCURSES_EXPORT(void) wbkgdset (WINDOW *,chtype);			/* implemented */
extern NCURSES_EXPORT(int) wborder (WINDOW *,chtype,chtype,chtype,chtype,chtype,chtype,chtype,chtype);	/* implemented */
extern NCURSES_EXPORT(int) wchgat (WINDOW *, int, attr_t, NCURSES_PAIRS_T, const void *);/* implemented */
extern NCURSES_EXPORT(int) wclear (WINDOW *);				/* implemented */
extern NCURSES_EXPORT(int) wclrtobot (WINDOW *);			/* implemented */
extern NCURSES_EXPORT(int) wclrtoeol (WINDOW *);			/* implemented */
extern NCURSES_EXPORT(int) wcolor_set (WINDOW*,NCURSES_PAIRS_T,void*);		/* implemented */
extern NCURSES_EXPORT(void) wcursyncup (WINDOW *);			/* implemented */
extern NCURSES_EXPORT(int) wdelch (WINDOW *);				/* implemented */
extern NCURSES_EXPORT(int) wdeleteln (WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) wechochar (WINDOW *, const chtype);		/* implemented */
extern NCURSES_EXPORT(int) werase (WINDOW *);				/* implemented */
extern NCURSES_EXPORT(int) wgetch (WINDOW *);				/* implemented */
extern NCURSES_EXPORT(int) wgetnstr (WINDOW *,char *,int);		/* implemented */
extern NCURSES_EXPORT(int) wgetstr (WINDOW *, char *);			/* generated */
extern NCURSES_EXPORT(int) whline (WINDOW *, chtype, int);		/* implemented */
extern NCURSES_EXPORT(chtype) winch (WINDOW *);				/* implemented */
extern NCURSES_EXPORT(int) winchnstr (WINDOW *, chtype *, int);		/* implemented */
extern NCURSES_EXPORT(int) winchstr (WINDOW *, chtype *);		/* generated */
extern NCURSES_EXPORT(int) winnstr (WINDOW *, char *, int);		/* implemented */
extern NCURSES_EXPORT(int) winsch (WINDOW *, chtype);			/* implemented */
extern NCURSES_EXPORT(int) winsdelln (WINDOW *,int);			/* implemented */
extern NCURSES_EXPORT(int) winsertln (WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) winsnstr (WINDOW *, const char *,int);	/* implemented */
extern NCURSES_EXPORT(int) winsstr (WINDOW *, const char *);		/* generated */
extern NCURSES_EXPORT(int) winstr (WINDOW *, char *);			/* generated */
extern NCURSES_EXPORT(int) wmove (WINDOW *,int,int);			/* implemented */
extern NCURSES_EXPORT(int) wnoutrefresh (WINDOW *);			/* implemented */
extern NCURSES_EXPORT(int) wprintw (WINDOW *, const char *,...)		/* implemented */
		GCC_PRINTFLIKE(2,3);
extern NCURSES_EXPORT(int) wredrawln (WINDOW *,int,int);		/* implemented */
extern NCURSES_EXPORT(int) wrefresh (WINDOW *);				/* implemented */
extern NCURSES_EXPORT(int) wscanw (WINDOW *, const char *,...)		/* implemented */
		GCC_SCANFLIKE(2,3);
extern NCURSES_EXPORT(int) wscrl (WINDOW *,int);			/* implemented */
extern NCURSES_EXPORT(int) wsetscrreg (WINDOW *,int,int);		/* implemented */
extern NCURSES_EXPORT(int) wstandout (WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) wstandend (WINDOW *);			/* generated */
extern NCURSES_EXPORT(void) wsyncdown (WINDOW *);			/* implemented */
extern NCURSES_EXPORT(void) wsyncup (WINDOW *);				/* implemented */
extern NCURSES_EXPORT(void) wtimeout (WINDOW *,int);			/* implemented */
extern NCURSES_EXPORT(int) wtouchln (WINDOW *,int,int,int);		/* implemented */
extern NCURSES_EXPORT(int) wvline (WINDOW *,chtype,int);		/* implemented */

/*
 * These are also declared in <term.h>:
 */
extern NCURSES_EXPORT(int) tigetflag (const char *);			/* implemented */
extern NCURSES_EXPORT(int) tigetnum (const char *);			/* implemented */
extern NCURSES_EXPORT(char *) tigetstr (const char *);			/* implemented */
extern NCURSES_EXPORT(int) putp (const char *);				/* implemented */

#if NCURSES_TPARM_VARARGS
extern NCURSES_EXPORT(char *) tparm (const char *, ...);		/* special */
#else
extern NCURSES_EXPORT(char *) tparm (const char *, NCURSES_TPARM_ARG,NCURSES_TPARM_ARG,NCURSES_TPARM_ARG,NCURSES_TPARM_ARG,NCURSES_TPARM_ARG,NCURSES_TPARM_ARG,NCURSES_TPARM_ARG,NCURSES_TPARM_ARG,NCURSES_TPARM_ARG);	/* special */
#endif

extern NCURSES_EXPORT(char *) tiparm (const char *, ...);		/* special */
extern NCURSES_EXPORT(char *) tiparm_s (int, int, const char *, ...);	/* special */
extern NCURSES_EXPORT(int) tiscan_s (int *, int *, const char *);	/* special */

/*
 * These functions are not in X/Open, but we use them in macro definitions:
 */
extern NCURSES_EXPORT(int) getattrs (const WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) getcurx (const WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) getcury (const WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) getbegx (const WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) getbegy (const WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) getmaxx (const WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) getmaxy (const WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) getparx (const WINDOW *);			/* generated */
extern NCURSES_EXPORT(int) getpary (const WINDOW *);			/* generated */

/*
 * vid_attr() was implemented originally based on a draft of X/Open curses.
 */
#if !NCURSES_WIDECHAR
#define vid_attr(a,pair,opts) vidattr(a)
#endif

/*
 * These functions are extensions - not in X/Open Curses.
 */
#if 1
#undef  NCURSES_EXT_FUNCS
#define NCURSES_EXT_FUNCS 20230826
typedef int (*NCURSES_WINDOW_CB)(WINDOW *, void *);
typedef int (*NCURSES_SCREEN_CB)(SCREEN *, void *);
extern NCURSES_EXPORT(int) alloc_pair (int, int);
extern NCURSES_EXPORT(int) assume_default_colors (int, int);
extern NCURSES_EXPORT(const char *) curses_version (void);
extern NCURSES_EXPORT(int) define_key (const char *, int);
extern NCURSES_EXPORT(int) extended_color_content(int, int *, int *, int *);
extern NCURSES_EXPORT(int) extended_pair_content(int, int *, int *);
extern NCURSES_EXPORT(int) extended_slk_color(int);
extern NCURSES_EXPORT(int) find_pair (int, int);
extern NCURSES_EXPORT(int) free_pair (int);
extern NCURSES_EXPORT(int) get_escdelay (void);
extern NCURSES_EXPORT(int) init_extended_color(int, int, int, int);
extern NCURSES_EXPORT(int) init_extended_pair(int, int, int);
extern NCURSES_EXPORT(int) is_cbreak(void);
extern NCURSES_EXPORT(int) is_echo(void);
extern NCURSES_EXPORT(int) is_nl(void);
extern NCURSES_EXPORT(int) is_raw(void);
extern NCURSES_EXPORT(bool) is_term_resized (int, int);
extern NCURSES_EXPORT(int) key_defined (const char *);
extern NCURSES_EXPORT(char *) keybound (int, int);
extern NCURSES_EXPORT(int) keyok (int, bool);
extern NCURSES_EXPORT(void) nofilter(void);
extern NCURSES_EXPORT(void) reset_color_pairs (void);
extern NCURSES_EXPORT(int) resize_term (int, int);
extern NCURSES_EXPORT(int) resizeterm (int, int);
extern NCURSES_EXPORT(int) set_escdelay (int);
extern NCURSES_EXPORT(int) set_tabsize (int);
extern NCURSES_EXPORT(int) use_default_colors (void);
extern NCURSES_EXPORT(int) use_legacy_coding (int);
extern NCURSES_EXPORT(int) use_screen (SCREEN *, NCURSES_SCREEN_CB, void *);
extern NCURSES_EXPORT(int) use_window (WINDOW *, NCURSES_WINDOW_CB, void *);
extern NCURSES_EXPORT(int) wresize (WINDOW *, int, int);

#if 1
#undef  NCURSES_XNAMES
#define NCURSES_XNAMES 1
extern NCURSES_EXPORT(int) use_extended_names (bool);
#endif

/*
 * These extensions provide access to information stored in the WINDOW even
 * when NCURSES_OPAQUE is set:
 */
extern NCURSES_EXPORT(WINDOW *) wgetparent (const WINDOW *);	/* generated */
extern NCURSES_EXPORT(bool) is_cleared (const WINDOW *);	/* generated */
extern NCURSES_EXPORT(bool) is_idcok (const WINDOW *);		/* generated */
extern NCURSES_EXPORT(bool) is_idlok (const WINDOW *);		/* generated */
extern NCURSES_EXPORT(bool) is_immedok (const WINDOW *);	/* generated */
extern NCURSES_EXPORT(bool) is_keypad (const WINDOW *);		/* generated */
extern NCURSES_EXPORT(bool) is_leaveok (const WINDOW *);	/* generated */
extern NCURSES_EXPORT(bool) is_nodelay (const WINDOW *);	/* generated */
extern NCURSES_EXPORT(bool) is_notimeout (const WINDOW *);	/* generated */
extern NCURSES_EXPORT(bool) is_pad (const WINDOW *);		/* generated */
extern NCURSES_EXPORT(bool) is_scrollok (const WINDOW *);	/* generated */
extern NCURSES_EXPORT(bool) is_subwin (const WINDOW *);		/* generated */
extern NCURSES_EXPORT(bool) is_syncok (const WINDOW *);		/* generated */
extern NCURSES_EXPORT(int) wgetdelay (const WINDOW *);		/* generated */
extern NCURSES_EXPORT(int) wgetscrreg (const WINDOW *, int *, int *); /* generated */

#else
#define curses_version() NCURSES_VERSION
#endif

/*
 * Extra extension-functions, which pass a SCREEN pointer rather than using
 * a global variable SP.
 */
#if 1
#undef  NCURSES_SP_FUNCS
#define NCURSES_SP_FUNCS 20230826
#define NCURSES_SP_NAME(name) name##_sp

/* Define the sp-funcs helper function */
#define NCURSES_SP_OUTC NCURSES_SP_NAME(NCURSES_OUTC)
typedef int (*NCURSES_SP_OUTC)(SCREEN*, int);

extern NCURSES_EXPORT(SCREEN *) new_prescr (void); /* implemented:SP_FUNC */

extern NCURSES_EXPORT(int) NCURSES_SP_NAME(baudrate) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(beep) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(bool) NCURSES_SP_NAME(can_change_color) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(cbreak) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(curs_set) (SCREEN*, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(color_content) (SCREEN*, NCURSES_PAIRS_T, NCURSES_COLOR_T*, NCURSES_COLOR_T*, NCURSES_COLOR_T*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(def_prog_mode) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(def_shell_mode) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(delay_output) (SCREEN*, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(doupdate) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(echo) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(endwin) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(char) NCURSES_SP_NAME(erasechar) (SCREEN*);/* implemented:SP_FUNC */
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(filter) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(flash) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(flushinp) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(WINDOW *) NCURSES_SP_NAME(getwin) (SCREEN*, FILE *);			/* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(halfdelay) (SCREEN*, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(bool) NCURSES_SP_NAME(has_colors) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(bool) NCURSES_SP_NAME(has_ic) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(bool) NCURSES_SP_NAME(has_il) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(init_color) (SCREEN*, NCURSES_COLOR_T, NCURSES_COLOR_T, NCURSES_COLOR_T, NCURSES_COLOR_T); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(init_pair) (SCREEN*, NCURSES_PAIRS_T, NCURSES_COLOR_T, NCURSES_COLOR_T); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(intrflush) (SCREEN*, WINDOW*, bool);	/* implemented:SP_FUNC */
extern NCURSES_EXPORT(bool) NCURSES_SP_NAME(isendwin) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(NCURSES_CONST char *) NCURSES_SP_NAME(keyname) (SCREEN*, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(char) NCURSES_SP_NAME(killchar) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(char *) NCURSES_SP_NAME(longname) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(mvcur) (SCREEN*, int, int, int, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(napms) (SCREEN*, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(WINDOW *) NCURSES_SP_NAME(newpad) (SCREEN*, int, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(SCREEN *) NCURSES_SP_NAME(newterm) (SCREEN*, const char *, FILE *, FILE *); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(WINDOW *) NCURSES_SP_NAME(newwin) (SCREEN*, int, int, int, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(nl) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(nocbreak) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(noecho) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(nonl) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(noqiflush) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(noraw) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(pair_content) (SCREEN*, NCURSES_PAIRS_T, NCURSES_COLOR_T*, NCURSES_COLOR_T*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(qiflush) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(raw) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(reset_prog_mode) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(reset_shell_mode) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(resetty) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(ripoffline) (SCREEN*, int, int (*)(WINDOW *, int));	/* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(savetty) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(scr_init) (SCREEN*, const char *); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(scr_restore) (SCREEN*, const char *); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(scr_set) (SCREEN*, const char *); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_attroff) (SCREEN*, const chtype); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_attron) (SCREEN*, const chtype); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_attrset) (SCREEN*, const chtype); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(attr_t) NCURSES_SP_NAME(slk_attr) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_attr_set) (SCREEN*, const attr_t, NCURSES_PAIRS_T, void*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_clear) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_color) (SCREEN*, NCURSES_PAIRS_T); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_init) (SCREEN*, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(char *) NCURSES_SP_NAME(slk_label) (SCREEN*, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_noutrefresh) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_refresh) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_restore) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_set) (SCREEN*, int, const char *, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(slk_touch) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(start_color) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(chtype) NCURSES_SP_NAME(termattrs) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(char *) NCURSES_SP_NAME(termname) (SCREEN*); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(typeahead) (SCREEN*, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(ungetch) (SCREEN*, int); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(use_env) (SCREEN*, bool); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(use_tioctl) (SCREEN*, bool); /* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(vidattr) (SCREEN*, chtype);	/* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(vidputs) (SCREEN*, chtype, NCURSES_SP_OUTC); /* implemented:SP_FUNC */
#if 1
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(alloc_pair) (SCREEN*, int, int); /* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(assume_default_colors) (SCREEN*, int, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(define_key) (SCREEN*, const char *, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(extended_color_content) (SCREEN*, int, int *, int *, int *);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(extended_pair_content) (SCREEN*, int, int *, int *);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(extended_slk_color) (SCREEN*, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(find_pair) (SCREEN*, int, int); /* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(free_pair) (SCREEN*, int); /* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(get_escdelay) (SCREEN*);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(init_extended_color) (SCREEN*, int, int, int, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(init_extended_pair) (SCREEN*, int, int, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(is_cbreak) (SCREEN*);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(is_echo) (SCREEN*);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(is_nl) (SCREEN*);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(is_raw) (SCREEN*);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(bool) NCURSES_SP_NAME(is_term_resized) (SCREEN*, int, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(key_defined) (SCREEN*, const char *);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(char *) NCURSES_SP_NAME(keybound) (SCREEN*, int, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(keyok) (SCREEN*, int, bool);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(nofilter) (SCREEN*); /* implemented */	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(reset_color_pairs) (SCREEN*); /* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(resize_term) (SCREEN*, int, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(resizeterm) (SCREEN*, int, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(set_escdelay) (SCREEN*, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(set_tabsize) (SCREEN*, int);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(use_default_colors) (SCREEN*);	/* implemented:EXT_SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(use_legacy_coding) (SCREEN*, int);	/* implemented:EXT_SP_FUNC */
#endif
#else
#undef  NCURSES_SP_FUNCS
#define NCURSES_SP_FUNCS 0
#define NCURSES_SP_NAME(name) name
#define NCURSES_SP_OUTC NCURSES_OUTC
#endif

/* attributes */

#define NCURSES_ATTR_SHIFT       8
#define NCURSES_BITS(mask,shift) (NCURSES_CAST(chtype,(mask)) << ((shift) + NCURSES_ATTR_SHIFT))

#define A_NORMAL	(1U - 1U)
#define A_ATTRIBUTES	NCURSES_BITS(~(1U - 1U),0)
#define A_CHARTEXT	(NCURSES_BITS(1U,0) - 1U)
#define A_COLOR		NCURSES_BITS(((1U) << 8) - 1U,0)
#define A_STANDOUT	NCURSES_BITS(1U,8)
#define A_UNDERLINE	NCURSES_BITS(1U,9)
#define A_REVERSE	NCURSES_BITS(1U,10)
#define A_BLINK		NCURSES_BITS(1U,11)
#define A_DIM		NCURSES_BITS(1U,12)
#define A_BOLD		NCURSES_BITS(1U,13)
#define A_ALTCHARSET	NCURSES_BITS(1U,14)
#define A_INVIS		NCURSES_BITS(1U,15)
#define A_PROTECT	NCURSES_BITS(1U,16)
#define A_HORIZONTAL	NCURSES_BITS(1U,17)
#define A_LEFT		NCURSES_BITS(1U,18)
#define A_LOW		NCURSES_BITS(1U,19)
#define A_RIGHT		NCURSES_BITS(1U,20)
#define A_TOP		NCURSES_BITS(1U,21)
#define A_VERTICAL	NCURSES_BITS(1U,22)

#if 1
#define A_ITALIC	NCURSES_BITS(1U,23)	/* ncurses extension */
#endif

/*
 * Most of the pseudo functions are macros that either provide compatibility
 * with older versions of curses, or provide inline functionality to improve
 * performance.
 */

/*
 * These pseudo functions are always implemented as macros:
 */

#define getyx(win,y,x)		(y = getcury(win), x = getcurx(win))
#define getbegyx(win,y,x)	(y = getbegy(win), x = getbegx(win))
#define getmaxyx(win,y,x)	(y = getmaxy(win), x = getmaxx(win))
#define getparyx(win,y,x)	(y = getpary(win), x = getparx(win))

#define getsyx(y,x) do { if (newscr) { \
			     if (is_leaveok(newscr)) \
				(y) = (x) = -1; \
			     else \
				 getyx(newscr,(y), (x)); \
			} \
		    } while(0)

#define setsyx(y,x) do { if (newscr) { \
			    if ((y) == -1 && (x) == -1) \
				leaveok(newscr, TRUE); \
			    else { \
				leaveok(newscr, FALSE); \
				wmove(newscr, (y), (x)); \
			    } \
			} \
		    } while(0)

#ifndef NCURSES_NOMACROS

/*
 * These miscellaneous pseudo functions are provided for compatibility:
 */

#define wgetstr(w, s)		wgetnstr(w, s, -1)
#define getnstr(s, n)		wgetnstr(stdscr, s, (n))

#define setterm(term)		setupterm(term, 1, (int *)0)

#define fixterm()		reset_prog_mode()
#define resetterm()		reset_shell_mode()
#define saveterm()		def_prog_mode()
#define crmode()		cbreak()
#define nocrmode()		nocbreak()
#define gettmode()

/* It seems older SYSV curses versions define these */
#if !NCURSES_OPAQUE
#define getattrs(win)		NCURSES_CAST(int, NCURSES_OK_ADDR(win) ? (win)->_attrs : A_NORMAL)
#define getcurx(win)		(NCURSES_OK_ADDR(win) ? (win)->_curx : ERR)
#define getcury(win)		(NCURSES_OK_ADDR(win) ? (win)->_cury : ERR)
#define getbegx(win)		(NCURSES_OK_ADDR(win) ? (win)->_begx : ERR)
#define getbegy(win)		(NCURSES_OK_ADDR(win) ? (win)->_begy : ERR)
#define getmaxx(win)		(NCURSES_OK_ADDR(win) ? ((win)->_maxx + 1) : ERR)
#define getmaxy(win)		(NCURSES_OK_ADDR(win) ? ((win)->_maxy + 1) : ERR)
#define getparx(win)		(NCURSES_OK_ADDR(win) ? (win)->_parx : ERR)
#define getpary(win)		(NCURSES_OK_ADDR(win) ? (win)->_pary : ERR)
#endif /* NCURSES_OPAQUE */

#define wstandout(win)		(wattrset(win,A_STANDOUT))
#define wstandend(win)		(wattrset(win,A_NORMAL))

#define wattron(win,at)		wattr_on(win, NCURSES_CAST(attr_t, at), NULL)
#define wattroff(win,at)	wattr_off(win, NCURSES_CAST(attr_t, at), NULL)

#if !NCURSES_OPAQUE
#if NCURSES_WATTR_MACROS
#if NCURSES_WIDECHAR && 1
#define wattrset(win,at) \
	(NCURSES_OK_ADDR(win) \
	  ? ((win)->_color = NCURSES_CAST(int, PAIR_NUMBER(at)), \
	     (win)->_attrs = NCURSES_CAST(attr_t, at), \
	     OK) \
	  : ERR)
#else
#define wattrset(win,at) \
	(NCURSES_OK_ADDR(win) \
	  ? ((win)->_attrs = NCURSES_CAST(attr_t, at), \
	     OK) \
	  : ERR)
#endif
#endif /* NCURSES_WATTR_MACROS */
#endif /* NCURSES_OPAQUE */

#define scroll(win)		wscrl(win,1)

#define touchwin(win)		wtouchln((win), 0, getmaxy(win), 1)
#define touchline(win, s, c)	wtouchln((win), s, c, 1)
#define untouchwin(win)		wtouchln((win), 0, getmaxy(win), 0)

#define box(win, v, h)		wborder(win, v, v, h, h, 0, 0, 0, 0)
#define border(ls, rs, ts, bs, tl, tr, bl, br)	wborder(stdscr, ls, rs, ts, bs, tl, tr, bl, br)
#define hline(ch, n)		whline(stdscr, ch, (n))
#define vline(ch, n)		wvline(stdscr, ch, (n))

#define winstr(w, s)		winnstr(w, s, -1)
#define winchstr(w, s)		winchnstr(w, s, -1)
#define winsstr(w, s)		winsnstr(w, s, -1)

#if !NCURSES_OPAQUE
#define redrawwin(win)		wredrawln(win, 0, (NCURSES_OK_ADDR(win) ? (win)->_maxy+1 : -1))
#endif /* NCURSES_OPAQUE */

#define waddstr(win,str)	waddnstr(win,str,-1)
#define waddchstr(win,str)	waddchnstr(win,str,-1)

/*
 * These apply to the first 256 color pairs.
 */
#define COLOR_PAIR(n)	(NCURSES_BITS((n), 0) & A_COLOR)
#define PAIR_NUMBER(a)	(NCURSES_CAST(int,((NCURSES_CAST(unsigned long,(a)) & A_COLOR) >> NCURSES_ATTR_SHIFT)))

/*
 * pseudo functions for standard screen
 */

#define addch(ch)		waddch(stdscr,(ch))
#define addchnstr(str,n)	waddchnstr(stdscr,(str),(n))
#define addchstr(str)		waddchstr(stdscr,(str))
#define addnstr(str,n)		waddnstr(stdscr,(str),(n))
#define addstr(str)		waddnstr(stdscr,(str),-1)
#define attr_get(ap,cp,o)	wattr_get(stdscr,(ap),(cp),(o))
#define attr_off(a,o)		wattr_off(stdscr,(a),(o))
#define attr_on(a,o)		wattr_on(stdscr,(a),(o))
#define attr_set(a,c,o)		wattr_set(stdscr,(a),(c),(o))
#define attroff(at)		wattroff(stdscr,(at))
#define attron(at)		wattron(stdscr,(at))
#define attrset(at)		wattrset(stdscr,(at))
#define bkgd(ch)		wbkgd(stdscr,(ch))
#define bkgdset(ch)		wbkgdset(stdscr,(ch))
#define chgat(n,a,c,o)		wchgat(stdscr,(n),(a),(c),(o))
#define clear()			wclear(stdscr)
#define clrtobot()		wclrtobot(stdscr)
#define clrtoeol()		wclrtoeol(stdscr)
#define color_set(c,o)		wcolor_set(stdscr,(c),(o))
#define delch()			wdelch(stdscr)
#define deleteln()		winsdelln(stdscr,-1)
#define echochar(c)		wechochar(stdscr,(c))
#define erase()			werase(stdscr)
#define getch()			wgetch(stdscr)
#define getstr(str)		wgetstr(stdscr,(str))
#define inch()			winch(stdscr)
#define inchnstr(s,n)		winchnstr(stdscr,(s),(n))
#define inchstr(s)		winchstr(stdscr,(s))
#define innstr(s,n)		winnstr(stdscr,(s),(n))
#define insch(c)		winsch(stdscr,(c))
#define insdelln(n)		winsdelln(stdscr,(n))
#define insertln()		winsdelln(stdscr,1)
#define insnstr(s,n)		winsnstr(stdscr,(s),(n))
#define insstr(s)		winsstr(stdscr,(s))
#define instr(s)		winstr(stdscr,(s))
#define move(y,x)		wmove(stdscr,(y),(x))
#define refresh()		wrefresh(stdscr)
#define scrl(n)			wscrl(stdscr,(n))
#define setscrreg(t,b)		wsetscrreg(stdscr,(t),(b))
#define standend()		wstandend(stdscr)
#define standout()		wstandout(stdscr)
#define timeout(delay)		wtimeout(stdscr,(delay))
#define wdeleteln(win)		winsdelln(win,-1)
#define winsertln(win)		winsdelln(win,1)

/*
 * mv functions
 */

#define mvwaddch(win,y,x,ch)		(wmove((win),(y),(x)) == ERR ? ERR : waddch((win),(ch)))
#define mvwaddchnstr(win,y,x,str,n)	(wmove((win),(y),(x)) == ERR ? ERR : waddchnstr((win),(str),(n)))
#define mvwaddchstr(win,y,x,str)	(wmove((win),(y),(x)) == ERR ? ERR : waddchnstr((win),(str),-1))
#define mvwaddnstr(win,y,x,str,n)	(wmove((win),(y),(x)) == ERR ? ERR : waddnstr((win),(str),(n)))
#define mvwaddstr(win,y,x,str)		(wmove((win),(y),(x)) == ERR ? ERR : waddnstr((win),(str),-1))
#define mvwchgat(win,y,x,n,a,c,o)	(wmove((win),(y),(x)) == ERR ? ERR : wchgat((win),(n),(a),(c),(o)))
#define mvwdelch(win,y,x)		(wmove((win),(y),(x)) == ERR ? ERR : wdelch(win))
#define mvwgetch(win,y,x)		(wmove((win),(y),(x)) == ERR ? ERR : wgetch(win))
#define mvwgetnstr(win,y,x,str,n)	(wmove((win),(y),(x)) == ERR ? ERR : wgetnstr((win),(str),(n)))
#define mvwgetstr(win,y,x,str)		(wmove((win),(y),(x)) == ERR ? ERR : wgetstr((win),(str)))
#define mvwhline(win,y,x,c,n)		(wmove((win),(y),(x)) == ERR ? ERR : whline((win),(c),(n)))
#define mvwinch(win,y,x)		(wmove((win),(y),(x)) == ERR ? NCURSES_CAST(chtype, ERR) : winch(win))
#define mvwinchnstr(win,y,x,s,n)	(wmove((win),(y),(x)) == ERR ? ERR : winchnstr((win),(s),(n)))
#define mvwinchstr(win,y,x,s)		(wmove((win),(y),(x)) == ERR ? ERR : winchstr((win),(s)))
#define mvwinnstr(win,y,x,s,n)		(wmove((win),(y),(x)) == ERR ? ERR : winnstr((win),(s),(n)))
#define mvwinsch(win,y,x,c)		(wmove((win),(y),(x)) == ERR ? ERR : winsch((win),(c)))
#define mvwinsnstr(win,y,x,s,n)		(wmove((win),(y),(x)) == ERR ? ERR : winsnstr((win),(s),(n)))
#define mvwinsstr(win,y,x,s)		(wmove((win),(y),(x)) == ERR ? ERR : winsstr((win),(s)))
#define mvwinstr(win,y,x,s)		(wmove((win),(y),(x)) == ERR ? ERR : winstr((win),(s)))
#define mvwvline(win,y,x,c,n)		(wmove((win),(y),(x)) == ERR ? ERR : wvline((win),(c),(n)))

#define mvaddch(y,x,ch)			mvwaddch(stdscr,(y),(x),(ch))
#define mvaddchnstr(y,x,str,n)		mvwaddchnstr(stdscr,(y),(x),(str),(n))
#define mvaddchstr(y,x,str)		mvwaddchstr(stdscr,(y),(x),(str))
#define mvaddnstr(y,x,str,n)		mvwaddnstr(stdscr,(y),(x),(str),(n))
#define mvaddstr(y,x,str)		mvwaddstr(stdscr,(y),(x),(str))
#define mvchgat(y,x,n,a,c,o)		mvwchgat(stdscr,(y),(x),(n),(a),(c),(o))
#define mvdelch(y,x)			mvwdelch(stdscr,(y),(x))
#define mvgetch(y,x)			mvwgetch(stdscr,(y),(x))
#define mvgetnstr(y,x,str,n)		mvwgetnstr(stdscr,(y),(x),(str),(n))
#define mvgetstr(y,x,str)		mvwgetstr(stdscr,(y),(x),(str))
#define mvhline(y,x,c,n)		mvwhline(stdscr,(y),(x),(c),(n))
#define mvinch(y,x)			mvwinch(stdscr,(y),(x))
#define mvinchnstr(y,x,s,n)		mvwinchnstr(stdscr,(y),(x),(s),(n))
#define mvinchstr(y,x,s)		mvwinchstr(stdscr,(y),(x),(s))
#define mvinnstr(y,x,s,n)		mvwinnstr(stdscr,(y),(x),(s),(n))
#define mvinsch(y,x,c)			mvwinsch(stdscr,(y),(x),(c))
#define mvinsnstr(y,x,s,n)		mvwinsnstr(stdscr,(y),(x),(s),(n))
#define mvinsstr(y,x,s)			mvwinsstr(stdscr,(y),(x),(s))
#define mvinstr(y,x,s)			mvwinstr(stdscr,(y),(x),(s))
#define mvvline(y,x,c,n)		mvwvline(stdscr,(y),(x),(c),(n))

/*
 * Some wide-character functions can be implemented without the extensions.
 */
#if !NCURSES_OPAQUE
#define getbkgd(win)                    (NCURSES_OK_ADDR(win) ? ((win)->_bkgd) : 0)
#endif /* NCURSES_OPAQUE */

#define slk_attr_off(a,v)		((v) ? ERR : slk_attroff(a))
#define slk_attr_on(a,v)		((v) ? ERR : slk_attron(a))

#if !NCURSES_OPAQUE
#if NCURSES_WATTR_MACROS
#if NCURSES_WIDECHAR && 1
#define wattr_set(win,a,p,opts) \
	(NCURSES_OK_ADDR(win) \
	 ? ((void)((win)->_attrs = ((a) & ~A_COLOR), \
		   (win)->_color = (opts) ? *(int *)(opts) : (p)), \
	    OK) \
	 : ERR)
#define wattr_get(win,a,p,opts) \
	(NCURSES_OK_ADDR(win) \
	 ? ((void)(NCURSES_OK_ADDR(a) \
		   ? (*(a) = (win)->_attrs) \
		   : OK), \
	    (void)(NCURSES_OK_ADDR(p) \
		   ? (*(p) = (NCURSES_PAIRS_T) (win)->_color) \
		   : OK), \
	    (void)(NCURSES_OK_ADDR(opts) \
		   ? (*(int *)(opts) = (win)->_color) \
		   : OK), \
	    OK) \
	 : ERR)
#else /* !(NCURSES_WIDECHAR && NCURSES_EXE_COLORS) */
#define wattr_set(win,a,p,opts) \
	 (NCURSES_OK_ADDR(win) \
	  ? ((void)((win)->_attrs = (((a) & ~A_COLOR) | \
				     (attr_t)COLOR_PAIR(p))), \
	     OK) \
	  : ERR)
#define wattr_get(win,a,p,opts) \
	(NCURSES_OK_ADDR(win) \
	 ? ((void)(NCURSES_OK_ADDR(a) \
		   ? (*(a) = (win)->_attrs) \
		   : OK), \
	    (void)(NCURSES_OK_ADDR(p) \
		   ? (*(p) = (NCURSES_PAIRS_T) PAIR_NUMBER((win)->_attrs)) \
		   : OK), \
	    OK) \
	 : ERR)
#endif /* (NCURSES_WIDECHAR && NCURSES_EXE_COLORS) */
#endif /* NCURSES_WATTR_MACROS */
#endif /* NCURSES_OPAQUE */

/*
 * X/Open curses deprecates SVr4 vwprintw/vwscanw, which are supposed to use
 * varargs.h.  It adds new calls vw_printw/vw_scanw, which are supposed to
 * use POSIX stdarg.h.  The ncurses versions of vwprintw/vwscanw already
 * use stdarg.h, so...
 */
/* define vw_printw		vwprintw */
/* define vw_scanw		vwscanw */

/*
 * Export fallback function for use in C++ binding.
 */
#if !1
#define vsscanf(a,b,c) _nc_vsscanf(a,b,c)
NCURSES_EXPORT(int) vsscanf(const char *, const char *, va_list);
#endif

/*
 * These macros are extensions - not in X/Open Curses.
 */
#if 1
#if !NCURSES_OPAQUE
#define is_cleared(win)		(NCURSES_OK_ADDR(win) ? (win)->_clear : FALSE)
#define is_idcok(win)		(NCURSES_OK_ADDR(win) ? (win)->_idcok : FALSE)
#define is_idlok(win)		(NCURSES_OK_ADDR(win) ? (win)->_idlok : FALSE)
#define is_immedok(win)		(NCURSES_OK_ADDR(win) ? (win)->_immed : FALSE)
#define is_keypad(win)		(NCURSES_OK_ADDR(win) ? (win)->_use_keypad : FALSE)
#define is_leaveok(win)		(NCURSES_OK_ADDR(win) ? (win)->_leaveok : FALSE)
#define is_nodelay(win)		(NCURSES_OK_ADDR(win) ? ((win)->_delay == 0) : FALSE)
#define is_notimeout(win)	(NCURSES_OK_ADDR(win) ? (win)->_notimeout : FALSE)
#define is_pad(win)		(NCURSES_OK_ADDR(win) ? ((win)->_flags & _ISPAD) != 0 : FALSE)
#define is_scrollok(win)	(NCURSES_OK_ADDR(win) ? (win)->_scroll : FALSE)
#define is_subwin(win)		(NCURSES_OK_ADDR(win) ? ((win)->_flags & _SUBWIN) != 0 : FALSE)
#define is_syncok(win)		(NCURSES_OK_ADDR(win) ? (win)->_sync : FALSE)
#define wgetdelay(win)		(NCURSES_OK_ADDR(win) ? (win)->_delay : 0)
#define wgetparent(win)		(NCURSES_OK_ADDR(win) ? (win)->_parent : 0)
#define wgetscrreg(win,t,b)	(NCURSES_OK_ADDR(win) ? (*(t) = (win)->_regtop, *(b) = (win)->_regbottom, OK) : ERR)
#endif
#endif

/*
 * X/Open says this returns a bool; SVr4 also checked for out-of-range line.
 * The macro provides compatibility:
 */
#define is_linetouched(w,l) ((!(w) || ((l) > getmaxy(w)) || ((l) < 0)) ? ERR : (is_linetouched)((w),(l)))

#endif /* NCURSES_NOMACROS */

/*
 * Public variables.
 *
 * Notes:
 *	a. ESCDELAY was an undocumented feature under AIX curses.
 *	   It gives the ESC expire time in milliseconds.
 *	b. ttytype is needed for backward compatibility
 */
#if NCURSES_REENTRANT

NCURSES_WRAPPED_VAR(WINDOW *, curscr);
NCURSES_WRAPPED_VAR(WINDOW *, newscr);
NCURSES_WRAPPED_VAR(WINDOW *, stdscr);
NCURSES_WRAPPED_VAR(char *, ttytype);
NCURSES_WRAPPED_VAR(int, COLORS);
NCURSES_WRAPPED_VAR(int, COLOR_PAIRS);
NCURSES_WRAPPED_VAR(int, COLS);
NCURSES_WRAPPED_VAR(int, ESCDELAY);
NCURSES_WRAPPED_VAR(int, LINES);
NCURSES_WRAPPED_VAR(int, TABSIZE);

#define curscr      NCURSES_PUBLIC_VAR(curscr())
#define newscr      NCURSES_PUBLIC_VAR(newscr())
#define stdscr      NCURSES_PUBLIC_VAR(stdscr())
#define ttytype     NCURSES_PUBLIC_VAR(ttytype())
#define COLORS      NCURSES_PUBLIC_VAR(COLORS())
#define COLOR_PAIRS NCURSES_PUBLIC_VAR(COLOR_PAIRS())
#define COLS        NCURSES_PUBLIC_VAR(COLS())
#define ESCDELAY    NCURSES_PUBLIC_VAR(ESCDELAY())
#define LINES       NCURSES_PUBLIC_VAR(LINES())
#define TABSIZE     NCURSES_PUBLIC_VAR(TABSIZE())

#else

extern NCURSES_EXPORT_VAR(WINDOW *) curscr;
extern NCURSES_EXPORT_VAR(WINDOW *) newscr;
extern NCURSES_EXPORT_VAR(WINDOW *) stdscr;
extern NCURSES_EXPORT_VAR(char) ttytype[];
extern NCURSES_EXPORT_VAR(int) COLORS;
extern NCURSES_EXPORT_VAR(int) COLOR_PAIRS;
extern NCURSES_EXPORT_VAR(int) COLS;
extern NCURSES_EXPORT_VAR(int) ESCDELAY;
extern NCURSES_EXPORT_VAR(int) LINES;
extern NCURSES_EXPORT_VAR(int) TABSIZE;

#endif

/*
 * Pseudo-character tokens outside ASCII range.  The curses wgetch() function
 * will return any given one of these only if the corresponding k- capability
 * is defined in your terminal's terminfo entry.
 *
 * Some keys (KEY_A1, etc) are arranged like this:
 *	a1     up    a3
 *	left   b2    right
 *	c1     down  c3
 *
 * A few key codes do not depend upon the terminfo entry.
 */
#define KEY_CODE_YES	0400		/* A wchar_t contains a key code */
#define KEY_MIN		0401		/* Minimum curses key */
#define KEY_BREAK	0401		/* Break key (unreliable) */
#define KEY_SRESET	0530		/* Soft (partial) reset (unreliable) */
#define KEY_RESET	0531		/* Reset or hard reset (unreliable) */
/*
 * These definitions were generated by ./MKkey_defs.sh ./Caps ./Caps-ncurses
 */
#define KEY_DOWN	0402		/* down-arrow key */
#define KEY_UP		0403		/* up-arrow key */
#define KEY_LEFT	0404		/* left-arrow key */
#define KEY_RIGHT	0405		/* right-arrow key */
#define KEY_HOME	0406		/* home key */
#define KEY_BACKSPACE	0407		/* backspace key */
#define KEY_F0		0410		/* Function keys.  Space for 64 */
#define KEY_F(n)	(KEY_F0+(n))	/* Value of function key n */
#define KEY_DL		0510		/* delete-line key */
#define KEY_IL		0511		/* insert-line key */
#define KEY_DC		0512		/* delete-character key */
#define KEY_IC		0513		/* insert-character key */
#define KEY_EIC		0514		/* sent by rmir or smir in insert mode */
#define KEY_CLEAR	0515		/* clear-screen or erase key */
#define KEY_EOS		0516		/* clear-to-end-of-screen key */
#define KEY_EOL		0517		/* clear-to-end-of-line key */
#define KEY_SF		0520		/* scroll-forward key */
#define KEY_SR		0521		/* scroll-backward key */
#define KEY_NPAGE	0522		/* next-page key */
#define KEY_PPAGE	0523		/* previous-page key */
#define KEY_STAB	0524		/* set-tab key */
#define KEY_CTAB	0525		/* clear-tab key */
#define KEY_CATAB	0526		/* clear-all-tabs key */
#define KEY_ENTER	0527		/* enter/send key */
#define KEY_PRINT	0532		/* print key */
#define KEY_LL		0533		/* lower-left key (home down) */
#define KEY_A1		0534		/* upper left of keypad */
#define KEY_A3		0535		/* upper right of keypad */
#define KEY_B2		0536		/* center of keypad */
#define KEY_C1		0537		/* lower left of keypad */
#define KEY_C3		0540		/* lower right of keypad */
#define KEY_BTAB	0541		/* back-tab key */
#define KEY_BEG		0542		/* begin key */
#define KEY_CANCEL	0543		/* cancel key */
#define KEY_CLOSE	0544		/* close key */
#define KEY_COMMAND	0545		/* command key */
#define KEY_COPY	0546		/* copy key */
#define KEY_CREATE	0547		/* create key */
#define KEY_END		0550		/* end key */
#define KEY_EXIT	0551		/* exit key */
#define KEY_FIND	0552		/* find key */
#define KEY_HELP	0553		/* help key */
#define KEY_MARK	0554		/* mark key */
#define KEY_MESSAGE	0555		/* message key */
#define KEY_MOVE	0556		/* move key */
#define KEY_NEXT	0557		/* next key */
#define KEY_OPEN	0560		/* open key */
#define KEY_OPTIONS	0561		/* options key */
#define KEY_PREVIOUS	0562		/* previous key */
#define KEY_REDO	0563		/* redo key */
#define KEY_REFERENCE	0564		/* reference key */
#define KEY_REFRESH	0565		/* refresh key */
#define KEY_REPLACE	0566		/* replace key */
#define KEY_RESTART	0567		/* restart key */
#define KEY_RESUME	0570		/* resume key */
#define KEY_SAVE	0571		/* save key */
#define KEY_SBEG	0572		/* shifted begin key */
#define KEY_SCANCEL	0573		/* shifted cancel key */
#define KEY_SCOMMAND	0574		/* shifted command key */
#define KEY_SCOPY	0575		/* shifted copy key */
#define KEY_SCREATE	0576		/* shifted create key */
#define KEY_SDC		0577		/* shifted delete-character key */
#define KEY_SDL		0600		/* shifted delete-line key */
#define KEY_SELECT	0601		/* select key */
#define KEY_SEND	0602		/* shifted end key */
#define KEY_SEOL	0603		/* shifted clear-to-end-of-line key */
#define KEY_SEXIT	0604		/* shifted exit key */
#define KEY_SFIND	0605		/* shifted find key */
#define KEY_SHELP	0606		/* shifted help key */
#define KEY_SHOME	0607		/* shifted home key */
#define KEY_SIC		0610		/* shifted insert-character key */
#define KEY_SLEFT	0611		/* shifted left-arrow key */
#define KEY_SMESSAGE	0612		/* shifted message key */
#define KEY_SMOVE	0613		/* shifted move key */
#define KEY_SNEXT	0614		/* shifted next key */
#define KEY_SOPTIONS	0615		/* shifted options key */
#define KEY_SPREVIOUS	0616		/* shifted previous key */
#define KEY_SPRINT	0617		/* shifted print key */
#define KEY_SREDO	0620		/* shifted redo key */
#define KEY_SREPLACE	0621		/* shifted replace key */
#define KEY_SRIGHT	0622		/* shifted right-arrow key */
#define KEY_SRSUME	0623		/* shifted resume key */
#define KEY_SSAVE	0624		/* shifted save key */
#define KEY_SSUSPEND	0625		/* shifted suspend key */
#define KEY_SUNDO	0626		/* shifted undo key */
#define KEY_SUSPEND	0627		/* suspend key */
#define KEY_UNDO	0630		/* undo key */
#define KEY_MOUSE	0631		/* Mouse event has occurred */

#ifdef NCURSES_EXT_FUNCS
#define KEY_RESIZE	0632		/* Terminal resize event */
#endif

#define KEY_MAX		0777		/* Maximum key value is 0632 */
/* $Id: curses.h,v 1.64 2023/10/17 09:52:08 nicm Exp $ */
/*
 * vile:cmode:
 * This file is part of ncurses, designed to be appended after curses.h.in
 * (see that file for the relevant copyright).
 */
#define _XOPEN_CURSES 1

#if NCURSES_WIDECHAR

extern NCURSES_EXPORT_VAR(cchar_t *) _nc_wacs;

#define NCURSES_WACS(c)	(&_nc_wacs[NCURSES_CAST(unsigned char,(c))])

#define WACS_BSSB	NCURSES_WACS('l')
#define WACS_SSBB	NCURSES_WACS('m')
#define WACS_BBSS	NCURSES_WACS('k')
#define WACS_SBBS	NCURSES_WACS('j')
#define WACS_SBSS	NCURSES_WACS('u')
#define WACS_SSSB	NCURSES_WACS('t')
#define WACS_SSBS	NCURSES_WACS('v')
#define WACS_BSSS	NCURSES_WACS('w')
#define WACS_BSBS	NCURSES_WACS('q')
#define WACS_SBSB	NCURSES_WACS('x')
#define WACS_SSSS	NCURSES_WACS('n')

#define WACS_ULCORNER	WACS_BSSB
#define WACS_LLCORNER	WACS_SSBB
#define WACS_URCORNER	WACS_BBSS
#define WACS_LRCORNER	WACS_SBBS
#define WACS_RTEE	WACS_SBSS
#define WACS_LTEE	WACS_SSSB
#define WACS_BTEE	WACS_SSBS
#define WACS_TTEE	WACS_BSSS
#define WACS_HLINE	WACS_BSBS
#define WACS_VLINE	WACS_SBSB
#define WACS_PLUS	WACS_SSSS

#define WACS_S1		NCURSES_WACS('o') /* scan line 1 */
#define WACS_S9 	NCURSES_WACS('s') /* scan line 9 */
#define WACS_DIAMOND	NCURSES_WACS('`') /* diamond */
#define WACS_CKBOARD	NCURSES_WACS('a') /* checker board */
#define WACS_DEGREE	NCURSES_WACS('f') /* degree symbol */
#define WACS_PLMINUS	NCURSES_WACS('g') /* plus/minus */
#define WACS_BULLET	NCURSES_WACS('~') /* bullet */

	/* Teletype 5410v1 symbols */
#define WACS_LARROW	NCURSES_WACS(',') /* arrow left */
#define WACS_RARROW	NCURSES_WACS('+') /* arrow right */
#define WACS_DARROW	NCURSES_WACS('.') /* arrow down */
#define WACS_UARROW	NCURSES_WACS('-') /* arrow up */
#define WACS_BOARD	NCURSES_WACS('h') /* board of squares */
#define WACS_LANTERN	NCURSES_WACS('i') /* lantern symbol */
#define WACS_BLOCK	NCURSES_WACS('0') /* solid square block */

	/* ncurses extensions */
#define WACS_S3		NCURSES_WACS('p') /* scan line 3 */
#define WACS_S7		NCURSES_WACS('r') /* scan line 7 */
#define WACS_LEQUAL	NCURSES_WACS('y') /* less/equal */
#define WACS_GEQUAL	NCURSES_WACS('z') /* greater/equal */
#define WACS_PI		NCURSES_WACS('{') /* Pi */
#define WACS_NEQUAL	NCURSES_WACS('|') /* not equal */
#define WACS_STERLING	NCURSES_WACS('}') /* UK pound sign */

	/* double lines */
#define WACS_BDDB	NCURSES_WACS('C')
#define WACS_DDBB	NCURSES_WACS('D')
#define WACS_BBDD	NCURSES_WACS('B')
#define WACS_DBBD	NCURSES_WACS('A')
#define WACS_DBDD	NCURSES_WACS('G')
#define WACS_DDDB	NCURSES_WACS('F')
#define WACS_DDBD	NCURSES_WACS('H')
#define WACS_BDDD	NCURSES_WACS('I')
#define WACS_BDBD	NCURSES_WACS('R')
#define WACS_DBDB	NCURSES_WACS('Y')
#define WACS_DDDD	NCURSES_WACS('E')

#define WACS_D_ULCORNER	WACS_BDDB
#define WACS_D_LLCORNER	WACS_DDBB
#define WACS_D_URCORNER	WACS_BBDD
#define WACS_D_LRCORNER	WACS_DBBD
#define WACS_D_RTEE	WACS_DBDD
#define WACS_D_LTEE	WACS_DDDB
#define WACS_D_BTEE	WACS_DDBD
#define WACS_D_TTEE	WACS_BDDD
#define WACS_D_HLINE	WACS_BDBD
#define WACS_D_VLINE	WACS_DBDB
#define WACS_D_PLUS	WACS_DDDD

	/* thick lines */
#define WACS_BTTB	NCURSES_WACS('L')
#define WACS_TTBB	NCURSES_WACS('M')
#define WACS_BBTT	NCURSES_WACS('K')
#define WACS_TBBT	NCURSES_WACS('J')
#define WACS_TBTT	NCURSES_WACS('U')
#define WACS_TTTB	NCURSES_WACS('T')
#define WACS_TTBT	NCURSES_WACS('V')
#define WACS_BTTT	NCURSES_WACS('W')
#define WACS_BTBT	NCURSES_WACS('Q')
#define WACS_TBTB	NCURSES_WACS('X')
#define WACS_TTTT	NCURSES_WACS('N')

#define WACS_T_ULCORNER	WACS_BTTB
#define WACS_T_LLCORNER	WACS_TTBB
#define WACS_T_URCORNER	WACS_BBTT
#define WACS_T_LRCORNER	WACS_TBBT
#define WACS_T_RTEE	WACS_TBTT
#define WACS_T_LTEE	WACS_TTTB
#define WACS_T_BTEE	WACS_TTBT
#define WACS_T_TTEE	WACS_BTTT
#define WACS_T_HLINE	WACS_BTBT
#define WACS_T_VLINE	WACS_TBTB
#define WACS_T_PLUS	WACS_TTTT

/*
 * Function prototypes for wide-character operations.
 *
 * "generated" comments should include ":WIDEC" to make the corresponding
 * functions ifdef'd in lib_gen.c
 *
 * "implemented" comments do not need this marker.
 */

extern NCURSES_EXPORT(int) add_wch (const cchar_t *);			/* generated:WIDEC */
extern NCURSES_EXPORT(int) add_wchnstr (const cchar_t *, int);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) add_wchstr (const cchar_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) addnwstr (const wchar_t *, int);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) addwstr (const wchar_t *);			/* generated:WIDEC */
extern NCURSES_EXPORT(int) bkgrnd (const cchar_t *);			/* generated:WIDEC */
extern NCURSES_EXPORT(void) bkgrndset (const cchar_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) border_set (const cchar_t*,const cchar_t*,const cchar_t*,const cchar_t*,const cchar_t*,const cchar_t*,const cchar_t*,const cchar_t*); /* generated:WIDEC */
extern NCURSES_EXPORT(int) box_set (WINDOW *, const cchar_t *, const cchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) echo_wchar (const cchar_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) erasewchar (wchar_t*);			/* implemented */
extern NCURSES_EXPORT(int) get_wch (wint_t *);				/* generated:WIDEC */
extern NCURSES_EXPORT(int) get_wstr (wint_t *);				/* generated:WIDEC */
extern NCURSES_EXPORT(int) getbkgrnd (cchar_t *);			/* generated:WIDEC */
extern NCURSES_EXPORT(int) getcchar (const cchar_t *, wchar_t*, attr_t*, NCURSES_PAIRS_T*, void*);	/* implemented */
extern NCURSES_EXPORT(int) getn_wstr (wint_t *, int);			/* generated:WIDEC */
extern NCURSES_EXPORT(int) hline_set (const cchar_t *, int);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) in_wch (cchar_t *);				/* generated:WIDEC */
extern NCURSES_EXPORT(int) in_wchnstr (cchar_t *, int);			/* generated:WIDEC */
extern NCURSES_EXPORT(int) in_wchstr (cchar_t *);			/* generated:WIDEC */
extern NCURSES_EXPORT(int) innwstr (wchar_t *, int);			/* generated:WIDEC */
extern NCURSES_EXPORT(int) ins_nwstr (const wchar_t *, int);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) ins_wch (const cchar_t *);			/* generated:WIDEC */
extern NCURSES_EXPORT(int) ins_wstr (const wchar_t *);			/* generated:WIDEC */
extern NCURSES_EXPORT(int) inwstr (wchar_t *);				/* generated:WIDEC */
extern NCURSES_EXPORT(NCURSES_CONST char*) key_name (wchar_t);		/* implemented */
extern NCURSES_EXPORT(int) killwchar (wchar_t *);			/* implemented */
extern NCURSES_EXPORT(int) mvadd_wch (int, int, const cchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvadd_wchnstr (int, int, const cchar_t *, int);/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvadd_wchstr (int, int, const cchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvaddnwstr (int, int, const wchar_t *, int);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvaddwstr (int, int, const wchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvget_wch (int, int, wint_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvget_wstr (int, int, wint_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvgetn_wstr (int, int, wint_t *, int);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvhline_set (int, int, const cchar_t *, int);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvin_wch (int, int, cchar_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvin_wchnstr (int, int, cchar_t *, int);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvin_wchstr (int, int, cchar_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvinnwstr (int, int, wchar_t *, int);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvins_nwstr (int, int, const wchar_t *, int);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvins_wch (int, int, const cchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvins_wstr (int, int, const wchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvinwstr (int, int, wchar_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvvline_set (int, int, const cchar_t *, int);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwadd_wch (WINDOW *, int, int, const cchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwadd_wchnstr (WINDOW *, int, int, const cchar_t *, int); /* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwadd_wchstr (WINDOW *, int, int, const cchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwaddnwstr (WINDOW *, int, int, const wchar_t *, int);/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwaddwstr (WINDOW *, int, int, const wchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwget_wch (WINDOW *, int, int, wint_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwget_wstr (WINDOW *, int, int, wint_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwgetn_wstr (WINDOW *, int, int, wint_t *, int);/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwhline_set (WINDOW *, int, int, const cchar_t *, int);/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwin_wch (WINDOW *, int, int, cchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwin_wchnstr (WINDOW *, int,int, cchar_t *,int);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwin_wchstr (WINDOW *, int, int, cchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwinnwstr (WINDOW *, int, int, wchar_t *, int);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwins_nwstr (WINDOW *, int,int, const wchar_t *,int); /* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwins_wch (WINDOW *, int, int, const cchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwins_wstr (WINDOW *, int, int, const wchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwinwstr (WINDOW *, int, int, wchar_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) mvwvline_set (WINDOW *, int,int, const cchar_t *,int); /* generated:WIDEC */
extern NCURSES_EXPORT(int) pecho_wchar (WINDOW *, const cchar_t *);	/* implemented */
extern NCURSES_EXPORT(int) setcchar (cchar_t *, const wchar_t *, const attr_t, NCURSES_PAIRS_T, const void *);	/* implemented */
extern NCURSES_EXPORT(int) slk_wset (int, const wchar_t *, int);	/* implemented */
extern NCURSES_EXPORT(attr_t) term_attrs (void);			/* implemented */
extern NCURSES_EXPORT(int) unget_wch (const wchar_t);			/* implemented */
extern NCURSES_EXPORT(int) vid_attr (attr_t, NCURSES_PAIRS_T, void *);		/* implemented */
extern NCURSES_EXPORT(int) vid_puts (attr_t, NCURSES_PAIRS_T, void *, NCURSES_OUTC); /* implemented */
extern NCURSES_EXPORT(int) vline_set (const cchar_t *, int);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) wadd_wch (WINDOW *,const cchar_t *);		/* implemented */
extern NCURSES_EXPORT(int) wadd_wchnstr (WINDOW *,const cchar_t *,int);	/* implemented */
extern NCURSES_EXPORT(int) wadd_wchstr (WINDOW *,const cchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) waddnwstr (WINDOW *,const wchar_t *,int);	/* implemented */
extern NCURSES_EXPORT(int) waddwstr (WINDOW *,const wchar_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) wbkgrnd (WINDOW *,const cchar_t *);		/* implemented */
extern NCURSES_EXPORT(void) wbkgrndset (WINDOW *,const cchar_t *);	/* implemented */
extern NCURSES_EXPORT(int) wborder_set (WINDOW *,const cchar_t*,const cchar_t*,const cchar_t*,const cchar_t*,const cchar_t*,const cchar_t*,const cchar_t*,const cchar_t*);	/* implemented */
extern NCURSES_EXPORT(int) wecho_wchar (WINDOW *, const cchar_t *);	/* implemented */
extern NCURSES_EXPORT(int) wget_wch (WINDOW *, wint_t *);		/* implemented */
extern NCURSES_EXPORT(int) wget_wstr (WINDOW *, wint_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) wgetbkgrnd (WINDOW *, cchar_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) wgetn_wstr (WINDOW *, wint_t *, int);	/* implemented */
extern NCURSES_EXPORT(int) whline_set (WINDOW *, const cchar_t *, int);	/* implemented */
extern NCURSES_EXPORT(int) win_wch (WINDOW *, cchar_t *);		/* implemented */
extern NCURSES_EXPORT(int) win_wchnstr (WINDOW *, cchar_t *, int);	/* implemented */
extern NCURSES_EXPORT(int) win_wchstr (WINDOW *, cchar_t *);		/* generated:WIDEC */
extern NCURSES_EXPORT(int) winnwstr (WINDOW *, wchar_t *, int);		/* implemented */
extern NCURSES_EXPORT(int) wins_nwstr (WINDOW *, const wchar_t *, int);	/* implemented */
extern NCURSES_EXPORT(int) wins_wch (WINDOW *, const cchar_t *);	/* implemented */
extern NCURSES_EXPORT(int) wins_wstr (WINDOW *, const wchar_t *);	/* generated:WIDEC */
extern NCURSES_EXPORT(int) winwstr (WINDOW *, wchar_t *);		/* implemented */
extern NCURSES_EXPORT(wchar_t*) wunctrl (cchar_t *);			/* implemented */
extern NCURSES_EXPORT(int) wvline_set (WINDOW *, const cchar_t *, int);	/* implemented */

#if NCURSES_SP_FUNCS
extern NCURSES_EXPORT(attr_t) NCURSES_SP_NAME(term_attrs) (SCREEN*);		/* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(erasewchar) (SCREEN*, wchar_t *);	/* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(killwchar) (SCREEN*, wchar_t *);	/* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(unget_wch) (SCREEN*, const wchar_t);	/* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(vid_attr) (SCREEN*, attr_t, NCURSES_PAIRS_T, void *);	/* implemented:SP_FUNC */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(vid_puts) (SCREEN*, attr_t, NCURSES_PAIRS_T, void *, NCURSES_SP_OUTC);	/* implemented:SP_FUNC */
extern NCURSES_EXPORT(wchar_t*) NCURSES_SP_NAME(wunctrl) (SCREEN*, cchar_t *);	/* implemented:SP_FUNC */
#endif

#ifndef NCURSES_NOMACROS

/*
 * XSI curses macros for XPG4 conformance.
 */
#define add_wch(c)			wadd_wch(stdscr,(c))
#define add_wchnstr(str,n)		wadd_wchnstr(stdscr,(str),(n))
#define add_wchstr(str)			wadd_wchstr(stdscr,(str))
#define addnwstr(wstr,n)		waddnwstr(stdscr,(wstr),(n))
#define addwstr(wstr)			waddwstr(stdscr,(wstr))
#define bkgrnd(c)			wbkgrnd(stdscr,(c))
#define bkgrndset(c)			wbkgrndset(stdscr,(c))
#define border_set(l,r,t,b,tl,tr,bl,br) wborder_set(stdscr,(l),(r),(t),(b),tl,tr,bl,br)
#define box_set(w,v,h)			wborder_set((w),(v),(v),(h),(h),0,0,0,0)
#define echo_wchar(c)			wecho_wchar(stdscr,(c))
#define get_wch(c)			wget_wch(stdscr,(c))
#define get_wstr(t)			wget_wstr(stdscr,(t))
#define getbkgrnd(wch)			wgetbkgrnd(stdscr,(wch))
#define getn_wstr(t,n)			wgetn_wstr(stdscr,(t),(n))
#define hline_set(c,n)			whline_set(stdscr,(c),(n))
#define in_wch(c)			win_wch(stdscr,(c))
#define in_wchnstr(c,n)			win_wchnstr(stdscr,(c),(n))
#define in_wchstr(c)			win_wchstr(stdscr,(c))
#define innwstr(c,n)			winnwstr(stdscr,(c),(n))
#define ins_nwstr(t,n)			wins_nwstr(stdscr,(t),(n))
#define ins_wch(c)			wins_wch(stdscr,(c))
#define ins_wstr(t)			wins_wstr(stdscr,(t))
#define inwstr(c)			winwstr(stdscr,(c))
#define vline_set(c,n)			wvline_set(stdscr,(c),(n))
#define wadd_wchstr(win,str)		wadd_wchnstr((win),(str),-1)
#define waddwstr(win,wstr)		waddnwstr((win),(wstr),-1)
#define wget_wstr(w,t)			wgetn_wstr((w),(t),-1)
#define win_wchstr(w,c)			win_wchnstr((w),(c),-1)
#define wins_wstr(w,t)			wins_nwstr((w),(t),-1)

#if !NCURSES_OPAQUE
#define wgetbkgrnd(win,wch)		(NCURSES_OK_ADDR(wch) ? ((win) ? (*(wch) = (win)->_bkgrnd) : *(wch), OK) : ERR)
#endif

#define mvadd_wch(y,x,c)		mvwadd_wch(stdscr,(y),(x),(c))
#define mvadd_wchnstr(y,x,s,n)		mvwadd_wchnstr(stdscr,(y),(x),(s),(n))
#define mvadd_wchstr(y,x,s)		mvwadd_wchstr(stdscr,(y),(x),(s))
#define mvaddnwstr(y,x,wstr,n)		mvwaddnwstr(stdscr,(y),(x),(wstr),(n))
#define mvaddwstr(y,x,wstr)		mvwaddwstr(stdscr,(y),(x),(wstr))
#define mvget_wch(y,x,c)		mvwget_wch(stdscr,(y),(x),(c))
#define mvget_wstr(y,x,t)		mvwget_wstr(stdscr,(y),(x),(t))
#define mvgetn_wstr(y,x,t,n)		mvwgetn_wstr(stdscr,(y),(x),(t),(n))
#define mvhline_set(y,x,c,n)		mvwhline_set(stdscr,(y),(x),(c),(n))
#define mvin_wch(y,x,c)			mvwin_wch(stdscr,(y),(x),(c))
#define mvin_wchnstr(y,x,c,n)		mvwin_wchnstr(stdscr,(y),(x),(c),(n))
#define mvin_wchstr(y,x,c)		mvwin_wchstr(stdscr,(y),(x),(c))
#define mvinnwstr(y,x,c,n)		mvwinnwstr(stdscr,(y),(x),(c),(n))
#define mvins_nwstr(y,x,t,n)		mvwins_nwstr(stdscr,(y),(x),(t),(n))
#define mvins_wch(y,x,c)		mvwins_wch(stdscr,(y),(x),(c))
#define mvins_wstr(y,x,t)		mvwins_wstr(stdscr,(y),(x),(t))
#define mvinwstr(y,x,c)			mvwinwstr(stdscr,(y),(x),(c))
#define mvvline_set(y,x,c,n)		mvwvline_set(stdscr,(y),(x),(c),(n))

#define mvwadd_wch(win,y,x,c)		(wmove(win,(y),(x)) == ERR ? ERR : wadd_wch((win),(c)))
#define mvwadd_wchnstr(win,y,x,s,n)	(wmove(win,(y),(x)) == ERR ? ERR : wadd_wchnstr((win),(s),(n)))
#define mvwadd_wchstr(win,y,x,s)	(wmove(win,(y),(x)) == ERR ? ERR : wadd_wchstr((win),(s)))
#define mvwaddnwstr(win,y,x,wstr,n)	(wmove(win,(y),(x)) == ERR ? ERR : waddnwstr((win),(wstr),(n)))
#define mvwaddwstr(win,y,x,wstr)	(wmove(win,(y),(x)) == ERR ? ERR : waddwstr((win),(wstr)))
#define mvwget_wch(win,y,x,c)		(wmove(win,(y),(x)) == ERR ? ERR : wget_wch((win),(c)))
#define mvwget_wstr(win,y,x,t)		(wmove(win,(y),(x)) == ERR ? ERR : wget_wstr((win),(t)))
#define mvwgetn_wstr(win,y,x,t,n)	(wmove(win,(y),(x)) == ERR ? ERR : wgetn_wstr((win),(t),(n)))
#define mvwhline_set(win,y,x,c,n)	(wmove(win,(y),(x)) == ERR ? ERR : whline_set((win),(c),(n)))
#define mvwin_wch(win,y,x,c)		(wmove(win,(y),(x)) == ERR ? ERR : win_wch((win),(c)))
#define mvwin_wchnstr(win,y,x,c,n)	(wmove(win,(y),(x)) == ERR ? ERR : win_wchnstr((win),(c),(n)))
#define mvwin_wchstr(win,y,x,c)		(wmove(win,(y),(x)) == ERR ? ERR : win_wchstr((win),(c)))
#define mvwinnwstr(win,y,x,c,n)		(wmove(win,(y),(x)) == ERR ? ERR : winnwstr((win),(c),(n)))
#define mvwins_nwstr(win,y,x,t,n)	(wmove(win,(y),(x)) == ERR ? ERR : wins_nwstr((win),(t),(n)))
#define mvwins_wch(win,y,x,c)		(wmove(win,(y),(x)) == ERR ? ERR : wins_wch((win),(c)))
#define mvwins_wstr(win,y,x,t)		(wmove(win,(y),(x)) == ERR ? ERR : wins_wstr((win),(t)))
#define mvwinwstr(win,y,x,c)		(wmove(win,(y),(x)) == ERR ? ERR : winwstr((win),(c)))
#define mvwvline_set(win,y,x,c,n)	(wmove(win,(y),(x)) == ERR ? ERR : wvline_set((win),(c),(n)))

#endif /* NCURSES_NOMACROS */

#if defined(TRACE) || defined(NCURSES_TEST)
extern NCURSES_EXPORT(const char *) _nc_viswbuf(const wchar_t *);
extern NCURSES_EXPORT(const char *) _nc_viswibuf(const wint_t *);
#endif

#endif /* NCURSES_WIDECHAR */
/* $Id: curses.h,v 1.64 2023/10/17 09:52:08 nicm Exp $ */
/*
 * vile:cmode:
 * This file is part of ncurses, designed to be appended after curses.h.in
 * (see that file for the relevant copyright).
 */

/* mouse interface */

#if NCURSES_MOUSE_VERSION > 1
#define NCURSES_MOUSE_MASK(b,m) ((m) << (((b) - 1) * 5))
#else
#define NCURSES_MOUSE_MASK(b,m) ((m) << (((b) - 1) * 6))
#endif

#define	NCURSES_BUTTON_RELEASED	001UL
#define	NCURSES_BUTTON_PRESSED	002UL
#define	NCURSES_BUTTON_CLICKED	004UL
#define	NCURSES_DOUBLE_CLICKED	010UL
#define	NCURSES_TRIPLE_CLICKED	020UL
#define	NCURSES_RESERVED_EVENT	040UL

/* event masks */
#define	BUTTON1_RELEASED	NCURSES_MOUSE_MASK(1, NCURSES_BUTTON_RELEASED)
#define	BUTTON1_PRESSED		NCURSES_MOUSE_MASK(1, NCURSES_BUTTON_PRESSED)
#define	BUTTON1_CLICKED		NCURSES_MOUSE_MASK(1, NCURSES_BUTTON_CLICKED)
#define	BUTTON1_DOUBLE_CLICKED	NCURSES_MOUSE_MASK(1, NCURSES_DOUBLE_CLICKED)
#define	BUTTON1_TRIPLE_CLICKED	NCURSES_MOUSE_MASK(1, NCURSES_TRIPLE_CLICKED)

#define	BUTTON2_RELEASED	NCURSES_MOUSE_MASK(2, NCURSES_BUTTON_RELEASED)
#define	BUTTON2_PRESSED		NCURSES_MOUSE_MASK(2, NCURSES_BUTTON_PRESSED)
#define	BUTTON2_CLICKED		NCURSES_MOUSE_MASK(2, NCURSES_BUTTON_CLICKED)
#define	BUTTON2_DOUBLE_CLICKED	NCURSES_MOUSE_MASK(2, NCURSES_DOUBLE_CLICKED)
#define	BUTTON2_TRIPLE_CLICKED	NCURSES_MOUSE_MASK(2, NCURSES_TRIPLE_CLICKED)

#define	BUTTON3_RELEASED	NCURSES_MOUSE_MASK(3, NCURSES_BUTTON_RELEASED)
#define	BUTTON3_PRESSED		NCURSES_MOUSE_MASK(3, NCURSES_BUTTON_PRESSED)
#define	BUTTON3_CLICKED		NCURSES_MOUSE_MASK(3, NCURSES_BUTTON_CLICKED)
#define	BUTTON3_DOUBLE_CLICKED	NCURSES_MOUSE_MASK(3, NCURSES_DOUBLE_CLICKED)
#define	BUTTON3_TRIPLE_CLICKED	NCURSES_MOUSE_MASK(3, NCURSES_TRIPLE_CLICKED)

#define	BUTTON4_RELEASED	NCURSES_MOUSE_MASK(4, NCURSES_BUTTON_RELEASED)
#define	BUTTON4_PRESSED		NCURSES_MOUSE_MASK(4, NCURSES_BUTTON_PRESSED)
#define	BUTTON4_CLICKED		NCURSES_MOUSE_MASK(4, NCURSES_BUTTON_CLICKED)
#define	BUTTON4_DOUBLE_CLICKED	NCURSES_MOUSE_MASK(4, NCURSES_DOUBLE_CLICKED)
#define	BUTTON4_TRIPLE_CLICKED	NCURSES_MOUSE_MASK(4, NCURSES_TRIPLE_CLICKED)

/*
 * In 32 bits the version-1 scheme does not provide enough space for a 5th
 * button, unless we choose to change the ABI by omitting the reserved-events.
 */
#if NCURSES_MOUSE_VERSION > 1

#define	BUTTON5_RELEASED	NCURSES_MOUSE_MASK(5, NCURSES_BUTTON_RELEASED)
#define	BUTTON5_PRESSED		NCURSES_MOUSE_MASK(5, NCURSES_BUTTON_PRESSED)
#define	BUTTON5_CLICKED		NCURSES_MOUSE_MASK(5, NCURSES_BUTTON_CLICKED)
#define	BUTTON5_DOUBLE_CLICKED	NCURSES_MOUSE_MASK(5, NCURSES_DOUBLE_CLICKED)
#define	BUTTON5_TRIPLE_CLICKED	NCURSES_MOUSE_MASK(5, NCURSES_TRIPLE_CLICKED)

#define	BUTTON_CTRL		NCURSES_MOUSE_MASK(6, 0001L)
#define	BUTTON_SHIFT		NCURSES_MOUSE_MASK(6, 0002L)
#define	BUTTON_ALT		NCURSES_MOUSE_MASK(6, 0004L)
#define	REPORT_MOUSE_POSITION	NCURSES_MOUSE_MASK(6, 0010L)

#else

#define	BUTTON1_RESERVED_EVENT	NCURSES_MOUSE_MASK(1, NCURSES_RESERVED_EVENT)
#define	BUTTON2_RESERVED_EVENT	NCURSES_MOUSE_MASK(2, NCURSES_RESERVED_EVENT)
#define	BUTTON3_RESERVED_EVENT	NCURSES_MOUSE_MASK(3, NCURSES_RESERVED_EVENT)
#define	BUTTON4_RESERVED_EVENT	NCURSES_MOUSE_MASK(4, NCURSES_RESERVED_EVENT)

#define	BUTTON_CTRL		NCURSES_MOUSE_MASK(5, 0001L)
#define	BUTTON_SHIFT		NCURSES_MOUSE_MASK(5, 0002L)
#define	BUTTON_ALT		NCURSES_MOUSE_MASK(5, 0004L)
#define	REPORT_MOUSE_POSITION	NCURSES_MOUSE_MASK(5, 0010L)

#endif

#define	ALL_MOUSE_EVENTS	(REPORT_MOUSE_POSITION - 1)

/* macros to extract single event-bits from masks */
#define	BUTTON_RELEASE(e, x)		((e) & NCURSES_MOUSE_MASK(x, 001))
#define	BUTTON_PRESS(e, x)		((e) & NCURSES_MOUSE_MASK(x, 002))
#define	BUTTON_CLICK(e, x)		((e) & NCURSES_MOUSE_MASK(x, 004))
#define	BUTTON_DOUBLE_CLICK(e, x)	((e) & NCURSES_MOUSE_MASK(x, 010))
#define	BUTTON_TRIPLE_CLICK(e, x)	((e) & NCURSES_MOUSE_MASK(x, 020))
#define	BUTTON_RESERVED_EVENT(e, x)	((e) & NCURSES_MOUSE_MASK(x, 040))

typedef struct
{
    short id;		/* ID to distinguish multiple devices */
    int x, y, z;	/* event coordinates (character-cell) */
    mmask_t bstate;	/* button state bits */
}
MEVENT;

extern NCURSES_EXPORT(bool)    has_mouse(void);
extern NCURSES_EXPORT(int)     getmouse (MEVENT *);
extern NCURSES_EXPORT(int)     ungetmouse (MEVENT *);
extern NCURSES_EXPORT(mmask_t) mousemask (mmask_t, mmask_t *);
extern NCURSES_EXPORT(bool)    wenclose (const WINDOW *, int, int);
extern NCURSES_EXPORT(int)     mouseinterval (int);
extern NCURSES_EXPORT(bool)    wmouse_trafo (const WINDOW*, int*, int*, bool);
extern NCURSES_EXPORT(bool)    mouse_trafo (int*, int*, bool);              /* generated */

#if NCURSES_SP_FUNCS
extern NCURSES_EXPORT(bool)    NCURSES_SP_NAME(has_mouse) (SCREEN*);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(getmouse) (SCREEN*, MEVENT *);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(ungetmouse) (SCREEN*,MEVENT *);
extern NCURSES_EXPORT(mmask_t) NCURSES_SP_NAME(mousemask) (SCREEN*, mmask_t, mmask_t *);
extern NCURSES_EXPORT(int)     NCURSES_SP_NAME(mouseinterval) (SCREEN*, int);
#endif

#ifndef NCURSES_NOMACROS
#define mouse_trafo(y,x,to_screen) wmouse_trafo(stdscr,y,x,to_screen)
#endif

/* other non-XSI functions */

extern NCURSES_EXPORT(int) mcprint (char *, int);	/* direct data to printer */
extern NCURSES_EXPORT(int) has_key (int);		/* do we have given key? */

#if NCURSES_SP_FUNCS
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(has_key) (SCREEN*, int);    /* do we have given key? */
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(mcprint) (SCREEN*, char *, int);	/* direct data to printer */
#endif

/* Debugging : use with libncurses_g.a */

extern NCURSES_EXPORT(void) _tracef (const char *, ...) GCC_PRINTFLIKE(1,2);
extern NCURSES_EXPORT(char *) _traceattr (attr_t);
extern NCURSES_EXPORT(char *) _traceattr2 (int, chtype);
extern NCURSES_EXPORT(char *) _tracechar (int);
extern NCURSES_EXPORT(char *) _tracechtype (chtype);
extern NCURSES_EXPORT(char *) _tracechtype2 (int, chtype);
#if NCURSES_WIDECHAR
#define _tracech_t		_tracecchar_t
extern NCURSES_EXPORT(char *) _tracecchar_t (const cchar_t *);
#define _tracech_t2		_tracecchar_t2
extern NCURSES_EXPORT(char *) _tracecchar_t2 (int, const cchar_t *);
#else
#define _tracech_t		_tracechtype
#define _tracech_t2		_tracechtype2
#endif
extern NCURSES_EXPORT(void) trace (const unsigned) GCC_DEPRECATED("use curses_trace");
extern NCURSES_EXPORT(unsigned) curses_trace (const unsigned);

/* trace masks */
#define TRACE_DISABLE	0x0000	/* turn off tracing */
#define TRACE_TIMES	0x0001	/* trace user and system times of updates */
#define TRACE_TPUTS	0x0002	/* trace tputs calls */
#define TRACE_UPDATE	0x0004	/* trace update actions, old & new screens */
#define TRACE_MOVE	0x0008	/* trace cursor moves and scrolls */
#define TRACE_CHARPUT	0x0010	/* trace all character outputs */
#define TRACE_ORDINARY	0x001F	/* trace all update actions */
#define TRACE_CALLS	0x0020	/* trace all curses calls */
#define TRACE_VIRTPUT	0x0040	/* trace virtual character puts */
#define TRACE_IEVENT	0x0080	/* trace low-level input processing */
#define TRACE_BITS	0x0100	/* trace state of TTY control bits */
#define TRACE_ICALLS	0x0200	/* trace internal/nested calls */
#define TRACE_CCALLS	0x0400	/* trace per-character calls */
#define TRACE_DATABASE	0x0800	/* trace read/write of terminfo/termcap data */
#define TRACE_ATTRS	0x1000	/* trace attribute updates */

#define TRACE_SHIFT	13	/* number of bits in the trace masks */
#define TRACE_MAXIMUM	((1 << TRACE_SHIFT) - 1) /* maximum trace level */

#if defined(TRACE) || defined(NCURSES_TEST)
extern NCURSES_EXPORT_VAR(int) _nc_optimize_enable;		/* enable optimizations */
extern NCURSES_EXPORT(const char *) _nc_visbuf (const char *);
#define OPTIMIZE_MVCUR		0x01	/* cursor movement optimization */
#define OPTIMIZE_HASHMAP	0x02	/* diff hashing to detect scrolls */
#define OPTIMIZE_SCROLL		0x04	/* scroll optimization */
#define OPTIMIZE_ALL		0xff	/* enable all optimizations (dflt) */
#endif

extern GCC_NORETURN NCURSES_EXPORT(void) exit_curses (int);

#include <unctrl.h>

#ifdef __cplusplus

#ifndef NCURSES_NOMACROS

/* these names conflict with STL */
#undef box
#undef clear
#undef erase
#undef move
#undef refresh

#endif /* NCURSES_NOMACROS */

}
#endif

#endif /* __NCURSES_H */
