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

/*
 * Common type definitions and macros for new_pair.c, lib_color.c
 *
 * $Id: new_pair.h,v 1.1 2023/10/17 09:52:08 nicm Exp $
 */

#ifndef NEW_PAIR_H
#define NEW_PAIR_H 1
/* *INDENT-OFF* */

#include <ncurses_cfg.h>
#include <ncurses_dll.h>

#include <sys/types.h>

#undef SCREEN
#define SCREEN struct screen
SCREEN;

#define LIMIT_TYPED(n,t) \
	(t)(((n) > MAX_OF_TYPE(t)) \
	    ? MAX_OF_TYPE(t) \
	    : ((n) < -MAX_OF_TYPE(t)) \
	       ? -MAX_OF_TYPE(t) \
	       : (n))

#define limit_COLOR(n) LIMIT_TYPED(n,NCURSES_COLOR_T)
#define limit_PAIRS(n) LIMIT_TYPED(n,NCURSES_PAIRS_T)

#define MAX_XCURSES_PAIR MAX_OF_TYPE(NCURSES_PAIRS_T)

#if NCURSES_EXT_COLORS
#define OPTIONAL_PAIR	GCC_UNUSED
#define get_extended_pair(opts, color_pair) \
	if ((opts) != NULL) { \
	    *(int*)(opts) = color_pair; \
	}
#define set_extended_pair(opts, color_pair) \
	if ((opts) != NULL) { \
	    color_pair = *(const int*)(opts); \
	}
#else
#define OPTIONAL_PAIR	/* nothing */
#define get_extended_pair(opts, color_pair) /* nothing */
#define set_extended_pair(opts, color_pair) \
	if ((opts) != NULL) { \
	    color_pair = -1; \
	}
#endif

#ifdef NEW_PAIR_INTERNAL

typedef enum {
    cpKEEP = -1,		/* color pair 0 */
    cpFREE = 0,			/* free for use */
    cpINIT = 1			/* initialized */
} CPMODE;

typedef struct _color_pairs
{
    int fg;
    int bg;
#if NCURSES_EXT_COLORS
    int mode;			/* tells if the entry is allocated or free */
    int prev;			/* index of previous item */
    int next;			/* index of next item */
#endif
}
colorpair_t;

#define MakeColorPair(target,f,b) target.fg = f, target.bg = b
#define isSamePair(a,b)		((a).fg == (b).fg && (a).bg == (b).bg)
#define FORE_OF(c)		(c).fg
#define BACK_OF(c)		(c).bg

/*
 * Ensure that we use color pairs only when colors have been started, and also
 * that the index is within the limits of the table which we allocated.
 */
#define ValidPair(sp,pair) \
    ((sp != 0) && (pair >= 0) && (pair < sp->_pair_limit) && sp->_coloron)

#if NCURSES_EXT_COLORS
extern NCURSES_EXPORT(void)     _nc_copy_pairs(SCREEN*, colorpair_t*, colorpair_t*, int);
extern NCURSES_EXPORT(void)     _nc_free_ordered_pairs(SCREEN*);
extern NCURSES_EXPORT(void)     _nc_reset_color_pair(SCREEN*, int, colorpair_t*);
extern NCURSES_EXPORT(void)     _nc_set_color_pair(SCREEN*, int, int);
#else
#define _nc_free_ordered_pairs(sp) /* nothing */
#define _nc_reset_color_pair(sp, pair, data) /* nothing */
#define _nc_set_color_pair(sp, pair, mode) /* nothing */
#endif

#else

typedef struct _color_pairs colorpair_t;

#endif /* NEW_PAIR_INTERNAL */

#if NO_LEAKS
extern NCURSES_EXPORT(void)     _nc_new_pair_leaks(SCREEN*);
#endif

/* *INDENT-ON* */

#endif /* NEW_PAIR_H */
