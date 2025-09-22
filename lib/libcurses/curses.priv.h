/* $OpenBSD: curses.priv.h,v 1.36 2023/10/17 09:52:08 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2022,2023 Thomas E. Dickey                                *
 * Copyright 1998-2017,2018 Free Software Foundation, Inc.                  *
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
 * $Id: curses.priv.h,v 1.36 2023/10/17 09:52:08 nicm Exp $
 *
 *	curses.priv.h
 *
 *	Header file for curses library objects which are private to
 *	the library.
 *
 */

#ifndef CURSES_PRIV_H
#define CURSES_PRIV_H 1
/* *INDENT-OFF* */

#include <ncurses_dll.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <ncurses_cfg.h>

#if USE_RCS_IDS
#define MODULE_ID(id) static const char Ident[] = id;
#else
#define MODULE_ID(id) /*nothing*/
#endif

#include <stddef.h>		/* for offsetof */
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#if HAVE_LIMITS_H
# include <limits.h>
#elif HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#include <assert.h>
#include <stdio.h>

#include <errno.h>

#if defined __hpux
#  ifndef EILSEQ
#    define EILSEQ 47
#  endif
#endif

#ifndef PATH_MAX
# if defined(_POSIX_PATH_MAX)
#  define PATH_MAX _POSIX_PATH_MAX
# elif defined(MAXPATHLEN)
#  define PATH_MAX MAXPATHLEN
# else
#  define PATH_MAX 255	/* the Posix minimum path-size */
# endif
#endif

#if DECL_ERRNO
extern int errno;
#endif

/* Some Windows related defines */
#undef _NC_WINDOWS
#if (defined(_WIN32) || defined(_WIN64))
#define _NC_WINDOWS
#else
#undef EXP_WIN32_DRIVER
#endif

#undef _NC_MINGW
#if (defined(__MINGW32__) || defined(__MINGW64__))
#define _NC_MINGW
#endif

#undef _NC_MSC
#ifdef _MSC_VER
#define _NC_MSC
#endif

/* Some systems have a broken 'select()', but workable 'poll()'.  Use that */
#if HAVE_WORKING_POLL
#define USE_FUNC_POLL 1
#if HAVE_POLL_H
#include <poll.h>
#else
#include <sys/poll.h>
#endif
#else
#define USE_FUNC_POLL 0
#endif

#if HAVE_INTTYPES_H
# include <inttypes.h>
#else
# if HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#if (defined(__USE_MINGW_ANSI_STDIO) && __USE_MINGW_ANSI_STDIO != 0) && (defined(__GNUC__) && (__GNUC__ < 12))
# undef PRIxPTR		/* gcc bug fixed in 12.x */
# define PRIxPTR	"lX"
# define CASTxPTR(n)    (unsigned long)(intptr_t)(void*)(n)
#else
# define CASTxPTR(n)    (intptr_t)(void*)(n)
#endif

#ifndef PRIxPTR
# define PRIxPTR	"lx"
# define CASTxPTR(n)    (long)(void*)(n)
#endif

/* include signal.h before curses.h to work-around defect in glibc 2.1.3 */
#include <signal.h>

/* Alessandro Rubini's GPM (general-purpose mouse) */
#if HAVE_LIBGPM && HAVE_GPM_H
#define USE_GPM_SUPPORT 1
#else
#define USE_GPM_SUPPORT 0
#endif

/* QNX mouse support */
#if defined(__QNX__) && !defined(__QNXNTO__)
#define USE_QNX_MOUSE 1
#else
#define USE_QNX_MOUSE 0
#endif

/* EMX mouse support */
#ifdef __EMX__
#define USE_EMX_MOUSE 1
#else
#define USE_EMX_MOUSE 0
#endif

/* kLIBC keyboard/mouse support */
#if defined(__OS2__) && defined(__KLIBC__)
#define USE_KLIBC_KBD   1
#define USE_KLIBC_MOUSE 1
#else
#define USE_KLIBC_KBD   0
#define USE_KLIBC_MOUSE 0
#endif

#define DEFAULT_MAXCLICK 166
#define EV_MAX		8	/* size of mouse circular event queue */

/*
 * If we don't have signals to support it, don't add a sigwinch handler.
 * In any case, resizing is an extended feature.  Use it if we've got it.
 */
#if !NCURSES_EXT_FUNCS
#undef HAVE_SIZECHANGE
#define HAVE_SIZECHANGE 0
#endif

#if HAVE_SIZECHANGE && USE_SIGWINCH && defined(SIGWINCH)
#define USE_SIZECHANGE 1
#else
#define USE_SIZECHANGE 0
#undef USE_SIGWINCH
#define USE_SIGWINCH 0
#endif

/*
 * When building in the MSYS2 environment, the automatic discovery of
 * the path separator in configure doesn't work properly. So, if building
 * for MinGW, we enforce the correct Windows PATH separator
 */
#if defined(_NC_WINDOWS)
#  ifdef NCURSES_PATHSEP
#    undef NCURSES_PATHSEP
#  endif
#  define NCURSES_PATHSEP ';'
#endif

/*
 * When the standard handles have been redirected (such as inside a text editor
 * or the less utility), keystrokes must be read from the console rather than
 * the redirected handle.  The standard output handle suffers from a similar
 * problem.  Both handles are not closed once opened.  The console shall be
 * considered reachable throughout the process.
 */
#if defined(_NC_WINDOWS)
#define GetDirectHandle(fileName, shareMode) \
	CreateFile(TEXT(fileName), \
		   GENERIC_READ | GENERIC_WRITE, \
		   shareMode, \
		   0, \
		   OPEN_EXISTING, \
		   0, \
		   0)
#endif

/*
 * Not all platforms have memmove; some have an equivalent bcopy.  (Some may
 * have neither).
 */
#if USE_OK_BCOPY
#define memmove(d,s,n) bcopy(s,d,n)
#elif USE_MY_MEMMOVE
#define memmove(d,s,n) _nc_memmove(d,s,n)
extern NCURSES_EXPORT(void *) _nc_memmove (void *, const void *, size_t);
#endif

/*
 * If we have va_copy(), use it for assigning va_list's.
 */
#if defined(HAVE___VA_COPY)
#define begin_va_copy(dst,src)	__va_copy(dst, src)
#define end_va_copy(dst)	va_end(dst)
#elif defined(va_copy) || defined(HAVE_VA_COPY)
#define begin_va_copy(dst,src)	va_copy(dst, src)
#define end_va_copy(dst)	va_end(dst)
#else
#define begin_va_copy(dst,src) (dst) = (src)
#define end_va_copy(dst)	/* nothing */
#endif

/*
 * Either/both S_ISxxx and/or S_IFxxx are defined in sys/types.h; some systems
 * lack one or the other.
 */
#ifndef S_ISDIR
#define S_ISDIR(mode) ((mode & S_IFMT) == S_IFDIR)
#endif

#ifndef S_ISREG
#define S_ISREG(mode) ((mode & S_IFMT) == S_IFREG)
#endif

/*
 * POSIX ignores the "b", which c89 specified.  Some very old systems do not
 * accept it.
 */
#if USE_FOPEN_BIN_R
#define BIN_R	"rb"
#define BIN_W	"wb"
#else
#define BIN_R	"r"
#define BIN_W	"w"
#endif

/*
 * Scroll hints are useless when hashmap is used
 */
#if !USE_SCROLL_HINTS
#if !USE_HASHMAP
#define USE_SCROLL_HINTS 1
#else
#define USE_SCROLL_HINTS 0
#endif
#endif

#if USE_SCROLL_HINTS
#define if_USE_SCROLL_HINTS(stmt) stmt
#else
#define if_USE_SCROLL_HINTS(stmt) /*nothing*/
#endif

#include <nc_string.h>

/*
 * Options for terminal drivers, etc...
 */
#ifdef USE_TERM_DRIVER
#define NO_TERMINAL "unknown"
#define USE_SP_RIPOFF     1
#define USE_SP_TERMTYPE   1
#define USE_SP_WINDOWLIST 1
#else
#define NO_TERMINAL 0
#endif

#define VALID_TERM_ENV(term_env, no_terminal) \
	(term_env = (NonEmpty(term_env) \
		     ? term_env \
		     : no_terminal), \
	 NonEmpty(term_env))

/*
 * Note:  ht/cbt expansion flakes out randomly under Linux 1.1.47, but only
 * when we're throwing control codes at the screen at high volume.  To see
 * this, re-enable USE_HARD_TABS and run worm for a while.  Other systems
 * probably don't want to define this either due to uncertainties about tab
 * delays and expansion in raw mode.
 */

#define TRIES struct tries
typedef TRIES {
	TRIES    *child;            /* ptr to child.  NULL if none          */
	TRIES    *sibling;          /* ptr to sibling.  NULL if none        */
	unsigned char    ch;        /* character at this node               */
	unsigned short   value;     /* code of string so far.  0 if none.   */
#undef TRIES
} TRIES;

/*
 * Common/troublesome character definitions
 */
#define StringOf(ch) {ch, 0}

#define L_BRACE '{'
#define R_BRACE '}'
#define S_QUOTE '\''
#define D_QUOTE '"'

#define VT_ACSC "``aaffggiijjkkllmmnnooppqqrrssttuuvvwwxxyyzz{{||}}~~"

/*
 * Structure for palette tables
 */

#define MAXCOLUMNS    135
#define MAXLINES      66
#define FIFO_SIZE     MAXCOLUMNS+2  /* for nocbreak mode input */

#define ACS_LEN       128

#define WINDOWLIST struct _win_list

#if USE_WIDEC_SUPPORT
#define _nc_bkgd    _bkgrnd
#else
#undef _XOPEN_SOURCE_EXTENDED
#undef _XPG5
#define _nc_bkgd    _bkgd
#define wgetbkgrnd(win, wch)	((*wch = win->_bkgd) != 0 ? OK : ERR)
#define wbkgrnd	    wbkgd
#endif

#undef NCURSES_OPAQUE
#define NCURSES_INTERNALS 1
#define NCURSES_OPAQUE 0

#include <curses.h>	/* we'll use -Ipath directive to get the right one! */

#if !(defined(NCURSES_WGETCH_EVENTS) && defined(NEED_KEY_EVENT))
#undef KEY_EVENT		/* reduce compiler-warnings with Visual C++ */
#endif

typedef struct
{
    int red, green, blue;	/* what color_content() returns */
    int r, g, b;		/* params to init_color() */
    int init;			/* true if we called init_color() */
}
color_t;

typedef union {
    struct {
	unsigned char red;
	unsigned char green;
	unsigned char blue;
    } bits;			/* bits per color-value in RGB */
    unsigned value;
} rgb_bits_t;

/*
 * If curses.h did not expose the SCREEN-functions, then we do not need the
 * parameter in the corresponding unextended functions.
 */

#define USE_SP_FUNC_SUPPORT     NCURSES_SP_FUNCS
#define USE_EXT_SP_FUNC_SUPPORT (NCURSES_SP_FUNCS && NCURSES_EXT_FUNCS)

#if NCURSES_SP_FUNCS
#define SP_PARM         sp	/* use parameter */
#define NCURSES_SP_ARG          SP_PARM
#define NCURSES_SP_DCL  SCREEN *NCURSES_SP_ARG
#define NCURSES_SP_DCL0 NCURSES_SP_DCL
#define NCURSES_SP_ARGx         NCURSES_SP_ARG,
#define NCURSES_SP_DCLx SCREEN *NCURSES_SP_ARGx
#else
#define SP_PARM         SP	/* use global variable */
#define NCURSES_SP_ARG
#define NCURSES_SP_DCL
#define NCURSES_SP_DCL0 void
#define NCURSES_SP_ARGx
#define NCURSES_SP_DCLx
#endif

#include <nc_panel.h>

#include <term.priv.h>
#include <nc_termios.h>

#define IsPreScreen(sp)      (((sp) != 0) && sp->_prescreen)
#define HasTerminal(sp)      (((sp) != 0) && (0 != ((sp)->_term)))
#define IsValidScreen(sp)    (HasTerminal(sp) && !IsPreScreen(sp))

#if USE_REENTRANT
#define CurTerm              _nc_prescreen._cur_term
#else
#define CurTerm              cur_term
#endif

#if NCURSES_SP_FUNCS
#define TerminalOf(sp)       ((sp) ? ((sp)->_term ? (sp)->_term : CurTerm) : CurTerm)
#else
#define TerminalOf(sp)       CurTerm
#endif

/*
 * The legacy layout for TERMTYPE uses "short" for all of the numbers.  Moving
 * past that, numeric capabilities can be "int" by using a TERMTYPE2 structure
 * in TERMINAL, and doing most of the internal work using TERMTYPE2.  There are
 * a few places (mostly to expose the legacy layout) where the distinction
 * needs attention.
 */
#if NCURSES_EXT_COLORS && HAVE_INIT_EXTENDED_COLOR
#define NCURSES_EXT_NUMBERS  1
#define NCURSES_INT2         int
#define SIZEOF_INT2          4
#define TerminalType(tp)     (tp)->type2
#else
#define NCURSES_EXT_NUMBERS  0
#define NCURSES_INT2         short
#define SIZEOF_INT2          2
#define TerminalType(tp)     (tp)->type
#endif

#define SIZEOF_SHORT         2

#ifdef CUR
#undef CUR
#define CUR TerminalType(cur_term).
#endif

/*
 * Reduce dependency on cur_term global by using terminfo data from SCREEN's
 * pointer to this data.
 */
#ifdef USE_SP_TERMTYPE
#undef CUR
#endif

#define SP_TERMTYPE TerminalType(TerminalOf(sp)).

#include <term_entry.h>

#include <nc_tparm.h>

/*
 * Simplify ifdef's for the "*_ATTR" macros in case italics are not configured.
 */
#if defined(A_ITALIC) && defined(exit_italics_mode)
#define USE_ITALIC 1
#else
#define USE_ITALIC 0
#undef  A_ITALIC
#define A_ITALIC 0
#endif

