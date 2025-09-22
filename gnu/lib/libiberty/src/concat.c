/* Concatenate variable number of strings.
   Copyright (C) 1991, 1994, 2001 Free Software Foundation, Inc.
   Written by Fred Fish @ Cygnus Support

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */


/*

@deftypefn Extension char* concat (const char *@var{s1}, const char *@var{s2}, @dots{}, @code{NULL})

Concatenate zero or more of strings and return the result in freshly
@code{xmalloc}ed memory.  Returns @code{NULL} if insufficient memory is
available.  The argument list is terminated by the first @code{NULL}
pointer encountered.  Pointers to empty strings are ignored.

@end deftypefn

NOTES

	This function uses xmalloc() which is expected to be a front end
	function to malloc() that deals with low memory situations.  In
	typical use, if malloc() returns NULL then xmalloc() diverts to an
	error handler routine which never returns, and thus xmalloc will
	never return a NULL pointer.  If the client application wishes to
	deal with low memory situations itself, it should supply an xmalloc
	that just directly invokes malloc and blindly returns whatever
	malloc returns.

*/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ansidecl.h"
#include "libiberty.h"
#include <sys/types.h>		/* size_t */

#include <stdarg.h>

# if HAVE_STRING_H
#  include <string.h>
# else
#  if HAVE_STRINGS_H
#   include <strings.h>
#  endif
# endif

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif

static inline unsigned long vconcat_length (const char *, va_list);
static inline unsigned long
vconcat_length (const char *first, va_list args)
{
  unsigned long length = 0;
  const char *arg;

  for (arg = first; arg ; arg = va_arg (args, const char *))
    length += strlen (arg);

  return length;
}

static inline char *
vconcat_copy (char *dst, const char *first, va_list args)
{
  char *end = dst;
  const char *arg;

  for (arg = first; arg ; arg = va_arg (args, const char *))
    {
      unsigned long length = strlen (arg);
      memcpy (end, arg, length);
      end += length;
    }
  *end = '\000';

  return dst;
}

/* @undocumented concat_length */

unsigned long
concat_length (const char *first, ...)
{
  unsigned long length;

  VA_OPEN (args, first);
  VA_FIXEDARG (args, const char *, first);
  length = vconcat_length (first, args);
  VA_CLOSE (args);

  return length;
}

/* @undocumented concat_copy */

char *
concat_copy (char *dst, const char *first, ...)
{
  char *save_dst;

  VA_OPEN (args, first);
  VA_FIXEDARG (args, char *, dst);
  VA_FIXEDARG (args, const char *, first);
  vconcat_copy (dst, first, args);
  save_dst = dst; /* With K&R C, dst goes out of scope here.  */
  VA_CLOSE (args);

  return save_dst;
}

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
char *libiberty_concat_ptr;
#ifdef __cplusplus
}
#endif /* __cplusplus */

/* @undocumented concat_copy2 */

char *
concat_copy2 (const char *first, ...)
{
  VA_OPEN (args, first);
  VA_FIXEDARG (args, const char *, first);
  vconcat_copy (libiberty_concat_ptr, first, args);
  VA_CLOSE (args);

  return libiberty_concat_ptr;
}

char *
concat (const char *first, ...)
{
  char *newstr;

  /* First compute the size of the result and get sufficient memory.  */
  VA_OPEN (args, first);
  VA_FIXEDARG (args, const char *, first);
  newstr = XNEWVEC (char, vconcat_length (first, args) + 1);
  VA_CLOSE (args);

  /* Now copy the individual pieces to the result string. */
  VA_OPEN (args, first);
  VA_FIXEDARG (args, const char *, first);
  vconcat_copy (newstr, first, args);
  VA_CLOSE (args);

  return newstr;
}

/*

@deftypefn Extension char* reconcat (char *@var{optr}, const char *@var{s1}, @dots{}, @code{NULL})

Same as @code{concat}, except that if @var{optr} is not @code{NULL} it
is freed after the string is created.  This is intended to be useful
when you're extending an existing string or building up a string in a
loop:

@example
  str = reconcat (str, "pre-", str, NULL);
@end example

@end deftypefn

*/

char *
reconcat (char *optr, const char *first, ...)
{
  char *newstr;

  /* First compute the size of the result and get sufficient memory.  */
  VA_OPEN (args, first);
  VA_FIXEDARG (args, char *, optr);
  VA_FIXEDARG (args, const char *, first);
  newstr = XNEWVEC (char, vconcat_length (first, args) + 1);
  VA_CLOSE (args);

  /* Now copy the individual pieces to the result string. */
  VA_OPEN (args, first);
  VA_FIXEDARG (args, char *, optr);
  VA_FIXEDARG (args, const char *, first);
  vconcat_copy (newstr, first, args);
  if (optr) /* Done before VA_CLOSE so optr stays in scope for K&R C.  */
    free (optr);
  VA_CLOSE (args);

  return newstr;
}

#ifdef MAIN
#define NULLP (char *)0

/* Simple little test driver. */

#include <stdio.h>

int
main (void)
{
  printf ("\"\" = \"%s\"\n", concat (NULLP));
  printf ("\"a\" = \"%s\"\n", concat ("a", NULLP));
  printf ("\"ab\" = \"%s\"\n", concat ("a", "b", NULLP));
  printf ("\"abc\" = \"%s\"\n", concat ("a", "b", "c", NULLP));
  printf ("\"abcd\" = \"%s\"\n", concat ("ab", "cd", NULLP));
  printf ("\"abcde\" = \"%s\"\n", concat ("ab", "c", "de", NULLP));
  printf ("\"abcdef\" = \"%s\"\n", concat ("", "a", "", "bcd", "ef", NULLP));
  return 0;
}

#endif
