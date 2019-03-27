/*
 * config.h for libdivsufsort
 * Copyright (c) 2003-2008 Yuta Mori All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * $FreeBSD$
 */

#ifndef _CONFIG_H
#define _CONFIG_H 1

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Define to the version of this package. **/
#define PROJECT_VERSION_FULL "2.0.1-14-g5f60d6f"

/** Define to 1 if you have the header files. **/
#define HAVE_INTTYPES_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_MEMORY_H 1
#define HAVE_SYS_TYPES_H 1

/** for WinIO **/
/* #undef HAVE_IO_H */
/* #undef HAVE_FCNTL_H */
/* #undef HAVE__SETMODE */
/* #undef HAVE_SETMODE */
/* #undef HAVE__FILENO */
/* #undef HAVE_FOPEN_S */
/* #undef HAVE__O_BINARY */
#ifndef HAVE__SETMODE
# if HAVE_SETMODE
#  define _setmode setmode
#  define HAVE__SETMODE 1
# endif
# if HAVE__SETMODE && !HAVE__O_BINARY
#  define _O_BINARY 0
#  define HAVE__O_BINARY 1
# endif
#endif

/** for inline **/
#ifndef INLINE
# define INLINE inline
#endif

/** for VC++ warning **/
#ifdef _MSC_VER
#pragma warning(disable: 4127)
#endif


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* _CONFIG_H */