/*
 * Use these macros internally, to make tracing less verbose.  But leave the
 * option for compiling the tracing into the library.
 */
#if 1
#define ColorPair(n)		(NCURSES_BITS(n, 0) & A_COLOR)
#define PairNumber(a)		(NCURSES_CAST(int,(((unsigned long)(a) & A_COLOR) >> NCURSES_ATTR_SHIFT)))
#else
#define ColorPair(pair)		COLOR_PAIR(pair)
#define PairNumber(attr)	PAIR_NUMBER(attr)
#endif

#define unColor(n)		unColor2(AttrOf(n))
#define unColor2(a)		((a) & ALL_BUT_COLOR)

/*
 * Extended-colors stores the color pair in a separate struct-member than the
 * attributes.  But for compatibility, we handle most cases where a program
 * written for non-extended colors stores the color in the attributes by
 * checking for a color pair in both places.
 */
#if NCURSES_EXT_COLORS
#define if_EXT_COLORS(stmt)	stmt
#define SetPair(value,p)	SetPair2((value).ext_color, AttrOf(value), p)
#define SetPair2(c,a,p)		c = (p), \
				a = (unColor2(a) | ColorPair(oldColor(c)))
#define GetPair(value)		GetPair2((value).ext_color, AttrOf(value))
#define GetPair2(c,a)		((c) ? (c) : PairNumber(a))
#define oldColor(p)		(((p) > 255) ? 255 : (p))
#define GET_WINDOW_PAIR(w)	GetPair2((w)->_color, (w)->_attrs)
#define SET_WINDOW_PAIR(w,p)	(w)->_color = (p)
#define SameAttrOf(a,b)		(AttrOf(a) == AttrOf(b) && GetPair(a) == GetPair(b))

#define VIDPUTS(sp,attr,pair)	do { \
				    int vid_pair = pair; \
				    NCURSES_SP_NAME(vid_puts)( \
					NCURSES_SP_ARGx attr, \
					(NCURSES_PAIRS_T) pair, \
					&vid_pair, \
					NCURSES_OUTC_FUNC); \
				} while (0)

#else /* !NCURSES_EXT_COLORS */

#define if_EXT_COLORS(stmt)	/* nothing */
#define SetPair(value,p)	RemAttr(value, A_COLOR), \
				SetAttr(value, AttrOf(value) | ColorPair(p))
#define GetPair(value)		PairNumber(AttrOf(value))
#define GET_WINDOW_PAIR(w)	PairNumber(WINDOW_ATTRS(w))
#define SET_WINDOW_PAIR(w,p)	WINDOW_ATTRS(w) &= ALL_BUT_COLOR, \
				WINDOW_ATTRS(w) |= ColorPair(p)
#define SameAttrOf(a,b)		(AttrOf(a) == AttrOf(b))

#define VIDPUTS(sp,attr,pair)	NCURSES_SP_NAME(vidputs)(NCURSES_SP_ARGx attr, NCURSES_OUTC_FUNC)

#endif /* NCURSES_EXT_COLORS */

#define NCURSES_OUTC_FUNC       NCURSES_SP_NAME(_nc_outch)
#define NCURSES_PUTP2(name,value)    NCURSES_SP_NAME(_nc_putp)(NCURSES_SP_ARGx name, value)
#define NCURSES_PUTP2_FLUSH(name,value)    NCURSES_SP_NAME(_nc_putp_flush)(NCURSES_SP_ARGx name, value)

#if NCURSES_NO_PADDING
#define GetNoPadding(sp)	((sp) ? (sp)->_no_padding : _nc_prescreen._no_padding)
#define SetNoPadding(sp)	_nc_set_no_padding(sp)
extern NCURSES_EXPORT(void)     _nc_set_no_padding(SCREEN *);
#else
#define GetNoPadding(sp)	FALSE
#define SetNoPadding(sp)	/*nothing*/
#endif

#define WINDOW_ATTRS(w)		((w)->_attrs)

#define SCREEN_ATTRS(s)		(*((s)->_current_attr))
#define GET_SCREEN_PAIR(s)	GetPair(SCREEN_ATTRS(s))
#define SET_SCREEN_PAIR(s,p)	SetPair(SCREEN_ATTRS(s), p)

#if USE_REENTRANT || NCURSES_SP_FUNCS
extern NCURSES_EXPORT(int *)    _nc_ptr_Lines (SCREEN *);
extern NCURSES_EXPORT(int *)    _nc_ptr_Cols (SCREEN *);
extern NCURSES_EXPORT(int *)    _nc_ptr_Tabsize (SCREEN *);
extern NCURSES_EXPORT(int *)    _nc_ptr_Escdelay (SCREEN *);
#endif

#if USE_REENTRANT

#define ptrLines(sp)         (sp ? &(sp->_LINES) : &(_nc_prescreen._LINES))
#define ptrCols(sp)          (sp ? &(sp->_COLS) : &(_nc_prescreen._COLS))
#define ptrTabsize(sp)       (sp ? &(sp->_TABSIZE) : &(_nc_prescreen._TABSIZE))
#define ptrEscdelay(sp)      (sp ? &(sp->_ESCDELAY) : &(_nc_prescreen._ESCDELAY))

#define SET_LINES(value)     *_nc_ptr_Lines(SP_PARM) = value
#define SET_COLS(value)      *_nc_ptr_Cols(SP_PARM) = value
#define SET_TABSIZE(value)   *_nc_ptr_Tabsize(SP_PARM) = value
#define SET_ESCDELAY(value)  *_nc_ptr_Escdelay(SP_PARM) = value

#else

#define ptrLines(sp)         &LINES
#define ptrCols(sp)          &COLS
#define ptrTabsize(sp)       &TABSIZE
#define ptrEscdelay(sp)      &ESCDELAY

#define SET_LINES(value)     LINES = value
#define SET_COLS(value)      COLS = value
#define SET_TABSIZE(value)   TABSIZE = value
#define SET_ESCDELAY(value)  ESCDELAY = value

#endif

#define IS_SUBWIN(w)         ((w)->_flags & _SUBWIN)
#define IS_PAD(w)            ((w)->_flags & _ISPAD)
#define IS_WRAPPED(w)        ((w)->_flags & _WRAPPED)

#define HasHardTabs()	(NonEmpty(clear_all_tabs) && NonEmpty(set_tab))

#define TR_MUTEX(data) _tracef("%s@%d: me:%08lX COUNT:%2u/%2d/%6d/%2d/%s%9u: " #data, \
	    __FILE__, __LINE__, \
	    (unsigned long) (pthread_self()), \
	    data.__data.__lock, \
	    data.__data.__count, \
	    data.__data.__owner, \
	    data.__data.__kind, \
	    (data.__data.__nusers > 5) ? " OOPS " : "", \
	    data.__data.__nusers)
#define TR_GLOBAL_MUTEX(name) TR_MUTEX(_nc_globals.mutex_##name)

#if USE_WEAK_SYMBOLS
#if defined(__GNUC__)
#  if defined __USE_ISOC99
#    define _cat_pragma(exp)	_Pragma(#exp)
#    define _weak_pragma(exp)	_cat_pragma(weak name)
#  else
#    define _weak_pragma(exp)
#  endif
#  define _declare(name)	__extension__ extern __typeof__(name) name
#  define weak_symbol(name)	_weak_pragma(name) _declare(name) __attribute__((weak))
#else
#  undef USE_WEAK_SYMBOLS
#  define USE_WEAK_SYMBOLS 0
#endif
#endif

#ifdef USE_PTHREADS

#if USE_REENTRANT
#include <pthread.h>
extern NCURSES_EXPORT(void) _nc_init_pthreads(void);
extern NCURSES_EXPORT(void) _nc_mutex_init(pthread_mutex_t *);
extern NCURSES_EXPORT(int) _nc_mutex_lock(pthread_mutex_t *);
extern NCURSES_EXPORT(int) _nc_mutex_trylock(pthread_mutex_t *);
extern NCURSES_EXPORT(int) _nc_mutex_unlock(pthread_mutex_t *);
#define _nc_lock_global(name)	_nc_mutex_lock(&_nc_globals.mutex_##name)
#define _nc_try_global(name)    _nc_mutex_trylock(&_nc_globals.mutex_##name)
#define _nc_unlock_global(name)	_nc_mutex_unlock(&_nc_globals.mutex_##name)

#else
#error POSIX threads requires --enable-reentrant option
#endif

#ifdef USE_PTHREADS
#  if USE_WEAK_SYMBOLS
weak_symbol(pthread_sigmask);
weak_symbol(pthread_kill);
weak_symbol(pthread_self);
weak_symbol(pthread_equal);
weak_symbol(pthread_mutex_init);
weak_symbol(pthread_mutex_lock);
weak_symbol(pthread_mutex_unlock);
weak_symbol(pthread_mutex_trylock);
weak_symbol(pthread_mutexattr_settype);
weak_symbol(pthread_mutexattr_init);
extern NCURSES_EXPORT(int) _nc_sigprocmask(int, const sigset_t *, sigset_t *);
#    undef  sigprocmask
#    define sigprocmask(a, b, c) _nc_sigprocmask(a, b, c)
#    define GetThreadID() (((pthread_self)) ? pthread_self() : (pthread_t) getpid())
#  else
#    define GetThreadID() pthread_self()
#  endif
#endif

#if HAVE_NANOSLEEP
#undef HAVE_NANOSLEEP
#define HAVE_NANOSLEEP 0	/* nanosleep suspends all threads */
#endif

#else /* !USE_PTHREADS */

#if USE_PTHREADS_EINTR
#  if USE_WEAK_SYMBOLS
#include <pthread.h>
weak_symbol(pthread_sigmask);
weak_symbol(pthread_kill);
weak_symbol(pthread_self);
weak_symbol(pthread_equal);
extern NCURSES_EXPORT(int) _nc_sigprocmask(int, const sigset_t *, sigset_t *);
#    undef  sigprocmask
#    define sigprocmask(a, b, c) _nc_sigprocmask(a, b, c)
#  endif
#endif /* USE_PTHREADS_EINTR */

#define _nc_init_pthreads()	/* nothing */
#define _nc_mutex_init(obj)	/* nothing */

#define _nc_lock_global(name)	/* nothing */
#define _nc_try_global(name)    0
#define _nc_unlock_global(name)	/* nothing */
#endif /* USE_PTHREADS */

#if USE_PTHREADS_EINTR
extern NCURSES_EXPORT(void) _nc_set_read_thread(bool);
#else
#define _nc_set_read_thread(enable)	/* nothing */
#endif

/*
 * When using sp-funcs, locks are targeted to SCREEN-level granularity.
 * So the locking is done in the non-sp-func (which calls the sp-func) rather
 * than in the sp-func itself.
 *
 * Use the _nc_nonsp_XXX functions in the function using "NCURSES_SP_NAME()".
 * Use the _nc_sp_XXX functions in the function using "#if NCURSES_SP_FUNCS".
 */
#if NCURSES_SP_FUNCS

#define _nc_nonsp_lock_global(name)	/* nothing */
#define _nc_nonsp_try_global(name)    0
#define _nc_nonsp_unlock_global(name)	/* nothing */

#define _nc_sp_lock_global(name)	_nc_lock_global(name)
#define _nc_sp_try_global(name)         _nc_try_global(name)
#define _nc_sp_unlock_global(name)	_nc_unlock_global(name)

#else

#define _nc_nonsp_lock_global(name)	_nc_lock_global(name)
#define _nc_nonsp_try_global(name)      _nc_try_global(name)
#define _nc_nonsp_unlock_global(name)	_nc_unlock_global(name)

#define _nc_sp_lock_global(name)	/* nothing */
#define _nc_sp_try_global(name)    0
#define _nc_sp_unlock_global(name)	/* nothing */

#endif

#if HAVE_CLOCK_GETTIME
# define PRECISE_GETTIME 1
# define GetClockTime(t) clock_gettime(CLOCK_REALTIME, t)
# define TimeType struct timespec
# define TimeScale 1000000000L		/* 1e9 */
# define sub_secs tv_nsec
#elif HAVE_GETTIMEOFDAY
# define PRECISE_GETTIME 1
# define GetClockTime(t) gettimeofday(t, 0)
# define TimeType struct timeval
# define TimeScale 1000000L		/* 1e6 */
# define sub_secs tv_usec
#else
# define PRECISE_GETTIME 0
# define TimeType time_t
#endif

/*
 * Definitions for color pairs
 */

#define MAX_OF_TYPE(t)   (int)(((unsigned t)(~0))>>1)

#include <new_pair.h>

#define isDefaultColor(c)	((c) < 0)
#define COLOR_DEFAULT		-1

#if defined(USE_BUILD_CC) || (defined(USE_TERMLIB) && !defined(NEED_NCURSES_CH_T))

#undef NCURSES_CH_T		/* this is not a termlib feature */
#define NCURSES_CH_T void	/* ...but we need a pointer in SCREEN */

#endif	/* USE_TERMLIB */

#ifndef USE_TERMLIB
struct ldat
{
	NCURSES_CH_T	*text;		/* text of the line */
	NCURSES_SIZE_T	firstchar;	/* first changed character in the line */
	NCURSES_SIZE_T	lastchar;	/* last changed character in the line */
	NCURSES_SIZE_T	oldindex;	/* index of the line at last update */
};
#endif	/* USE_TERMLIB */

typedef enum {
	M_XTERM	= -1		/* use xterm's mouse tracking? */
	,M_NONE = 0		/* no mouse device */
#if USE_GPM_SUPPORT
	,M_GPM			/* use GPM */
#endif
#if USE_SYSMOUSE
	,M_SYSMOUSE		/* FreeBSD sysmouse on console */
#endif
#ifdef USE_TERM_DRIVER
	,M_TERM_DRIVER		/* Win32 console, etc */
#endif
} MouseType;

typedef enum {
    	MF_X10 = 0		/* conventional 3-byte format */
	, MF_SGR1006		/* xterm private mode 1006, SGR-style */
#ifdef EXP_XTERM_1005
	, MF_XTERM_1005		/* xterm UTF-8 private mode 1005 */
#endif
} MouseFormat;

