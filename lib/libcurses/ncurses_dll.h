/****************************************************************************
 * Copyright 2018-2020,2023 Thomas E. Dickey                                *
 * Copyright 2009,2014 Free Software Foundation, Inc.                       *
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
/* $Id: ncurses_dll.h,v 1.1 2023/10/17 09:52:08 nicm Exp $ */

#ifndef NCURSES_DLL_H_incl
#define NCURSES_DLL_H_incl 1

/*
 * MinGW gcc (unlike MSYS2 and Cygwin) should define _WIN32 and possibly _WIN64.
 */
#if defined(__MINGW64__)

#ifndef _WIN64
#define _WIN64 1
#endif

#elif defined(__MINGW32__)

#ifndef _WIN32
#define _WIN32 1
#endif

/* 2014-08-02 workaround for broken MinGW compiler.
 * Oddly, only TRACE is mapped to trace - the other -D's are okay.
 * suggest TDM as an alternative.
 */
#if (__GNUC__ == 4) && (__GNUC_MINOR__ == 8)

#ifdef trace
#undef trace
#define TRACE
#endif

#endif	/* broken compiler */

#endif	/* MingW */

/*
 * For reentrant code, we map the various global variables into SCREEN by
 * using functions to access them.
 */
#define NCURSES_PUBLIC_VAR(name) _nc_##name

#if defined(BUILDING_NCURSES)
# define NCURSES_IMPEXP NCURSES_EXPORT_GENERAL_EXPORT
#else
# define NCURSES_IMPEXP NCURSES_EXPORT_GENERAL_IMPORT
#endif

#define NCURSES_WRAPPED_VAR(type,name) extern NCURSES_IMPEXP type NCURSES_PUBLIC_VAR(name)(void)

#define NCURSES_EXPORT(type) NCURSES_IMPEXP type NCURSES_API
#define NCURSES_EXPORT_VAR(type) NCURSES_IMPEXP type

/*
 * These symbols hide dllimport/dllexport, for compilers which care about it.
 */
#if defined(__CYGWIN__) || (defined(_WIN32) || defined(_WIN64))
# if defined(NCURSES_STATIC)	/* "static" here only implies "not-a-DLL" */
#   define NCURSES_EXPORT_GENERAL_IMPORT
#   define NCURSES_EXPORT_GENERAL_EXPORT
# else
#   define NCURSES_EXPORT_GENERAL_IMPORT __declspec(dllimport)
#   define NCURSES_EXPORT_GENERAL_EXPORT __declspec(dllexport)
# endif
# define NCURSES_API __cdecl
#else
# define NCURSES_EXPORT_GENERAL_IMPORT
# if (__GNUC__ >= 4) && !defined(__cplusplus)
#   define NCURSES_EXPORT_GENERAL_EXPORT __attribute__((visibility ("default")))
# else
#   define NCURSES_EXPORT_GENERAL_EXPORT
# endif
# define NCURSES_API /* FIXME: __attribute__ ((cdecl)) is only available on x86 */
#endif

#endif /* NCURSES_DLL_H_incl */
