/****************************************************************************
 * Copyright 2021,2023 Thomas E. Dickey                                     *
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

/* $Id: nc_access.h,v 1.1 2023/10/17 09:52:08 nicm Exp $ */

#ifndef NC_ACCESS_included
#define NC_ACCESS_included 1
/* *INDENT-OFF* */

#include <ncurses_cfg.h>
#include <curses.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Turn off the 'use_terminfo_vars()' symbol to limit access to environment
 * variables when running with privileges.
 */
#if defined(USE_ROOT_ENVIRON) && defined(USE_SETUID_ENVIRON)
#define use_terminfo_vars() 1
#else
#define use_terminfo_vars() _nc_env_access()
#endif

extern NCURSES_EXPORT(int) _nc_env_access (void);

/*
 * Turn off this symbol to limit access to files when running setuid.
 */
#ifdef USE_ROOT_ACCESS

#define safe_fopen(name,mode)       fopen(name,mode)
#define safe_open2(name,flags)      open(name,flags)
#define safe_open3(name,flags,mode) open(name,flags,mode)

#else

#define safe_fopen(name,mode)       _nc_safe_fopen(name,mode)
#define safe_open2(name,flags)      _nc_safe_open3(name,flags,0)
#define safe_open3(name,flags,mode) _nc_safe_open3(name,flags,mode)
extern NCURSES_EXPORT(FILE *)       _nc_safe_fopen (const char *, const char *);
extern NCURSES_EXPORT(int)          _nc_safe_open3 (const char *, int, mode_t);

#endif

#ifdef __cplusplus
}
#endif

/* *INDENT-ON* */

#endif /* NC_ACCESS_included */