/*
 * Structures for scrolling.
 */

typedef struct {
	unsigned long hashval;
	int oldcount, newcount;
	int oldindex, newindex;
} HASHMAP;

/*
 * Structures for soft labels.
 */

struct _SLK;

#if !(defined(USE_TERMLIB) || defined(USE_BUILD_CC))

typedef struct
{
	char *ent_text;		/* text for the label */
	char *form_text;	/* formatted text (left/center/...) */
	int ent_x;		/* x coordinate of this field */
	char dirty;		/* this label has changed */
	char visible;		/* field is visible */
} slk_ent;

typedef struct _SLK {
	bool    dirty;		/* all labels have changed */
	bool    hidden;		/* soft labels are hidden */
	WINDOW  *win;
	slk_ent *ent;
	short   maxlab;		/* number of available labels */
	short   labcnt;		/* number of allocated labels */
	short   maxlen;		/* length of labels */
	NCURSES_CH_T attr;	/* soft label attribute */
} SLK;

#endif	/* USE_TERMLIB */

#if USE_GPM_SUPPORT
#undef buttons			/* term.h defines this, and gpm uses it! */
#include <gpm.h>
#if USE_WEAK_SYMBOLS
weak_symbol(Gpm_Wgetch);
#endif

#ifdef HAVE_LIBDL
/* link dynamically to GPM */
typedef int *TYPE_gpm_fd;
typedef int (*TYPE_Gpm_Open) (Gpm_Connect *, int);
typedef int (*TYPE_Gpm_Close) (void);
typedef int (*TYPE_Gpm_GetEvent) (Gpm_Event *);

#define my_gpm_fd       SP_PARM->_mouse_gpm_fd
#define my_Gpm_Open     SP_PARM->_mouse_Gpm_Open
#define my_Gpm_Close    SP_PARM->_mouse_Gpm_Close
#define my_Gpm_GetEvent SP_PARM->_mouse_Gpm_GetEvent
#else
/* link statically to GPM */
#define my_gpm_fd       &gpm_fd
#define my_Gpm_Open     Gpm_Open
#define my_Gpm_Close    Gpm_Close
#define my_Gpm_GetEvent Gpm_GetEvent
#endif /* HAVE_LIBDL */
#endif /* USE_GPM_SUPPORT */

/*
 * When converting from terminfo to termcap, check for cases where we can trim
 * octal escapes down to 2-character form.  It is useful for terminfo format
 * also, but not as important.
 */
#define MAX_TC_FIXUPS	10
#define MIN_TC_FIXUPS	4

#define isoctal(c) ((c) >= '0' && (c) <= '7')

/*
 * The filesystem database normally uses a single-letter for the lower level
 * of directories.  Use a hexadecimal code for filesystems which do not
 * preserve mixed-case names.
 */
#if MIXEDCASE_FILENAMES
#define LEAF_FMT "%c"
#define LEAF_LEN 1
#else
#define LEAF_FMT "%02x"
#define LEAF_LEN 2
#endif

/*
 * TRACEMSE_FMT is no longer than 80 columns, there are 5 numbers that
 * could at most have 10 digits, and the mask contains no more than 32 bits
 * with each bit representing less than 15 characters.  Usually the whole
 * string is less than 80 columns, but this buffer size is an absolute
 * limit.
 */
#define TRACEMSE_MAX	(80 + (5 * 10) + (32 * 15))
#define TRACEMSE_FMT	"id %2d  at (%2d, %2d, %2d) state %4lx = {" /* } */

#ifdef USE_TERM_DRIVER
struct DriverTCB; /* Terminal Control Block forward declaration */
#define INIT_TERM_DRIVER()	_nc_globals.term_driver = _nc_get_driver
#else
#define INIT_TERM_DRIVER()	/* nothing */
#endif

extern NCURSES_EXPORT_VAR(NCURSES_GLOBALS) _nc_globals;

/* The limit reserves one byte for a terminating NUL */
#define my_getstr_limit	(_nc_globals.getstr_limit - 1)
#define _nc_getstr_limit(n) \
	(((n) < 0) \
	 ? my_getstr_limit \
	 : (((n) > my_getstr_limit) \
	    ? my_getstr_limit \
	    : (n)))

/*
 * Use screen-specific ripoff data (for softkeys) rather than global.
 */
#ifdef USE_SP_RIPOFF
#define safe_ripoff_sp     (sp)->rsp
#define safe_ripoff_stack  (sp)->rippedoff
#else
#define safe_ripoff_sp	   _nc_prescreen.rsp
#define safe_ripoff_stack  _nc_prescreen.rippedoff
#endif

extern NCURSES_EXPORT_VAR(NCURSES_PRESCREEN) _nc_prescreen;

typedef enum {
    ewInitial = 0,
    ewRunning,
    ewSuspend
} ENDWIN;

typedef struct {
	int		_nl;		/* True if NL -> CR/NL is on	    */
	int		_raw;		/* True if in raw mode		    */
	int		_cbreak;	/* 1 if in cbreak mode		    */
					/* > 1 if in halfdelay mode	    */
	int		_echo;		/* True if echo on		    */
} TTY_FLAGS;

#define IsNl(sp)        (sp)->_tty_flags._nl
#define IsRaw(sp)       (sp)->_tty_flags._raw
#define IsCbreak(sp)    (sp)->_tty_flags._cbreak
#define IsEcho(sp)      (sp)->_tty_flags._echo

/*
 * The SCREEN structure.
 */
typedef struct screen {
	int		_ifd;		/* input file descriptor for screen */
	int		_ofd;		/* output file descriptor for screen */
	FILE		*_ofp;		/* output file ptr for screen	    */
	char		*out_buffer;	/* output buffer		    */
	size_t		out_limit;	/* output buffer size		    */
	size_t		out_inuse;	/* output buffer current use	    */
	bool		_filtered;	/* filter() was called		    */
	bool		_prescreen;	/* is in prescreen phase	    */
	bool		_use_env;	/* LINES & COLS from environment?   */
	int		_checkfd;	/* filedesc for typeahead check	    */
	TERMINAL	*_term;		/* terminal type information	    */
	TTY		_saved_tty;	/* savetty/resetty information	    */
	NCURSES_SIZE_T	_lines;		/* screen lines			    */
	NCURSES_SIZE_T	_columns;	/* screen columns		    */

	NCURSES_SIZE_T	_lines_avail;	/* lines available for stdscr	    */
	NCURSES_SIZE_T	_topstolen;	/* lines stolen from top	    */

	WINDOW		*_curscr;	/* current screen		    */
	WINDOW		*_newscr;	/* virtual screen to be updated to  */
	WINDOW		*_stdscr;	/* screen's full-window context	    */

#define CurScreen(sp)  (sp)->_curscr
#define NewScreen(sp)  (sp)->_newscr
#define StdScreen(sp)  (sp)->_stdscr

	TRIES		*_keytry;	/* "Try" for use with keypad mode   */
	TRIES		*_key_ok;	/* Disabled keys via keyok(,FALSE)  */
	bool		_tried;		/* keypad mode was initialized	    */
	bool		_keypad_on;	/* keypad mode is currently on	    */

	bool		_called_wgetch;	/* check for recursion in wgetch()  */
	int		_fifo[FIFO_SIZE];	/* input push-back buffer   */
	short		_fifohead,	/* head of fifo queue		    */
			_fifotail,	/* tail of fifo queue		    */
			_fifopeek,	/* where to peek for next char	    */
			_fifohold;	/* set if breakout marked	    */

	int		_endwin;	/* are we out of window mode?	    */
	NCURSES_CH_T	*_current_attr; /* holds current attributes set	    */
	int		_coloron;	/* is color enabled?		    */
	int		_color_defs;	/* are colors modified		    */
	int		_cursor;	/* visibility of the cursor	    */
	int		_cursrow;	/* physical cursor row		    */
	int		_curscol;	/* physical cursor column	    */
	bool		_notty;		/* true if we cannot switch non-tty */
	TTY_FLAGS	_tty_flags;
	int		_use_meta;	/* use the meta key?		    */
	struct _SLK	*_slk;		/* ptr to soft key struct / NULL    */
	int		slk_format;	/* selected format for this screen  */
	/* cursor movement costs; units are 10ths of milliseconds */
	int		_char_padding;	/* cost of character put	    */
	int		_cr_cost;	/* cost of (carriage_return)	    */
	int		_cup_cost;	/* cost of (cursor_address)	    */
	int		_home_cost;	/* cost of (cursor_home)	    */
	int		_ll_cost;	/* cost of (cursor_to_ll)	    */
	int		_cub1_cost;	/* cost of (cursor_left)	    */
	int		_cuf1_cost;	/* cost of (cursor_right)	    */
	int		_cud1_cost;	/* cost of (cursor_down)	    */
	int		_cuu1_cost;	/* cost of (cursor_up)		    */
	int		_cub_cost;	/* cost of (parm_cursor_left)	    */
	int		_cuf_cost;	/* cost of (parm_cursor_right)	    */
	int		_cud_cost;	/* cost of (parm_cursor_down)	    */
	int		_cuu_cost;	/* cost of (parm_cursor_up)	    */
	int		_hpa_cost;	/* cost of (column_address)	    */
	int		_vpa_cost;	/* cost of (row_address)	    */
	/* used in tty_update.c, must be chars */
	int		_ed_cost;	/* cost of (clr_eos)		    */
	int		_el_cost;	/* cost of (clr_eol)		    */
	int		_el1_cost;	/* cost of (clr_bol)		    */
	int		_dch1_cost;	/* cost of (delete_character)	    */
	int		_ich1_cost;	/* cost of (insert_character)	    */
	int		_dch_cost;	/* cost of (parm_dch)		    */
	int		_ich_cost;	/* cost of (parm_ich)		    */
	int		_ech_cost;	/* cost of (erase_chars)	    */
	int		_rep_cost;	/* cost of (repeat_char)	    */
	int		_hpa_ch_cost;	/* cost of (column_address)	    */
	int		_cup_ch_cost;	/* cost of (cursor_address)	    */
	int		_cuf_ch_cost;	/* cost of (parm_cursor_right)	    */
	int		_inline_cost;	/* cost of inline-move		    */
	int		_smir_cost;	/* cost of (enter_insert_mode)	    */
	int		_rmir_cost;	/* cost of (exit_insert_mode)	    */
	int		_ip_cost;	/* cost of (insert_padding)	    */
	/* used in lib_mvcur.c */
	char *		_address_cursor;
	/* used in tty_update.c */
	int		_scrolling;	/* 1 if terminal's smart enough to  */

	/* used in lib_color.c */
	rgb_bits_t	_direct_color;	/* RGB overrides color-table	     */
	color_t		*_color_table;	/* screen's color palette	     */
	int		_color_count;	/* count of colors in palette	     */
	colorpair_t	*_color_pairs;	/* screen's color pair list	     */
	int		_pair_count;	/* same as COLOR_PAIRS               */
	int		_pair_limit;	/* actual limit of color-pairs       */
	int		_pair_alloc;	/* current table-size of color-pairs */
	chtype		_ok_attributes; /* valid attributes for terminal     */
	chtype		_xmc_suppress;	/* attributes to suppress if xmc     */
	chtype		_xmc_triggers;	/* attributes to process if xmc	     */
	chtype *	_acs_map;	/* the real alternate-charset map    */
	bool *		_screen_acs_map;


	/* used in lib_vidattr.c */
	bool		_use_rmso;	/* true if we may use 'rmso'	     */
	bool		_use_rmul;	/* true if we may use 'rmul'	     */

	/*
	 * These data correspond to the state of the idcok() and idlok()
	 * functions.  A caveat is in order here:  the XSI and SVr4
	 * documentation specify that these functions apply to the window which
	 * is given as an argument.  However, ncurses implements this logic
	 * only for the newscr/curscr update process, _not_ per-window.
	 */
	bool		_nc_sp_idlok;
	bool		_nc_sp_idcok;

	/*
	 * These are the data that support the mouse interface.
	 */
	bool		_mouse_initialized;
	MouseType	_mouse_type;
	int		_maxclick;
	bool		(*_mouse_event) (SCREEN *);
	bool		(*_mouse_inline)(SCREEN *);
	bool		(*_mouse_parse) (SCREEN *, int);
	void		(*_mouse_resume)(SCREEN *);
	void		(*_mouse_wrap)	(SCREEN *);
	int		_mouse_fd;	/* file-descriptor, if any */
	bool		_mouse_active;	/* true if initialized */
	mmask_t		_mouse_mask;	/* set via mousemask() */
	mmask_t		_mouse_mask2;	/* OR's in press/release bits */
	mmask_t		_mouse_bstate;
	MouseFormat	_mouse_format;	/* type of xterm mouse protocol */
	NCURSES_CONST char *_mouse_xtermcap; /* string to enable/disable mouse */
	MEVENT		_mouse_events[EV_MAX];	/* hold the last mouse event seen */
	MEVENT		*_mouse_eventp;	/* next free slot in event queue */

	/*
	 * These are data that support the proper handling of the panel stack on an
	 * per screen basis.
	 */
	struct panelhook _panelHook;

	bool		_sig_winch;
	SCREEN		*_next_screen;

	/* hashes for old and new lines */
	unsigned long	*oldhash, *newhash;
	HASHMAP		*hashtab;
	int		hashtab_len;
	int		*_oldnum_list;
	int		_oldnum_size;

	NCURSES_SP_OUTC	_outch;		/* output handler if not putc */
	NCURSES_OUTC	jump;

	ripoff_t	rippedoff[N_RIPS];
	ripoff_t	*rsp;

	int		_legacy_coding;	/* see use_legacy_coding() */

#if NCURSES_NO_PADDING
	bool		_no_padding;	/* flag to set if padding disabled  */
#endif

#if USE_HARD_TABS
	int		_ht_cost;	/* cost of (tab)		    */
	int		_cbt_cost;	/* cost of (backtab)		    */
#endif /* USE_HARD_TABS */

	/* used in lib_vidattr.c */
#if USE_ITALIC
	bool		_use_ritm;	/* true if we may use 'ritm'	     */
#endif

	/* used in getch/twait */
#if USE_KLIBC_KBD
	bool		_extended_key;	/* true if an extended key	     */
#endif

	/* used in lib_color.c */
#if NCURSES_EXT_FUNCS
	bool		_assumed_color; /* use assumed colors		     */
	bool		_default_color; /* use default colors		     */
	bool		_has_sgr_39_49; /* has ECMA default color support    */
	int		_default_fg;	/* assumed default foreground	     */
	int		_default_bg;	/* assumed default background	     */
	int		_default_pairs;	/* count pairs using default color   */
#endif

	/* system-dependent mouse data */
#if USE_GPM_SUPPORT
	bool		_mouse_gpm_loaded;
	bool		_mouse_gpm_found;
#ifdef HAVE_LIBDL
	void		*_dlopen_gpm;
	TYPE_gpm_fd	_mouse_gpm_fd;
	TYPE_Gpm_Open	_mouse_Gpm_Open;
	TYPE_Gpm_Close	_mouse_Gpm_Close;
	TYPE_Gpm_GetEvent _mouse_Gpm_GetEvent;
#endif
	Gpm_Connect	_mouse_gpm_connect;
#endif /* USE_GPM_SUPPORT */

#if USE_EMX_MOUSE
	int		_emxmouse_wfd;
	int		_emxmouse_thread;
	int		_emxmouse_activated;
	char		_emxmouse_buttons[4];
#endif

#if USE_SYSMOUSE
	MEVENT		_sysmouse_fifo[FIFO_SIZE];
	int		_sysmouse_head;
	int		_sysmouse_tail;
	int		_sysmouse_char_width;	/* character width */
	int		_sysmouse_char_height;	/* character height */
	int		_sysmouse_old_buttons;
	int		_sysmouse_new_buttons;
#endif

#if defined(USE_TERM_DRIVER) || defined(EXP_WIN32_DRIVER)
	MEVENT		_drv_mouse_fifo[FIFO_SIZE];
	int		_drv_mouse_head;
	int		_drv_mouse_tail;
	int		_drv_mouse_old_buttons;
	int		_drv_mouse_new_buttons;
#endif
	/*
	 * This supports automatic resizing
	 */
#if USE_SIZECHANGE
	int		(*_resize)(NCURSES_SP_DCLx int y, int x);
	int		(*_ungetch)(SCREEN *, int);
#endif

#ifdef USE_SP_WINDOWLIST
	WINDOWLIST*	_windowlist;
#define WindowList(sp)  (sp)->_windowlist
#endif

#if USE_REENTRANT
	char		_ttytype[NAMESIZE];
	int		_ESCDELAY;
	int		_TABSIZE;
	int		_LINES;
	int		_COLS;
#endif

#if NCURSES_SP_FUNCS
	bool		use_tioctl;
#endif

	/*
	 * ncurses/ncursesw are the same up to this point.
	 */
#if USE_WIDEC_SUPPORT
	/* recent versions of 'screen' have partially-working support for
	 * UTF-8, but do not permit ACS at the same time (see tty_update.c).
	 */
	bool		_screen_acs_fix;
	bool		_screen_unicode;
#endif

#if NCURSES_EXT_FUNCS && NCURSES_EXT_COLORS
	void		*_ordered_pairs; /* index used by alloc_pair()	     */
	int		_pairs_used;	/* actual number of color-pairs used */
	int		_recent_pair;	/* number for most recent free-pair  */
#endif

#ifdef TRACE
	char		tracechr_buf[40];
	char		tracemse_buf[TRACEMSE_MAX];
#if USE_REENTRANT
	long		_outchars;
	const char	*_tputs_trace;
#endif
#endif
#undef SCREEN
} SCREEN;

