/* Portability cruft.  Include after config.h and sys/types.h.
   Copyright 1996, 1998, 1999, 2000 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
   02111-1307, USA.  */

#undef PARAMS
#if defined (__STDC__) && __STDC__
# ifndef _PTR_T
# define _PTR_T
  typedef void * ptr_t;
# endif
# define PARAMS(x) x
#else
# ifndef _PTR_T
# define _PTR_T
  typedef char * ptr_t;
# endif
# define PARAMS(x) ()
#endif

#ifdef HAVE_UNISTD_H
# include <fcntl.h>
# include <unistd.h>
#else
# define O_RDONLY 0
# define SEEK_SET 0
# define SEEK_CUR 1
int open(), read(), close();
#endif

#include <errno.h>
#ifndef errno
extern int errno;
#endif

#ifndef HAVE_STRERROR
extern int sys_nerr;
extern char *sys_errlist[];
# define strerror(E) (0 <= (E) && (E) < sys_nerr ? _(sys_errlist[E]) : _("Unknown system error"))
#endif

/* Some operating systems treat text and binary files differently.  */
#ifdef __BEOS__
# undef O_BINARY /* BeOS 5 has O_BINARY and O_TEXT, but they have no effect. */
#endif
#ifdef HAVE_DOS_FILE_CONTENTS
# include <io.h>
# ifdef HAVE_SETMODE
#  define SET_BINARY(fd)  setmode (fd, O_BINARY)
# else
#  define SET_BINARY(fd)  _setmode (fd, O_BINARY)
# endif
#endif

#ifdef HAVE_DOS_FILE_NAMES
# define IS_SLASH(c) ((c) == '/' || (c) == '\\')
# define FILESYSTEM_PREFIX_LEN(f) ((f)[0] && (f)[1] == ':' ? 2 : 0)
#endif

#ifndef IS_SLASH
# define IS_SLASH(c) ((c) == '/')
#endif

#ifndef FILESYSTEM_PREFIX_LEN
# define FILESYSTEM_PREFIX_LEN(f) 0
#endif

int isdir PARAMS ((char const *));

#ifdef HAVE_DIR_EACCES_BUG
# ifdef EISDIR
#  define is_EISDIR(e, f) \
     ((e) == EISDIR \
      || ((e) == EACCES && isdir (f) && ((e) = EISDIR, 1)))
# else
#  define is_EISDIR(e, f) ((e) == EACCES && isdir (f))
# endif
#endif

#ifndef is_EISDIR
# ifdef EISDIR
#  define is_EISDIR(e, f) ((e) == EISDIR)
# else
#  define is_EISDIR(e, f) 0
# endif
#endif

#if STAT_MACROS_BROKEN
# undef S_ISDIR
# undef S_ISREG
#endif
#if !defined(S_ISDIR) && defined(S_IFDIR)
# define S_ISDIR(Mode) (((Mode) & S_IFMT) == S_IFDIR)
#endif
#if !defined(S_ISREG) && defined(S_IFREG)
# define S_ISREG(Mode) (((Mode) & S_IFMT) == S_IFREG)
#endif

#ifdef STDC_HEADERS
# include <stdlib.h>
#else
char *getenv ();
ptr_t malloc(), realloc(), calloc();
void free();
#endif

#if __STDC__
# include <stddef.h>
#endif
#ifdef STDC_HEADERS
# include <limits.h>
#endif
#ifndef CHAR_BIT
# define CHAR_BIT 8
#endif
/* The extra casts work around common compiler bugs.  */
#define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))
#define TYPE_MINIMUM(t) ((t) (TYPE_SIGNED (t) \
			      ? ~ (t) 0 << (sizeof (t) * CHAR_BIT - 1) \
			      : (t) 0))
#define TYPE_MAXIMUM(t) ((t) (~ (t) 0 - TYPE_MINIMUM (t)))
#ifndef CHAR_MAX
# define CHAR_MAX TYPE_MAXIMUM (char)
#endif
#ifndef INT_MAX
# define INT_MAX TYPE_MAXIMUM (int)
#endif
#ifndef UCHAR_MAX
# define UCHAR_MAX TYPE_MAXIMUM (unsigned char)
#endif

#if !defined(STDC_HEADERS) && defined(HAVE_STRING_H) && defined(HAVE_MEMORY_H)
# include <memory.h>
#endif
#if defined(STDC_HEADERS) || defined(HAVE_STRING_H)
# include <string.h>
#else
# include <strings.h>
# undef strchr
# define strchr index
# undef strrchr
# define strrchr rindex
# undef memcpy
# define memcpy(d, s, n) bcopy (s, d, n)
#endif
#ifndef HAVE_MEMCHR
ptr_t memchr();
#endif
#if ! defined HAVE_MEMMOVE && ! defined memmove
# define memmove(d, s, n) bcopy (s, d, n)
#endif

#include <ctype.h>

#ifndef isgraph
# define isgraph(C) (isprint(C) && !isspace(C))
#endif

#if defined (STDC_HEADERS) || (!defined (isascii) && !defined (HAVE_ISASCII))
# define IN_CTYPE_DOMAIN(c) 1
#else
# define IN_CTYPE_DOMAIN(c) isascii(c)
#endif

#define ISALPHA(C)	(IN_CTYPE_DOMAIN (C) && isalpha (C))
#define ISUPPER(C)	(IN_CTYPE_DOMAIN (C) && isupper (C))
#define ISLOWER(C)	(IN_CTYPE_DOMAIN (C) && islower (C))
#define ISDIGIT(C)	(IN_CTYPE_DOMAIN (C) && isdigit (C))
#define ISXDIGIT(C)	(IN_CTYPE_DOMAIN (C) && isxdigit (C))
#define ISSPACE(C)	(IN_CTYPE_DOMAIN (C) && isspace (C))
#define ISPUNCT(C)	(IN_CTYPE_DOMAIN (C) && ispunct (C))
#define ISALNUM(C)	(IN_CTYPE_DOMAIN (C) && isalnum (C))
#define ISPRINT(C)	(IN_CTYPE_DOMAIN (C) && isprint (C))
#define ISGRAPH(C)	(IN_CTYPE_DOMAIN (C) && isgraph (C))
#define ISCNTRL(C)	(IN_CTYPE_DOMAIN (C) && iscntrl (C))

#define TOLOWER(C) (ISUPPER(C) ? tolower(C) : (C))

#if ENABLE_NLS
# include <libintl.h>
# define _(String) gettext (String)
#else
# define _(String) String
#endif
#define N_(String) String

#if HAVE_SETLOCALE
# include <locale.h>
#endif

#ifndef initialize_main
#define initialize_main(argcp, argvp)
#endif
