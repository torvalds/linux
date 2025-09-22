/****************************************************************************
 * Copyright 2020,2021 Thomas E. Dickey                                     *
 * Copyright 2012-2013,2016 Free Software Foundation, Inc.                  *
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
 *  Author: Thomas E. Dickey                        2012                    *
 ****************************************************************************/

#ifndef STRING_HACKS_H
#define STRING_HACKS_H 1

#include <ncurses_cfg.h>

#if HAVE_BSD_STRING_H
#include <bsd/string.h>
#endif

/*
 * $Id: nc_string.h,v 1.1 2023/10/17 09:52:08 nicm Exp $
 *
 * String-hacks.  Use these macros to stifle warnings on (presumably) correct
 * uses of strcat, strcpy and sprintf.
 *
 * By the way -
 * A fundamental limitation of the interfaces (and frequent issue in bug
 * reports using these functions) is that sizes are passed as unsigned values
 * (with associated sign-extension problems), limiting their effectiveness
 * when checking for buffer overflow.
 */

#ifdef __cplusplus
#define NCURSES_VOID		/* nothing */
#else
#define NCURSES_VOID (void)
#endif

#if USE_STRING_HACKS && HAVE_STRLCAT
#define _nc_STRCAT(d,s,n)	NCURSES_VOID strlcat((d),(s),NCURSES_CAST(size_t,n))
#define _nc_STRNCAT(d,s,m,n)	NCURSES_VOID strlcat((d),(s),NCURSES_CAST(size_t,m))
#else
#define _nc_STRCAT(d,s,n)	NCURSES_VOID strcat((d),(s))
#define _nc_STRNCAT(d,s,m,n)	NCURSES_VOID strncat((d),(s),(n))
#endif

#if USE_STRING_HACKS && HAVE_STRLCPY
#define _nc_STRCPY(d,s,n)	NCURSES_VOID strlcpy((d),(s),NCURSES_CAST(size_t,n))
#define _nc_STRNCPY(d,s,n)	NCURSES_VOID strlcpy((d),(s),NCURSES_CAST(size_t,n))
#else
#define _nc_STRCPY(d,s,n)	NCURSES_VOID strcpy((d),(s))
#define _nc_STRNCPY(d,s,n)	NCURSES_VOID strncpy((d),(s),(n))
#endif

#if USE_STRING_HACKS && HAVE_SNPRINTF
#ifdef __cplusplus
#define _nc_SPRINTF             NCURSES_VOID snprintf
#else
#define _nc_SPRINTF             NCURSES_VOID (snprintf)
#endif
#define _nc_SLIMIT(n)           NCURSES_CAST(size_t,n),
#else
#define _nc_SPRINTF             NCURSES_VOID sprintf
#define _nc_SLIMIT(n)		/* nothing */
#endif

#endif /* STRING_HACKS_H */