extern NCURSES_EXPORT_VAR(SCREEN *) _nc_screen_chain;
extern NCURSES_EXPORT_VAR(SIG_ATOMIC_T) _nc_have_sigwinch;

	WINDOWLIST {
	WINDOWLIST *next;
	SCREEN *screen;		/* screen containing the window */
	WINDOW	win;		/* WINDOW_EXT() needs to account for offset */
#if NCURSES_WIDECHAR
	char addch_work[(MB_LEN_MAX * 9) + 1];
	unsigned addch_used;	/* number of bytes in addch_work[] */
	int addch_x;		/* x-position for addch_work[] */
	int addch_y;		/* y-position for addch_work[] */
#endif
};

#define WINDOW_EXT(w,m) (((WINDOWLIST *)((void *)((char *)(w) - offsetof(WINDOWLIST, win))))->m)

#ifdef USE_SP_WINDOWLIST
#define SP_INIT_WINDOWLIST(sp)	WindowList(sp) = 0
#else
#define SP_INIT_WINDOWLIST(sp)	/* nothing */
#endif

#define SP_PRE_INIT(sp)                         \
    sp->_cursrow           = -1;                \
    sp->_curscol           = -1;                \
    IsNl(sp)               = TRUE;              \
    IsRaw(sp)              = FALSE;             \
    IsCbreak(sp)           = 0;                 \
    IsEcho(sp)             = TRUE;              \
    sp->_fifohead          = -1;                \
    sp->_endwin            = ewSuspend;         \
    sp->_cursor            = -1;                \
    SP_INIT_WINDOWLIST(sp);                     \
    sp->_outch             = NCURSES_OUTC_FUNC; \
    sp->jump               = 0                  \

/* usually in <limits.h> */
#ifndef UCHAR_MAX
#define UCHAR_MAX 255
#endif

/* The terminfo source is assumed to be 7-bit ASCII */
#define is7bits(c)	((unsigned)(c) < 128)

/* Checks for isprint() should be done on 8-bit characters (non-wide) */
#define is8bits(c)	((unsigned)(c) <= UCHAR_MAX)

#ifndef min
#define min(a,b)	((a) > (b)  ?  (b)  :  (a))
#endif

#ifndef max
#define max(a,b)	((a) < (b)  ?  (b)  :  (a))
#endif

/* usually in <unistd.h> */
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

#ifndef R_OK
#define	R_OK	4		/* Test for read permission.  */
#endif
#ifndef W_OK
#define	W_OK	2		/* Test for write permission.  */
#endif
#ifndef X_OK
#define	X_OK	1		/* Test for execute permission.  */
#endif
#ifndef F_OK
#define	F_OK	0		/* Test for existence.  */
#endif

#if HAVE_FCNTL_H
#include <fcntl.h>		/* may define O_BINARY	*/
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef TRACE
#if USE_REENTRANT
#define COUNT_OUTCHARS(n) _nc_count_outchars(n);
#else
#define COUNT_OUTCHARS(n) _nc_outchars += (n);
#endif
#else
#define COUNT_OUTCHARS(n) /* nothing */
#endif

#define RESET_OUTCHARS() COUNT_OUTCHARS(-_nc_outchars)

#define UChar(c)	((unsigned char)(c))
#define UShort(c)	((unsigned short)(c))
#define ChCharOf(c)	((chtype)(c) & (chtype)A_CHARTEXT)
#define ChAttrOf(c)	((chtype)(c) & (chtype)A_ATTRIBUTES)

#define TR_PUTC(c)	TR(TRACE_CHARPUT, ("PUTC %#x", UChar(c)))

#ifndef MB_LEN_MAX
#define MB_LEN_MAX 16	/* should be >= MB_CUR_MAX, but that may be a function */
#endif

#if USE_WIDEC_SUPPORT /* { */
/* true if the status/errno indicate an illegal multibyte sequence */
#define isEILSEQ(status) (((size_t)status == (size_t)-1) && (errno == EILSEQ))

#define init_mb(state)	memset(&(state), 0, sizeof(state))

#if NCURSES_EXT_COLORS
#define NulColor	, 0
#else
#define NulColor	/* nothing */
#endif

#define NulChar		0,0,0,0	/* FIXME: see CCHARW_MAX */
#define CharOf(c)	((c).chars[0])
#define AttrOf(c)	((c).attr)

#define AddAttr(c,a)	AttrOf(c) |=  ((a) & A_ATTRIBUTES)
#define RemAttr(c,a)	AttrOf(c) &= ~((a) & A_ATTRIBUTES)
#define SetAttr(c,a)	AttrOf(c) =   ((a) & A_ATTRIBUTES) | WidecExt(c)

#define NewChar2(c,a)	{ a, { c, NulChar } NulColor }
#define NewChar(ch)	NewChar2(ChCharOf(ch), ChAttrOf(ch))

#if CCHARW_MAX == 5
#define CharEq(a,b)	(((a).attr == (b).attr) \
		       && (a).chars[0] == (b).chars[0] \
		       && (a).chars[1] == (b).chars[1] \
		       && (a).chars[2] == (b).chars[2] \
		       && (a).chars[3] == (b).chars[3] \
		       && (a).chars[4] == (b).chars[4] \
			if_EXT_COLORS(&& (a).ext_color == (b).ext_color))
#elif CCHARW_MAX > 0
#error Inconsistent values for CCHARW_MAX
#else
#define CharEq(a,b)	(!memcmp(&(a), &(b), sizeof(a)))
#endif

#define SetChar(ch,c,a) do {							    \
			    NCURSES_CH_T *_cp = &(ch);				    \
			    memset(_cp, 0, sizeof(ch));				    \
			    _cp->chars[0] = (wchar_t) (c);			    \
			    _cp->attr = (a);					    \
			    if_EXT_COLORS(SetPair(ch, PairNumber(a)));		    \
			} while (0)
#define CHREF(wch)	(&(wch))
#define CHDEREF(wch)	(*(wch))
#define ARG_CH_T	NCURSES_CH_T *
#define CARG_CH_T	const NCURSES_CH_T *
#define PUTC_DATA	char PUTC_buf[MB_LEN_MAX]; int PUTC_i, PUTC_n; \
			mbstate_t PUT_st; wchar_t PUTC_ch
#define PUTC_INIT	init_mb (PUT_st)
#define PUTC(ch)	do { if(!isWidecExt(ch)) {				    \
			if (Charable(ch)) {					    \
			    TR_PUTC(CharOf(ch));				    \
			    NCURSES_OUTC_FUNC (NCURSES_SP_ARGx CharOf(ch));	    \
			    COUNT_OUTCHARS(1);					    \
			} else {						    \
			    for (PUTC_i = 0; PUTC_i < CCHARW_MAX; ++PUTC_i) {	    \
				PUTC_ch = (ch).chars[PUTC_i];			    \
				if (PUTC_ch == L'\0')				    \
				    break;					    \
				PUTC_INIT;					    \
				PUTC_n = (int) wcrtomb(PUTC_buf,		    \
						       (ch).chars[PUTC_i], &PUT_st); \
				if (PUTC_n <= 0) {				    \
				    if (PUTC_ch && is8bits(PUTC_ch) && PUTC_i == 0) { \
					TR_PUTC(CharOf(ch));			    \
					NCURSES_OUTC_FUNC (NCURSES_SP_ARGx CharOf(ch)); \
				    }						    \
				    break;					    \
				} else {					    \
				    int PUTC_j;					    \
				    for (PUTC_j = 0; PUTC_j < PUTC_n; ++PUTC_j) {   \
					TR_PUTC(PUTC_buf[PUTC_j]);		    \
					NCURSES_OUTC_FUNC (NCURSES_SP_ARGx PUTC_buf[PUTC_j]); \
				    }						    \
				}						    \
			    }							    \
			    COUNT_OUTCHARS(PUTC_i);				    \
			} } } while (0)

#define BLANK		NewChar2(' ', WA_NORMAL)
#define ZEROS		NewChar2('\0', WA_NORMAL)
#define ISBLANK(ch)	((ch).chars[0] == L' ' && (ch).chars[1] == L'\0')

	/*
	 * Wide characters cannot be represented in the A_CHARTEXT mask of
	 * attr_t's but an application might have set a narrow character there.
	 * But even in that case, it would only be a printable character, or
	 * zero.  Otherwise we can use those bits to tell if a cell is the
	 * first or extension part of a wide character.
	 */
#define WidecExt(ch)	(int) (AttrOf(ch) & A_CHARTEXT)
#define isWidecBase(ch)	(WidecExt(ch) == 1)
#define isWidecExt(ch)	(WidecExt(ch) > 1 && WidecExt(ch) < 32)
#define SetWidecExt(dst, ext)	AttrOf(dst) &= ~A_CHARTEXT,		\
				AttrOf(dst) |= (attr_t) (ext + 1)

#define if_WIDEC(code)  code
#define Charable(ch)	(((SP_PARM->_legacy_coding)			\
			 || (AttrOf(ch) & A_ALTCHARSET)			\
			 || (!isWidecExt(ch))) &&			\
			     (ch).chars[1] == L'\0' &&			\
			     _nc_is_charable(CharOf(ch)))

#define L(ch)		L ## ch
#else /* }{ */
#define CharOf(c)	ChCharOf(c)
#define AttrOf(c)	ChAttrOf(c)
#define AddAttr(c,a)	c |= (a)
#define RemAttr(c,a)	c &= ~((a) & A_ATTRIBUTES)
#define SetAttr(c,a)	c = ((c) & ~A_ATTRIBUTES) | (a)
#define NewChar(ch)	(ch)
#define NewChar2(c,a)	((c) | (a))
#define CharEq(a,b)	((a) == (b))
#define SetChar(ch,c,a)	ch = (c) | (a)
#define CHREF(wch)	wch
#define CHDEREF(wch)	wch
#define ARG_CH_T	NCURSES_CH_T
#define CARG_CH_T	NCURSES_CH_T
#define PUTC_DATA	/* nothing */
#define PUTC(ch)	{ \
			    TR_PUTC(ch); \
			    NCURSES_OUTC_FUNC (NCURSES_SP_ARGx (int) ch); \
			}

