/* Copyright (C) 1991, 1994, 1995, 1996, 2002 Free Software Foundation, Inc.
   This file based on putenv.c in the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/*

@deftypefn Supplemental int putenv (const char *@var{string})

Uses @code{setenv} or @code{unsetenv} to put @var{string} into
the environment or remove it.  If @var{string} is of the form
@samp{name=value} the string is added; if no @samp{=} is present the
name is unset/removed.

@end deftypefn

*/

#if defined (_AIX) && !defined (__GNUC__)
 #pragma alloca
#endif

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include "ansidecl.h"

#define putenv libiberty_putenv

#if HAVE_STDLIB_H
# include <stdlib.h>
#endif
#if HAVE_STRING_H
# include <string.h>
#endif

#ifdef HAVE_ALLOCA_H
# include <alloca.h>
#else
# ifndef alloca
#  ifdef __GNUC__
#   define alloca __builtin_alloca
#  else
extern char *alloca ();
#  endif /* __GNUC__ */
# endif /* alloca */
#endif /* HAVE_ALLOCA_H */

#undef putenv

/* Below this point, it's verbatim code from the glibc-2.0 implementation */


/* Put STRING, which is of the form "NAME=VALUE", in the environment.  */
int
putenv (const char *string)
{
  const char *const name_end = strchr (string, '=');

  if (name_end)
    {
      char *name = (char *) alloca (name_end - string + 1);
      memcpy (name, string, name_end - string);
      name[name_end - string] = '\0';
      return setenv (name, name_end + 1, 1);
    }

  unsetenv (string);
  return 0;
}