#define BLANK		(' '|A_NORMAL)
#define ZEROS		('\0'|A_NORMAL)
#define ISBLANK(ch)	(CharOf(ch) == ' ')

#define isWidecExt(ch)	(0)
#define if_WIDEC(code) /* nothing */

#define Charable(ch)	(CharOf(ch) >= ' ' && CharOf(ch) <= '~')
#define L(ch)		ch
#endif /* } */

#define AttrOfD(ch)	AttrOf(CHDEREF(ch))
#define CharOfD(ch)	CharOf(CHDEREF(ch))
#define SetChar2(wch,ch)    SetChar(wch,ChCharOf(ch),ChAttrOf(ch))

#define BLANK_ATTR	A_NORMAL
#define BLANK_TEXT	L(' ')

#define CHANGED     -1

#define LEGALYX(w, y, x) \
	      ((w) != 0 && \
		((x) >= 0 && (x) <= (w)->_maxx && \
		 (y) >= 0 && (y) <= (w)->_maxy))

#define CHANGED_CELL(line,col) \
	if (line->firstchar == _NOCHANGE) \
		line->firstchar = line->lastchar = (NCURSES_SIZE_T) (col); \
	else if ((col) < line->firstchar) \
		line->firstchar = (NCURSES_SIZE_T) (col); \
	else if ((col) > line->lastchar) \
		line->lastchar = (NCURSES_SIZE_T) (col)

#define CHANGED_RANGE(line,start,end) \
	if (line->firstchar == _NOCHANGE \
	 || line->firstchar > (start)) \
		line->firstchar = (NCURSES_SIZE_T) (start); \
	if (line->lastchar == _NOCHANGE \
	 || line->lastchar < (end)) \
		line->lastchar = (NCURSES_SIZE_T) (end)

#define CHANGED_TO_EOL(line,start,end) \
	if (line->firstchar == _NOCHANGE \
	 || line->firstchar > (start)) \
		line->firstchar = (NCURSES_SIZE_T) (start); \
	line->lastchar = (NCURSES_SIZE_T) (end)

#define SIZEOF(v) (sizeof(v)/sizeof(v[0]))

#include <nc_alloc.h>
#include <nc_access.h>

#define FreeIfNeeded(p)  if ((p) != 0) free(p)

/* FreeAndNull() is not a comma-separated expression because some compilers
 * do not accept a mixture of void with values.
 */
#define FreeAndNull(p)   do { free(p); p = 0; } while (0)

#ifdef EXP_OOM_TESTING
extern NCURSES_EXPORT(void *)	_nc_oom_malloc(size_t size);
extern NCURSES_EXPORT(void *)	_nc_oom_calloc(size_t nmemb, size_t size);
extern NCURSES_EXPORT(void *)	_nc_oom_realloc(void *ptr, size_t size);
extern NCURSES_EXPORT(void)	_nc_oom_free(void *ptr);
extern NCURSES_EXPORT(char *)	_nc_oom_strdup(const char *ptr);
#define malloc(size)		_nc_oom_malloc(size)
#define calloc(nmemb, size)	_nc_oom_calloc(nmemb, size)
#define realloc(ptr, size)	_nc_oom_realloc(ptr, size)
#define free(ptr)		_nc_oom_free(ptr)
#define strdup(ptr)		_nc_oom_strdup(ptr)
#endif

/*
 * Use these for tic/infocmp malloc failures.  Generally the ncurses library
 * tries to limp along after a failure.
 */
#define TYPE_MALLOC(type, size, name) \
	do { \
	    name = typeMalloc(type, size); \
	    if (name == 0) \
		_nc_err_abort(MSG_NO_MEMORY); \
	} while (0)

#define TYPE_CALLOC(type, size, name) \
	do { \
	    name = typeCalloc(type, size); \
	    if (name == 0) \
		_nc_err_abort(MSG_NO_MEMORY); \
	} while (0)

#define TYPE_REALLOC(type, size, name) \
	do { \
	    name = typeRealloc(type, size, name); \
	    if (name == 0) \
		_nc_err_abort(MSG_NO_MEMORY); \
	} while (0)

/*
 * TTY bit definition for converting tabs to spaces.
 */
#ifdef TAB3
# define OFLAGS_TABS TAB3	/* POSIX specifies TAB3 */
#else
# ifdef XTABS
#  define OFLAGS_TABS XTABS	/* XTABS is usually the "same" */
# else
#  ifdef OXTABS
#   define OFLAGS_TABS OXTABS	/* the traditional BSD equivalent */
#  else
#   define OFLAGS_TABS 0
#  endif
# endif
#endif

#ifdef __TANDEM
#define ROOT_UID 65535
#endif

#ifndef ROOT_UID
#define ROOT_UID 0
#endif

/*
 * Standardize/simplify common loops
 */
#define each_screen(p) p = _nc_screen_chain; p != 0; p = (p)->_next_screen
#define each_window(sp,p) p = WindowList(sp); p != 0; p = (p)->next
#define each_ripoff(p) p = safe_ripoff_stack; (p - safe_ripoff_stack) < N_RIPS; ++p

/*
 * Prefixes for call/return points of library function traces.  We use these to
 * instrument the public functions so that the traces can be easily transformed
 * into regression scripts.
 */
#define T_CALLED(fmt) "called {" fmt
#define T_CREATE(fmt) "create :" fmt
#define T_RETURN(fmt) "return }" fmt

#define NonNull(s)              ((s) != 0 ? s : "<null>")
#define NonEmpty(s)             ((s) != 0 && *(s) != '\0')

#ifdef TRACE

#if USE_REENTRANT
#define TPUTS_TRACE(s)	_nc_set_tputs_trace(s);
#else
#define TPUTS_TRACE(s)	_nc_tputs_trace = s;
#endif

#ifdef HAVE_CONSISTENT_GETENV
#define START_TRACE() \
	if ((_nc_tracing & TRACE_MAXIMUM) == 0) { \
	    int t = _nc_getenv_num("NCURSES_TRACE"); \
	    if (t >= 0) \
		curses_trace((unsigned) t); \
	}
#else
#define START_TRACE() /* nothing */
#endif

/*
 * Many of the _tracef() calls use static buffers; lock the trace state before
 * trying to fill them.
 */
#if USE_REENTRANT
#define USE_TRACEF(mask) _nc_use_tracef(mask)
extern NCURSES_EXPORT(int)	_nc_use_tracef (unsigned);
extern NCURSES_EXPORT(void)	_nc_locked_tracef (const char *, ...) GCC_PRINTFLIKE(1,2);
#else
#define USE_TRACEF(mask) (_nc_tracing & (mask))
#define _nc_locked_tracef _tracef
#endif

#define TR(n, a)	if (USE_TRACEF(n)) _nc_locked_tracef a
#define T(a)		TR(TRACE_CALLS, a)
#define TRACE_RETURN(value,type)     return _nc_retrace_##type((type)(value))
#define TRACE_RETURN1(value,dst)     return _nc_retrace_##dst(value)
#define TRACE_RETURN2(value,dst,src) return _nc_retrace_##dst##_##src(value)
#define TRACE_RETURN_SP(value,type)  return _nc_retrace_##type(SP_PARM, value)

typedef void VoidFunc(void);

#define TR_FUNC_LEN		((sizeof(void *) + sizeof(void (*)(void))) * 2 + 4)
#define TR_FUNC_BFR(max)	char tr_func_data[max][TR_FUNC_LEN]
#define TR_FUNC_ARG(num,func)	_nc_fmt_funcptr(&tr_func_data[num][0], (const char *)&(func), sizeof((func)))

#define returnAttr(code)	TRACE_RETURN(code,attr_t)
#define returnBits(code)	TRACE_RETURN(code,unsigned)
#define returnBool(code)	TRACE_RETURN(code,bool)
#define returnCPtr(code)	TRACE_RETURN1(code,cptr)
#define returnCVoidPtr(code)	TRACE_RETURN1(code,cvoid_ptr)
#define returnChar(code)	TRACE_RETURN(code,char)
#define returnChtype(code)	TRACE_RETURN(code,chtype)
#define returnCode(code)	TRACE_RETURN(code,int)
#define returnIntAttr(code)	TRACE_RETURN2(code,int,attr_t)
#define returnMMask(code)	TRACE_RETURN_SP(code,mmask_t)
#define returnPtr(code)		TRACE_RETURN1(code,ptr)
#define returnSP(code)		TRACE_RETURN1(code,sp)
#define returnVoid		{ T((T_RETURN(""))); return; }
#define returnVoidPtr(code)	TRACE_RETURN1(code,void_ptr)
#define returnWin(code)		TRACE_RETURN1(code,win)

#define returnDB(rc)		do { TR(TRACE_DATABASE,(T_RETURN("code %d"), (rc))); return (rc); } while (0)
#define returnPtrDB(rc)		do { TR(TRACE_DATABASE,(T_RETURN("%p"), (rc))); return (rc); } while (0)
#define returnVoidDB		do { TR(TRACE_DATABASE,(T_RETURN(""))); return; } while (0)

extern NCURSES_EXPORT(NCURSES_BOOL)     _nc_retrace_bool (int);
extern NCURSES_EXPORT(NCURSES_CONST void *) _nc_retrace_cvoid_ptr (NCURSES_CONST void *);
extern NCURSES_EXPORT(SCREEN *)         _nc_retrace_sp (SCREEN *);
extern NCURSES_EXPORT(WINDOW *)         _nc_retrace_win (WINDOW *);
extern NCURSES_EXPORT(attr_t)           _nc_retrace_attr_t (attr_t);
extern NCURSES_EXPORT(char *)           _nc_retrace_ptr (char *);
extern NCURSES_EXPORT(char *)           _nc_trace_ttymode(const TTY *tty);
extern NCURSES_EXPORT(char *)           _nc_varargs (const char *, va_list);
extern NCURSES_EXPORT(chtype)           _nc_retrace_chtype (chtype);
extern NCURSES_EXPORT(const char *)     _nc_altcharset_name(attr_t, chtype);
extern NCURSES_EXPORT(const char *)     _nc_retrace_cptr (const char *);
extern NCURSES_EXPORT(char)             _nc_retrace_char (int);
extern NCURSES_EXPORT(int)              _nc_retrace_int (int);
extern NCURSES_EXPORT(int)              _nc_retrace_int_attr_t (attr_t);
extern NCURSES_EXPORT(mmask_t)          _nc_retrace_mmask_t (SCREEN *, mmask_t);
extern NCURSES_EXPORT(unsigned)         _nc_retrace_unsigned (unsigned);
extern NCURSES_EXPORT(void *)           _nc_retrace_void_ptr (void *);
extern NCURSES_EXPORT(void)             _nc_fifo_dump (SCREEN *);

extern NCURSES_EXPORT(char *)           _nc_fmt_funcptr(char *, const char *, size_t);

#if USE_REENTRANT
NCURSES_WRAPPED_VAR(long, _nc_outchars);
NCURSES_WRAPPED_VAR(const char *, _nc_tputs_trace);
#define _nc_outchars       NCURSES_PUBLIC_VAR(_nc_outchars())
#define _nc_tputs_trace    NCURSES_PUBLIC_VAR(_nc_tputs_trace())
extern NCURSES_EXPORT(void)		_nc_set_tputs_trace (const char *);
extern NCURSES_EXPORT(void)		_nc_count_outchars (long);
#else
extern NCURSES_EXPORT_VAR(const char *) _nc_tputs_trace;
extern NCURSES_EXPORT_VAR(long)         _nc_outchars;
#endif

extern NCURSES_EXPORT_VAR(unsigned)     _nc_tracing;

extern NCURSES_EXPORT(char *) _nc_tracebits (void);
extern NCURSES_EXPORT(char *) _tracemouse (const MEVENT *);
extern NCURSES_EXPORT(void) _tracedump (const char *, WINDOW *);

#if USE_WIDEC_SUPPORT
extern NCURSES_EXPORT(const char *) _nc_viswbuf2 (int, const wchar_t *);
extern NCURSES_EXPORT(const char *) _nc_viswbufn (const wchar_t *, int);
#endif

extern NCURSES_EXPORT(const char *) _nc_viscbuf2 (int, const NCURSES_CH_T *, int);
extern NCURSES_EXPORT(const char *) _nc_viscbuf (const NCURSES_CH_T *, int);

#else /* !TRACE */

#define START_TRACE() /* nothing */

#define T(a)
#define TR(n, a)
#define TPUTS_TRACE(s)
#define TR_FUNC_BFR(max)

#define returnAttr(code)	return code
#define returnBits(code)	return code
#define returnBool(code)	return code
#define returnCPtr(code)	return code
#define returnCVoidPtr(code)	return code
#define returnChar(code)	return ((char) code)
#define returnChtype(code)	return code
#define returnCode(code)	return code
#define returnIntAttr(code)	return code
#define returnMMask(code)	return code
#define returnPtr(code)		return code
#define returnSP(code)		return code
#define returnVoid		return
#define returnVoidPtr(code)	return code
#define returnWin(code)		return code

#define returnDB(code)		return code
#define returnPtrDB(rc)		return rc
#define returnVoidDB		return

#endif /* TRACE/!TRACE */

/*
 * Workaround for defective implementation of gcc attribute warn_unused_result
 */
#if defined(__GNUC__) && defined(_FORTIFY_SOURCE)
#define IGNORE_RC(func) errno = (int) func
#else
#define IGNORE_RC(func) (void) func
#endif /* gcc workarounds */

/*
 * Return-codes for tgetent() and friends.
 */
#define TGETENT_YES  1		/* entry is found */
#define TGETENT_NO   0		/* entry is not found */
#define TGETENT_ERR -1		/* an error occurred */

extern NCURSES_EXPORT(const char *) _nc_visbuf2 (int, const char *);
extern NCURSES_EXPORT(const char *) _nc_visbufn (const char *, int);

#define EMPTY_MODULE(name) \
extern	NCURSES_EXPORT(void) name (void); \
	NCURSES_EXPORT(void) name (void) { }

#define ALL_BUT_COLOR ((chtype)~(A_COLOR))
#define NONBLANK_ATTR (A_BOLD | A_DIM | A_BLINK | A_ITALIC)
#define TPARM_ATTR    (A_STANDOUT | A_UNDERLINE | A_REVERSE | A_BLINK | A_DIM | A_BOLD | A_ALTCHARSET | A_INVIS | A_PROTECT)
#define XMC_CONFLICT  (A_STANDOUT | A_UNDERLINE | A_REVERSE | A_BLINK | A_DIM | A_BOLD | A_INVIS | A_PROTECT | A_ITALIC)
#define XMC_CHANGES(c) ((c) & SP_PARM->_xmc_suppress)

#define toggle_attr_on(S,at) {\
   if (PairNumber(at) > 0) {\
      (S) = ((S) & ALL_BUT_COLOR) | (attr_t) (at);\
   } else {\
      (S) |= (attr_t) (at);\
   }\
   TR(TRACE_ATTRS, ("new attribute is %s", _traceattr((S))));}


#define toggle_attr_off(S,at) {\
   if (PairNumber(at) > 0) {\
      (S) &= ~(at|A_COLOR);\
   } else {\
      (S) &= ~(at);\
   }\
   TR(TRACE_ATTRS, ("new attribute is %s", _traceattr((S))));}

#define DelCharCost(sp,count) \
		((parm_dch != 0) \
		? sp->_dch_cost \
		: ((delete_character != 0) \
			? (sp->_dch1_cost * count) \
			: INFINITY))

#define InsCharCost(sp,count) \
		((parm_ich != 0) \
		? sp->_ich_cost \
		: ((enter_insert_mode && exit_insert_mode) \
		  ? sp->_smir_cost + sp->_rmir_cost + (sp->_ip_cost * count) \
		  : ((insert_character != 0) \
		    ? ((sp->_ich1_cost + sp->_ip_cost) * count) \
		    : INFINITY)))

#if USE_XMC_SUPPORT
#define UpdateAttrs(sp,c) if (!SameAttrOf(SCREEN_ATTRS(sp), c)) { \
				attr_t chg = AttrOf(SCREEN_ATTRS(sp)); \
				VIDPUTS(sp, AttrOf(c), GetPair(c)); \
				if (magic_cookie_glitch > 0 \
				 && XMC_CHANGES((chg ^ AttrOf(SCREEN_ATTRS(sp))))) { \
					T(("%s @%d before glitch %d,%d", \
						__FILE__, __LINE__, \
						sp->_cursrow, \
						sp->_curscol)); \
					NCURSES_SP_NAME(_nc_do_xmc_glitch)(NCURSES_SP_ARGx chg); \
				} \
			}
#else
#define UpdateAttrs(sp,c) if (!SameAttrOf(SCREEN_ATTRS(sp), c)) { \
				    VIDPUTS(sp, AttrOf(c), GetPair(c)); \
			}
#endif

/*
 * Macros to make additional parameter to implement wgetch_events()
 */
#ifdef NCURSES_WGETCH_EVENTS
#define EVENTLIST_0th(param) param
#define EVENTLIST_1st(param) param
#define EVENTLIST_2nd(param) , param
#define TWAIT_MASK (TW_ANY | TW_EVENT)
#else
#define EVENTLIST_0th(param) void
#define EVENTLIST_1st(param) /* nothing */
#define EVENTLIST_2nd(param) /* nothing */
#define TWAIT_MASK TW_ANY
#endif

#if NCURSES_EXPANDED && NCURSES_EXT_FUNCS

#undef  toggle_attr_on
#define toggle_attr_on(S,at) _nc_toggle_attr_on(&(S), at)
extern NCURSES_EXPORT(void) _nc_toggle_attr_on (attr_t *, attr_t);

#undef  toggle_attr_off
#define toggle_attr_off(S,at) _nc_toggle_attr_off(&(S), at)
extern NCURSES_EXPORT(void) _nc_toggle_attr_off (attr_t *, attr_t);

#undef  DelCharCost
#define DelCharCost(sp, count) NCURSES_SP_NAME(_nc_DelCharCost)(NCURSES_SP_ARGx count)

#undef  InsCharCost
#define InsCharCost(sp, count) NCURSES_SP_NAME(_nc_InsCharCost)(NCURSES_SP_ARGx count)

extern NCURSES_EXPORT(int) NCURSES_SP_NAME(_nc_DelCharCost) (NCURSES_SP_DCLx int _c);
extern NCURSES_EXPORT(int) NCURSES_SP_NAME(_nc_InsCharCost) (NCURSES_SP_DCLx int _c);

#undef  UpdateAttrs
#define UpdateAttrs(sp,c) NCURSES_SP_NAME(_nc_UpdateAttrs)(NCURSES_SP_ARGx CHREF(c))

#if USE_WIDEC_SUPPORT || defined(NEED_NCURSES_CH_T)
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(_nc_UpdateAttrs) (NCURSES_SP_DCLx CARG_CH_T _c);
#else
extern NCURSES_EXPORT(void) NCURSES_SP_NAME(_nc_UpdateAttrs) (NCURSES_SP_DCLx chtype c);
#endif

#if NCURSES_SP_FUNCS
extern NCURSES_EXPORT(int) _nc_DelCharCost (int);
extern NCURSES_EXPORT(int) _nc_InsCharCost (int);
extern NCURSES_EXPORT(void) _nc_UpdateAttrs (CARG_CH_T);
#endif /* NCURSES_SP_FUNCS */

#else

extern NCURSES_EXPORT(void) _nc_expanded (void);

#endif

#if !NCURSES_EXT_FUNCS
#define set_escdelay(value) ESCDELAY = value
#endif

#if !HAVE_GETCWD
#define getcwd(buf,len) getwd(buf)
#endif

#define save_ttytype(termp) \
	if (TerminalType(termp).term_names != 0) { \
	    _nc_STRNCPY(ttytype, \
	    		TerminalType(termp).term_names, \
			NAMESIZE - 1); \
	    ttytype[NAMESIZE - 1] = '\0'; \
	}

#if !NCURSES_WCWIDTH_GRAPHICS
extern NCURSES_EXPORT(int) _nc_wacs_width(unsigned);
#else
#define _nc_wacs_width(ch) wcwidth(ch)
#endif

/* charable.c */
#if USE_WIDEC_SUPPORT
extern NCURSES_EXPORT(bool) _nc_is_charable(wchar_t);
extern NCURSES_EXPORT(int) _nc_to_char(wint_t);
extern NCURSES_EXPORT(wint_t) _nc_to_widechar(int);
#endif

/* comp_captab.c */
typedef struct {
	short	nte_name;	/* offset of name to hash on */
	int	nte_type;	/* BOOLEAN, NUMBER or STRING */
	short	nte_index;	/* index of associated variable in its array */
	short	nte_link;	/* index in table of next hash, or -1 */
} name_table_data;

typedef struct
{
	short	from;
	short	to;
	short	source;
} alias_table_data;

/* comp_userdefs.c */
typedef struct {
	short	ute_name;	/* offset of name to hash on */
	unsigned ute_type;	/* mask (BOOLEAN, NUMBER, STRING) */
	unsigned ute_argc;	/* number of parameters */
	unsigned ute_args;	/* bit-mask for string parameters */
	short	ute_index;	/* index of associated variable in its array */
	short	ute_link;	/* index in table of next hash, or -1 */
} user_table_data;

/* doupdate.c */
#if USE_XMC_SUPPORT
extern NCURSES_EXPORT(void) _nc_do_xmc_glitch (attr_t);
#endif

/* hardscroll.c */
#if defined(TRACE) || defined(SCROLLDEBUG) || defined(HASHDEBUG)
extern NCURSES_EXPORT(void) _nc_linedump (void);
#endif

/* lib_acs.c */
extern NCURSES_EXPORT(void) _nc_init_acs (void); /* corresponds to traditional 'init_acs()' */
extern NCURSES_EXPORT(int)  _nc_msec_cost (const char *const, int);

/* lib_addch.c */
#if USE_WIDEC_SUPPORT
extern NCURSES_EXPORT(int) _nc_build_wch(WINDOW *win, ARG_CH_T ch);
#endif

/* lib_addstr.c */
#if USE_WIDEC_SUPPORT && !(defined(USE_TERMLIB) || defined(USE_BUILD_CC))
extern NCURSES_EXPORT(int) _nc_wchstrlen(const cchar_t *);
#endif

/* lib_color.c */
extern NCURSES_EXPORT(int) _nc_init_color(SCREEN *, int, int, int, int);
extern NCURSES_EXPORT(int) _nc_init_pair(SCREEN *, int, int, int);
extern NCURSES_EXPORT(int) _nc_pair_content(SCREEN *, int, int *, int *);
extern NCURSES_EXPORT(bool) _nc_reset_colors(void);
extern NCURSES_EXPORT(void) _nc_reserve_pairs(SCREEN *, int);
extern NCURSES_EXPORT(void) _nc_change_pair(SCREEN *, int);

#define ReservePairs(sp,want) \
	    if ((sp->_color_pairs == 0) || (want >= sp->_pair_alloc)) \
		_nc_reserve_pairs(sp, want)

/* lib_getch.c */
extern NCURSES_EXPORT(int) _nc_wgetch(WINDOW *, int *, int EVENTLIST_2nd(_nc_eventlist *));

/* lib_insch.c */
extern NCURSES_EXPORT(int) _nc_insert_ch(SCREEN *, WINDOW *, chtype);

/* lib_mvcur.c */
#define INFINITY	1000000	/* cost: too high to use */

extern NCURSES_EXPORT(int) _nc_mvcur(int yold, int xold, int ynew, int xnew);

extern NCURSES_EXPORT(void) _nc_mvcur_init (void);
extern NCURSES_EXPORT(void) _nc_mvcur_resume (void);
extern NCURSES_EXPORT(void) _nc_mvcur_wrap (void);

extern NCURSES_EXPORT(int) _nc_scrolln (int, int, int, int);

extern NCURSES_EXPORT(void) _nc_screen_init (void);
extern NCURSES_EXPORT(void) _nc_screen_resume (void);
extern NCURSES_EXPORT(void) _nc_screen_wrap (void);

/* lib_mouse.c */
extern NCURSES_EXPORT(bool) _nc_has_mouse (SCREEN *);

/* lib_mvcur.c */
#define INFINITY	1000000	/* cost: too high to use */
#define BAUDBYTE	9	/* 9 = 7 bits + 1 parity + 1 stop */

/* lib_setup.c */
extern NCURSES_EXPORT(char *) _nc_get_locale(void);
extern NCURSES_EXPORT(int)    _nc_unicode_locale(void);
extern NCURSES_EXPORT(int)    _nc_locale_breaks_acs(TERMINAL *);
extern NCURSES_EXPORT(int)    _nc_setupterm(const char *, int, int *, int);
extern NCURSES_EXPORT(void)   _nc_tinfo_cmdch(TERMINAL *, int);

#ifdef USE_PTHREADS
extern NCURSES_EXPORT(SCREEN *) _nc_find_prescr(void);
extern NCURSES_EXPORT(void)   _nc_forget_prescr(void);
#else
#define _nc_find_prescr()     _nc_prescreen.allocated
#define _nc_forget_prescr()   _nc_prescreen.allocated = 0
#endif

/* lib_set_term.c */
extern NCURSES_EXPORT(int)    _nc_ripoffline(int, int(*)(WINDOW*, int));

/* lib_setup.c */
#define ExitTerminfo(code)    exit_terminfo(code)

#define SETUP_FAIL ERR

#define ret_error(rc, fmt, p, q)	if (errret) {\
					    *errret = rc;\
					    q;\
					    returnCode(SETUP_FAIL);\
					} else {\
					    fprintf(stderr, fmt, p);\
					    q;\
					    ExitTerminfo(EXIT_FAILURE);\
					}

#define ret_error1(rc, fmt, p, q)	ret_error(rc, "'%s': " fmt, p, q)

#define ret_error0(rc, msg)		if (errret) {\
					    *errret = rc;\
					    returnCode(SETUP_FAIL);\
					} else {\
					    fprintf(stderr, msg);\
					    ExitTerminfo(EXIT_FAILURE);\
					}

/* lib_tstp.c */
#if USE_SIGWINCH
extern NCURSES_EXPORT(int) _nc_handle_sigwinch(SCREEN *);
#else
#define _nc_handle_sigwinch(a) /* nothing */
#endif

/* lib_wacs.c */
#if USE_WIDEC_SUPPORT
extern NCURSES_EXPORT(void) _nc_init_wacs(void);
#endif

typedef struct {
    char *s_head;	/* beginning of the string (may be null) */
    char *s_tail;	/* end of the string (may be null) */
    size_t s_size;	/* current remaining size available */
    size_t s_init;	/* total size available */
} string_desc;

/* strings.c */
extern NCURSES_EXPORT(string_desc *) _nc_str_init (string_desc *, char *, size_t);
extern NCURSES_EXPORT(string_desc *) _nc_str_null (string_desc *, size_t);
extern NCURSES_EXPORT(string_desc *) _nc_str_copy (string_desc *, const string_desc *);
extern NCURSES_EXPORT(bool) _nc_safe_strcat (string_desc *, const char *);
extern NCURSES_EXPORT(bool) _nc_safe_strcpy (string_desc *, const char *);

#if !HAVE_STRSTR
#define strstr _nc_strstr
extern NCURSES_EXPORT(char *) _nc_strstr (const char *, const char *);
#endif

/* safe_sprintf.c */
extern NCURSES_EXPORT(char *) _nc_printf_string (const char *, va_list);

/* tries.c */
extern NCURSES_EXPORT(int) _nc_add_to_try (TRIES **, const char *, unsigned);
extern NCURSES_EXPORT(char *) _nc_expand_try (TRIES *, unsigned, int *, size_t);
extern NCURSES_EXPORT(int) _nc_remove_key (TRIES **, unsigned);
extern NCURSES_EXPORT(int) _nc_remove_string (TRIES **, const char *);

/* elsewhere ... */
extern NCURSES_EXPORT(SCREEN *) _nc_screen_of (WINDOW *);
extern NCURSES_EXPORT(TERMINAL*) _nc_get_cur_term (void);
extern NCURSES_EXPORT(WINDOW *) _nc_makenew (int, int, int, int, int);
extern NCURSES_EXPORT(char *) _nc_trace_buf (int, size_t);
extern NCURSES_EXPORT(char *) _nc_trace_bufcat (int, const char *);
extern NCURSES_EXPORT(char *) _nc_tracechar (SCREEN *, int);
extern NCURSES_EXPORT(char *) _nc_tracemouse (SCREEN *, MEVENT const *);
extern NCURSES_EXPORT(char *) _nc_trace_mmask_t (SCREEN *, mmask_t);
extern NCURSES_EXPORT(int) _nc_access (const char *, int);
extern NCURSES_EXPORT(int) _nc_baudrate (int);
extern NCURSES_EXPORT(int) _nc_freewin (WINDOW *);
extern NCURSES_EXPORT(int) _nc_getenv_num (const char *);
extern NCURSES_EXPORT(int) _nc_keypad (SCREEN *, int);
extern NCURSES_EXPORT(int) _nc_ospeed (int);
extern NCURSES_EXPORT(int) _nc_outch (int);
extern NCURSES_EXPORT(int) _nc_putchar (int);
extern NCURSES_EXPORT(int) _nc_putp(const char *, const char *);
extern NCURSES_EXPORT(int) _nc_putp_flush(const char *, const char *);
extern NCURSES_EXPORT(int) _nc_read_termcap_entry (const char *const, TERMTYPE2 *const);
extern NCURSES_EXPORT(int) _nc_setup_tinfo(const char *, TERMTYPE2 *);
extern NCURSES_EXPORT(int) _nc_setupscreen (int, int, FILE *, int, int);
extern NCURSES_EXPORT(int) _nc_timed_wait (SCREEN *, int, int, int * EVENTLIST_2nd(_nc_eventlist *));
extern NCURSES_EXPORT(int) _nc_trans_string (char *, const char *);
extern NCURSES_EXPORT(void) _nc_init_termtype (TERMTYPE2 *const);
extern NCURSES_EXPORT(void) _nc_do_color (int, int, int, NCURSES_OUTC);
extern NCURSES_EXPORT(void) _nc_flush (void);
extern NCURSES_EXPORT(void) _nc_free_entry (ENTRY *, TERMTYPE2 *);
extern NCURSES_EXPORT(void) _nc_freeall (void);
extern NCURSES_EXPORT(void) _nc_hash_map (void);
extern NCURSES_EXPORT(void) _nc_init_keytry (SCREEN *);
extern NCURSES_EXPORT(void) _nc_keep_tic_dir (const char *);
extern NCURSES_EXPORT(void) _nc_make_oldhash (int i);
extern NCURSES_EXPORT(void) _nc_reset_input (FILE *, char *);
extern NCURSES_EXPORT(void) _nc_scroll_oldhash (int n, int top, int bot);
extern NCURSES_EXPORT(void) _nc_scroll_optimize (void);
extern NCURSES_EXPORT(void) _nc_set_buffer (FILE *, int);
extern NCURSES_EXPORT(void) _nc_setenv_num (const char *, int);
extern NCURSES_EXPORT(void) _nc_signal_handler (int);
extern NCURSES_EXPORT(void) _nc_synchook (WINDOW *);
extern NCURSES_EXPORT(void) _nc_trace_tries (TRIES *);

#if NCURSES_EXT_NUMBERS
extern NCURSES_EXPORT(const TERMTYPE2 *) _nc_fallback2 (const char *);
#else
#define _nc_fallback2(tp) _nc_fallback(tp)
#endif

extern NCURSES_EXPORT(void) _nc_copy_termtype(TERMTYPE *, const TERMTYPE *);

#if NCURSES_EXT_NUMBERS
extern NCURSES_EXPORT(void) _nc_copy_termtype2 (TERMTYPE2 *, const TERMTYPE2 *);
extern NCURSES_EXPORT(void) _nc_export_termtype2(TERMTYPE *, const TERMTYPE2 *);
#else
#define _nc_copy_termtype2(dst,src) _nc_copy_termtype((dst),(src))
#define _nc_export_termtype2(dst,src) /* nothing */
#define _nc_free_termtype2(t) _nc_free_termtype(t)
/* also... */
#define _nc_read_entry2 _nc_read_entry
#endif

#if NO_LEAKS
extern NCURSES_EXPORT(void) _nc_alloc_entry_leaks(void);
extern NCURSES_EXPORT(void) _nc_captoinfo_leaks(void);
extern NCURSES_EXPORT(void) _nc_codes_leaks(void);
extern NCURSES_EXPORT(void) _nc_comp_captab_leaks(void);
extern NCURSES_EXPORT(void) _nc_comp_error_leaks(void);
extern NCURSES_EXPORT(void) _nc_comp_scan_leaks(void);
extern NCURSES_EXPORT(void) _nc_comp_userdefs_leaks(void);
extern NCURSES_EXPORT(void) _nc_db_iterator_leaks(void);
extern NCURSES_EXPORT(void) _nc_keyname_leaks(void);
extern NCURSES_EXPORT(void) _nc_names_leaks(void);
extern NCURSES_EXPORT(void) _nc_tgetent_leak(const TERMINAL *);
extern NCURSES_EXPORT(void) _nc_tgetent_leaks(void);
#endif

#if !(defined(USE_TERMLIB) || defined(USE_BUILD_CC))
extern NCURSES_EXPORT(NCURSES_CH_T) _nc_render (WINDOW *, NCURSES_CH_T);
extern NCURSES_EXPORT(int) _nc_waddch_nosync (WINDOW *, const NCURSES_CH_T);
extern NCURSES_EXPORT(void) _nc_scroll_window (WINDOW *, int const, int const, int const, NCURSES_CH_T);
#endif

#if USE_WIDEC_SUPPORT
extern NCURSES_EXPORT(int) _nc_insert_wch(WINDOW *, const cchar_t *);
#endif

#if USE_WIDEC_SUPPORT && !(defined(USE_TERMLIB) || defined(USE_BUILD_CC))
extern NCURSES_EXPORT(size_t) _nc_wcrtomb (char *, wchar_t, mbstate_t *);
#endif

#if USE_SIZECHANGE
extern NCURSES_EXPORT(void) _nc_update_screensize (SCREEN *);
#endif

#if HAVE_RESIZETERM
extern NCURSES_EXPORT(void) _nc_resize_margins (WINDOW *);
#else
#define _nc_resize_margins(wp) /* nothing */
#endif

#ifdef NCURSES_WGETCH_EVENTS
extern NCURSES_EXPORT(int) _nc_eventlist_timeout(_nc_eventlist *);
#else
#define wgetch_events(win, evl) wgetch(win)
#define wgetnstr_events(win, str, maxlen, evl) wgetnstr(win, str, maxlen)
#endif

/*
 * Wide-character macros to hide some platform-differences.
 */
#if USE_WIDEC_SUPPORT

#if defined(_NC_WINDOWS) && !defined(_NC_MSC) && !defined(EXP_WIN32_DRIVER)
/*
 * MinGW has wide-character functions, but they do not work correctly.
 */
extern int __MINGW_NOTHROW _nc_wctomb(char *, wchar_t);
#define wctomb(s,wc) _nc_wctomb(s,wc)
#define wcrtomb(s,wc,n) _nc_wctomb(s,wc)

extern int __MINGW_NOTHROW _nc_mbtowc(wchar_t *, const char *, size_t);
#define mbtowc(pwc,s,n) _nc_mbtowc(pwc,s,n)

extern int __MINGW_NOTHROW _nc_mblen(const char *, size_t);
#define mblen(s,n) _nc_mblen(s, n)

#endif /* _NC_WINDOWS && !_NC_MSC */

#if defined(_NC_WINDOWS) || defined(_NC_MINGW)
/* see wcwidth.c */
extern NCURSES_EXPORT(int) mk_wcwidth(wchar_t);
#define wcwidth(ucs) _nc_wcwidth(ucs)
#endif

#if HAVE_MBTOWC && HAVE_MBLEN
#define reset_mbytes(state) IGNORE_RC(mblen(NULL, (size_t) 0)), IGNORE_RC(mbtowc(NULL, NULL, (size_t) 0))
#define count_mbytes(buffer,length,state) mblen(buffer,length)
#define check_mbytes(wch,buffer,length,state) \
	(int) mbtowc(&(wch), buffer, length)
#define state_unused
#elif HAVE_MBRTOWC && HAVE_MBRLEN
#define reset_mbytes(state) init_mb(state)
#define count_mbytes(buffer,length,state) mbrlen(buffer,length,&(state))
#define check_mbytes(wch,buffer,length,state) \
	(int) mbrtowc(&(wch), buffer, length, &(state))
#else
make an error
#endif

#endif /* USE_WIDEC_SUPPORT */

/*
 * Not everyone has vsscanf(), but we'd like to use it for scanw().
 */
#if !HAVE_VSSCANF
extern int vsscanf(const char *str, const char *format, va_list __arg);
#endif

/* scroll indices */
extern NCURSES_EXPORT_VAR(int *) _nc_oldnums;

#define USE_SETBUF_0 0

#define NC_OUTPUT(sp) ((sp != 0 && sp->_ofp != 0) ? sp->_ofp : stdout)

/*
 * On systems with a broken linker, define 'SP' as a function to force the
 * linker to pull in the data-only module with 'SP'.
 */
#define _nc_alloc_screen_sp() typeCalloc(SCREEN, 1)

#if BROKEN_LINKER
#define SP _nc_screen()
extern NCURSES_EXPORT(SCREEN *) _nc_screen (void);
extern NCURSES_EXPORT(int)      _nc_alloc_screen (void);
extern NCURSES_EXPORT(void)     _nc_set_screen (SCREEN *);
#define CURRENT_SCREEN          _nc_screen()
#else
/* current screen is private data; avoid possible linking conflicts too */
extern NCURSES_EXPORT_VAR(SCREEN *) SP;
#define CURRENT_SCREEN SP
#define _nc_alloc_screen()      ((SP = _nc_alloc_screen_sp()) != 0)
#define _nc_set_screen(sp)      SP = sp
#endif

#if NCURSES_SP_FUNCS
#define CURRENT_SCREEN_PRE      (IsPreScreen(CURRENT_SCREEN) ? CURRENT_SCREEN : new_prescr())
#else
#define CURRENT_SCREEN_PRE      CURRENT_SCREEN
#endif

/*
 * We don't want to use the lines or columns capabilities internally, because
 * if the application is running multiple screens under X, it is quite possible
 * they could all have type xterm but have different sizes!  So...
 */
#define screen_lines(sp)        (sp)->_lines
#define screen_columns(sp)      (sp)->_columns

extern NCURSES_EXPORT(int) _nc_slk_initialize (WINDOW *, int);
extern NCURSES_EXPORT(int) _nc_format_slks (NCURSES_SP_DCLx int _c);

/*
 * Some constants related to SLK's
 */
#define MAX_SKEY_OLD	   8	/* count of soft keys */
#define MAX_SKEY_LEN_OLD   8	/* max length of soft key text */
#define MAX_SKEY_PC       12    /* This is what most PC's have */
#define MAX_SKEY_LEN_PC    5

/* Macro to check whether or not we use a standard format */
#define SLK_STDFMT(fmt) (fmt < 3)
/* Macro to determine height of label window */
#define SLK_LINES(fmt)  (SLK_STDFMT(fmt) ? 1 : ((fmt) - 2))

#define MAX_SKEY(fmt)     (SLK_STDFMT(fmt)? MAX_SKEY_OLD : MAX_SKEY_PC)
#define MAX_SKEY_LEN(fmt) (SLK_STDFMT(fmt)? MAX_SKEY_LEN_OLD : MAX_SKEY_LEN_PC)

/*
 * Common error messages
 */
#define MSG_NO_MEMORY "Out of memory"
#define MSG_NO_INPUTS "Premature EOF"

extern NCURSES_EXPORT(int) _nc_set_tty_mode(TTY *);
extern NCURSES_EXPORT(int) _nc_get_tty_mode(TTY *);

/* timed_wait flag definitions */
#define TW_NONE    0
#define TW_INPUT   1
#define TW_MOUSE   2
#define TW_ANY     (TW_INPUT | TW_MOUSE)
#define TW_EVENT   4

#define SetSafeOutcWrapper(outc)	    \
    SCREEN* sp = CURRENT_SCREEN;            \
    struct screen outc_wrapper;		    \
    if (sp==0) {                            \
	sp = &outc_wrapper;                 \
	memset(sp,0,sizeof(struct screen)); \
	sp->_outch = _nc_outc_wrapper;      \
    }\
    sp->jump = outc

#ifdef USE_TERM_DRIVER

typedef struct _termInfo
{
    bool caninit;

    bool hascolor;
    bool initcolor;
    bool canchange;

    int  tabsize;

    int  maxcolors;
    int  maxpairs;
    int  nocolorvideo;

    int  numbuttons;
    int  numlabels;
    int  labelwidth;
    int  labelheight;

    const color_t* defaultPalette;
} TerminalInfo;

typedef struct term_driver {
    bool   isTerminfo;
    const char* (*td_name)(struct DriverTCB*);
    bool   (*td_CanHandle)(struct DriverTCB*, const char*, int*);
    void   (*td_init)(struct DriverTCB*);
    void   (*td_release)(struct DriverTCB*);
    int    (*td_size)(struct DriverTCB*, int* Line, int *Cols);
    int    (*td_sgmode)(struct DriverTCB*, int setFlag, TTY*);
    chtype (*td_conattr)(struct DriverTCB*);
    int    (*td_hwcur)(struct DriverTCB*, int yold, int xold, int y, int x);
    int    (*td_mode)(struct DriverTCB*, int progFlag, int defFlag);
    bool   (*td_rescol)(struct DriverTCB*);
    bool   (*td_rescolors)(struct DriverTCB*);
    void   (*td_color)(struct DriverTCB*, int fore, int color, int(*)(SCREEN*, int));
    int    (*td_doBeepOrFlash)(struct DriverTCB*, int);
    void   (*td_initpair)(struct DriverTCB*, int, int, int);
    void   (*td_initcolor)(struct DriverTCB*, int, int, int, int);
    void   (*td_docolor)(struct DriverTCB*, int, int, int, int(*)(SCREEN*, int));
    void   (*td_initmouse)(struct DriverTCB*);
    int    (*td_testmouse)(struct DriverTCB*, int EVENTLIST_2nd(_nc_eventlist*));
    void   (*td_setfilter)(struct DriverTCB*);
    void   (*td_hwlabel)(struct DriverTCB*, int, char*);
    void   (*td_hwlabelOnOff)(struct DriverTCB*, int);
    int    (*td_update)(struct DriverTCB*);
    int    (*td_defaultcolors)(struct DriverTCB*, int, int);
    int    (*td_print)(struct DriverTCB*, char*, int);
    int    (*td_getsize)(struct DriverTCB*, int*, int*);
    int    (*td_setsize)(struct DriverTCB*, int, int);
    void   (*td_initacs)(struct DriverTCB*, chtype*, chtype*);
    void   (*td_scinit)(SCREEN *);
    void   (*td_scexit)(SCREEN *);
    int    (*td_twait)(struct DriverTCB*, int, int, int* EVENTLIST_2nd(_nc_eventlist*));
    int    (*td_read)(struct DriverTCB*, int*);
    int    (*td_nap)(struct DriverTCB*, int);
    int    (*td_kpad)(struct DriverTCB*, int);
    int    (*td_kyOk)(struct DriverTCB*, int, int);
    bool   (*td_kyExist)(struct DriverTCB*, int);
    int    (*td_cursorSet)(struct DriverTCB*, int);
} TERM_DRIVER;

typedef struct DriverTCB
{
    TERMINAL      term;   /* needs to be the first Element !!! */
    TERM_DRIVER*  drv;    /* The driver for that Terminal */
    SCREEN*       csp;    /* The screen that owns that Terminal */
    TerminalInfo  info;   /* Driver independent core capabilities of the Terminal */
    void*         prop;   /* Driver dependent property storage to be used by the Driver */
    long          magic;
} TERMINAL_CONTROL_BLOCK;

#define NCDRV_MAGIC(id) (0x47110000 | (id&0xffff))
#define NCDRV_TINFO      0x01
#define NCDRV_WINCONSOLE 0x02

#define TCBOf(sp)    ((TERMINAL_CONTROL_BLOCK*)(TerminalOf(sp)))
#define InfoOf(sp)   TCBOf(sp)->info
#define CallDriver(sp,method)                        TCBOf(sp)->drv->method(TCBOf(sp))
#define CallDriver_1(sp,method,arg1)                 TCBOf(sp)->drv->method(TCBOf(sp),arg1)
#define CallDriver_2(sp,method,arg1,arg2)            TCBOf(sp)->drv->method(TCBOf(sp),arg1,arg2)
#define CallDriver_3(sp,method,arg1,arg2,arg3)       TCBOf(sp)->drv->method(TCBOf(sp),arg1,arg2,arg3)
#define CallDriver_4(sp,method,arg1,arg2,arg3,arg4)  TCBOf(sp)->drv->method(TCBOf(sp),arg1,arg2,arg3,arg4)

extern NCURSES_EXPORT_VAR(const color_t*) _nc_cga_palette;
extern NCURSES_EXPORT_VAR(const color_t*) _nc_hls_palette;

extern NCURSES_EXPORT(int)      _nc_get_driver(TERMINAL_CONTROL_BLOCK*, const char*, int*);
extern NCURSES_EXPORT(void)     _nc_get_screensize_ex(SCREEN *, TERMINAL *, int *, int *);
#endif /* USE_TERM_DRIVER */

/*
 * Entrypoints which are actually provided in the terminal driver, which would
 * be an sp-name otherwise.
 */
#ifdef USE_TERM_DRIVER
#define TINFO_HAS_KEY           _nc_tinfo_has_key
#define TINFO_DOUPDATE          _nc_tinfo_doupdate
#define TINFO_MVCUR             _nc_tinfo_mvcur
extern NCURSES_EXPORT(int)      TINFO_HAS_KEY(SCREEN*, int);
extern NCURSES_EXPORT(int)      TINFO_DOUPDATE(SCREEN *);
extern NCURSES_EXPORT(int)      TINFO_MVCUR(SCREEN*, int, int, int, int);
#else
#define TINFO_HAS_KEY           NCURSES_SP_NAME(has_key)
#define TINFO_DOUPDATE          NCURSES_SP_NAME(doupdate)
#define TINFO_MVCUR             NCURSES_SP_NAME(_nc_mvcur)
#endif

#if defined(EXP_WIN32_DRIVER)
#include <nc_win32.h>
#endif

/*
 * Entrypoints using an extra parameter with the terminal driver.
 */
#ifdef USE_TERM_DRIVER
extern NCURSES_EXPORT(void)   _nc_get_screensize(SCREEN *, TERMINAL *, int *, int *);
extern NCURSES_EXPORT(int)    _nc_setupterm_ex(TERMINAL **, const char *, int , int *, int);
#define TINFO_GET_SIZE(sp, tp, lp, cp) \
	_nc_get_screensize(sp, tp, lp, cp)
#define TINFO_SET_CURTERM(sp, tp) \
	NCURSES_SP_NAME(set_curterm)(sp, tp)
#define TINFO_SETUP_TERM(tpp, name, fd, err, reuse) \
	_nc_setupterm_ex(tpp, name, fd, err, reuse)
#else /* !USE_TERM_DRIVER */
extern NCURSES_EXPORT(void)   _nc_get_screensize(SCREEN *, int *, int *);
#define TINFO_GET_SIZE(sp, tp, lp, cp) \
	_nc_get_screensize(sp, lp, cp)
#define TINFO_SET_CURTERM(sp, tp) \
	set_curterm(tp)
#define TINFO_SETUP_TERM(tpp, name, fd, err, reuse) \
	_nc_setupterm(name, fd, err, reuse)
#endif /* !USE_TERM_DRIVER */

#ifdef EXP_WIN32_DRIVER
extern NCURSES_EXPORT_VAR(TERM_DRIVER) _nc_TINFO_DRIVER;
#else
#ifdef USE_TERM_DRIVER
#if defined(USE_WIN32CON_DRIVER)
#include <nc_mingw.h>
extern NCURSES_EXPORT_VAR(TERM_DRIVER) _nc_WIN_DRIVER;
extern NCURSES_EXPORT(int)  _nc_mingw_isatty(int fd);
extern NCURSES_EXPORT(int)  _nc_mingw_isconsole(int fd);
extern NCURSES_EXPORT(int) _nc_mingw_console_read(
    SCREEN *sp,
    HANDLE  fd,
    int *buf);
extern NCURSES_EXPORT(int) _nc_mingw_testmouse(
    SCREEN * sp,
    HANDLE fd,
    int delay EVENTLIST_2nd(_nc_eventlist*));
#else
#endif
extern NCURSES_EXPORT_VAR(TERM_DRIVER) _nc_TINFO_DRIVER;
#endif /* USE_TERM_DRIVER */
#endif /* EXP_WIN32_DRIVER */

#if defined(USE_TERM_DRIVER) && defined(EXP_WIN32_DRIVER)
#define NC_ISATTY(fd) (0 != _nc_console_isatty(fd))
#elif defined(USE_TERM_DRIVER) && defined(USE_WIN32CON_DRIVER)
#define NC_ISATTY(fd) _nc_mingw_isatty(fd)
#else
#define NC_ISATTY(fd) isatty(fd)
#endif

#ifdef USE_TERM_DRIVER
#  define IsTermInfo(sp)       ((TCBOf(sp) != 0) && ((TCBOf(sp)->drv->isTerminfo)))
#  define HasTInfoTerminal(sp) ((0 != TerminalOf(sp)) && IsTermInfo(sp))
#  if defined(EXP_WIN32_DRIVER)
#    define IsTermInfoOnConsole(sp) (IsTermInfo(sp) && _nc_console_test(TerminalOf(sp)->Filedes))
#  elif defined(USE_WIN32CON_DRIVER)
#    define IsTermInfoOnConsole(sp) (IsTermInfo(sp) && _nc_mingw_isconsole(TerminalOf(sp)->Filedes))
#  else
#    define IsTermInfoOnConsole(sp) FALSE
#  endif
#else
#  define IsTermInfo(sp)       TRUE
#  define HasTInfoTerminal(sp) (0 != TerminalOf(sp))
#  if defined(EXP_WIN32_DRIVER)
#    define IsTermInfoOnConsole(sp) _nc_console_test(TerminalOf(sp)->Filedes)
#  else
#    define IsTermInfoOnConsole(sp) FALSE
#  endif
#endif

#define IsValidTIScreen(sp)  (HasTInfoTerminal(sp))

/*
 * Exported entrypoints beyond the published API
 */
#if NCURSES_SP_FUNCS
extern NCURSES_EXPORT(WINDOW *) _nc_curscr_of(SCREEN*);
extern NCURSES_EXPORT(WINDOW *) _nc_newscr_of(SCREEN*);
extern NCURSES_EXPORT(WINDOW *) _nc_stdscr_of(SCREEN*);
extern NCURSES_EXPORT(int)      _nc_outc_wrapper(SCREEN*,int);

#if USE_REENTRANT
extern NCURSES_EXPORT(int)       NCURSES_SP_NAME(_nc_TABSIZE)(SCREEN*);
extern NCURSES_EXPORT(char *)    NCURSES_SP_NAME(longname)(SCREEN*);
#endif

#if NCURSES_EXT_FUNCS
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_set_tabsize)(SCREEN*, int);
#endif

/*
 * We put the safe versions of various calls here as they are not published
 * part of the API up to now
 */
extern NCURSES_EXPORT(TERMINAL*) NCURSES_SP_NAME(_nc_get_cur_term) (SCREEN *sp);
extern NCURSES_EXPORT(WINDOW *) NCURSES_SP_NAME(_nc_makenew) (SCREEN*, int, int, int, int, int);
extern NCURSES_EXPORT(bool)     NCURSES_SP_NAME(_nc_reset_colors)(SCREEN*);
extern NCURSES_EXPORT(char *)   NCURSES_SP_NAME(_nc_printf_string)(SCREEN*, const char *, va_list);
extern NCURSES_EXPORT(chtype)   NCURSES_SP_NAME(_nc_acs_char)(SCREEN*,int);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_get_tty_mode)(SCREEN*,TTY*);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_mcprint)(SCREEN*,char*, int);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_msec_cost)(SCREEN*, const char *, int);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_mvcur)(SCREEN*, int, int, int, int);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_outch)(SCREEN*, int);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_putchar)(SCREEN*, int);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_putp)(SCREEN*, const char *, const char*);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_putp_flush)(SCREEN*, const char *, const char *);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_ripoffline)(SCREEN*, int, int (*)(WINDOW *,int));
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_scrolln)(SCREEN*, int, int, int, int);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_set_tty_mode)(SCREEN*, TTY*);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_setupscreen)(SCREEN**, int, int, FILE *, int, int);
extern NCURSES_EXPORT(int)      NCURSES_SP_NAME(_nc_tgetent)(SCREEN*,char*,const char *);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_do_color)(SCREEN*, int, int, int, NCURSES_SP_OUTC);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_do_xmc_glitch)(SCREEN*, attr_t);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_flush)(SCREEN*);
extern GCC_NORETURN NCURSES_EXPORT(void) NCURSES_SP_NAME(_nc_free_and_exit)(SCREEN*, int);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_freeall)(SCREEN*);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_hash_map)(SCREEN*);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_init_acs)(SCREEN*);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_make_oldhash)(SCREEN*, int i);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_mvcur_init)(SCREEN*);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_mvcur_resume)(SCREEN*);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_mvcur_wrap)(SCREEN*);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_screen_init)(SCREEN*);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_screen_resume)(SCREEN*);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_screen_wrap)(SCREEN*);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_scroll_oldhash)(SCREEN*, int n, int top, int bot);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_scroll_optimize)(SCREEN*);
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_set_buffer)(SCREEN*, FILE *, int);

extern NCURSES_EXPORT(void)     _nc_cookie_init(SCREEN *sp);

#if defined(TRACE) || defined(SCROLLDEBUG) || defined(HASHDEBUG)
extern NCURSES_EXPORT(void)     NCURSES_SP_NAME(_nc_linedump)(SCREEN*);
#endif

#if USE_WIDEC_SUPPORT
extern NCURSES_EXPORT(wchar_t *) NCURSES_SP_NAME(_nc_wunctrl)(SCREEN*, cchar_t *);
#endif

#endif /* NCURSES_SP_FUNCS */

#if NCURSES_SP_FUNCS

#define safe_keyname NCURSES_SP_NAME(keyname)
#define safe_unctrl  NCURSES_SP_NAME(unctrl)
#define safe_ungetch NCURSES_SP_NAME(ungetch)

#else

#define safe_keyname _nc_keyname
#define safe_unctrl  _nc_unctrl
#define safe_ungetch _nc_ungetch

extern NCURSES_EXPORT(NCURSES_CONST char *) _nc_keyname (SCREEN *, int);
extern NCURSES_EXPORT(int) _nc_ungetch (SCREEN *, int);
extern NCURSES_EXPORT(NCURSES_CONST char *) _nc_unctrl (SCREEN *, chtype);

#endif

#ifdef EXP_XTERM_1005
extern NCURSES_EXPORT(int) _nc_conv_to_utf8(unsigned char *, unsigned, unsigned);
extern NCURSES_EXPORT(int) _nc_conv_to_utf32(unsigned *, const char *, unsigned);
#endif

#ifdef __cplusplus
}
#endif

/* *INDENT-ON* */

#endif /* CURSES_PRIV_H */
