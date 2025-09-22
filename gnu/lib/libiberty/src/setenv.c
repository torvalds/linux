/* Copyright (C) 1992, 1995, 1996, 1997, 2002 Free Software Foundation, Inc.
   This file based on setenv.c in the GNU C Library.

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

@deftypefn Supplemental int setenv (const char *@var{name}, const char *@var{value}, int @var{overwrite})
@deftypefnx Supplemental void unsetenv (const char *@var{name})

@code{setenv} adds @var{name} to the environment with value
@var{value}.  If the name was already present in the environment,
the new value will be stored only if @var{overwrite} is nonzero.
The companion @code{unsetenv} function removes @var{name} from the
environment.  This implementation is not safe for multithreaded code.

@end deftypefn

*/

#if HAVE_CONFIG_H
# include <config.h>
#endif

#define setenv libiberty_setenv
#define unsetenv libiberty_unsetenv

#include "ansidecl.h"
#include <sys/types.h> /* For `size_t' */
#include <stdio.h>     /* For `NULL' */

#include <errno.h>
#if !defined(errno) && !defined(HAVE_ERRNO_DECL)
extern int errno;
#endif
#define __set_errno(ev) ((errno) = (ev))

#if HAVE_STDLIB_H
# include <stdlib.h>
#endif
#if HAVE_STRING_H
# include <string.h>
#endif
#if HAVE_UNISTD_H
# include <unistd.h>
#endif

#define __environ	environ
#ifndef HAVE_ENVIRON_DECL
extern char **environ;
#endif

#undef setenv
#undef unsetenv

/* LOCK and UNLOCK are defined as no-ops.  This makes the libiberty
 * implementation MT-Unsafe. */
#define LOCK
#define UNLOCK

/* Below this point, it's verbatim code from the glibc-2.0 implementation */

/* If this variable is not a null pointer we allocated the current
   environment.  */
static char **last_environ;


int
setenv (const char *name, const char *value, int replace)
{
  register char **ep = 0;
  register size_t size;
  const size_t namelen = strlen (name);
  const size_t vallen = strlen (value) + 1;

  LOCK;

  size = 0;
  if (__environ != NULL)
    {
      for (ep = __environ; *ep != NULL; ++ep)
	if (!strncmp (*ep, name, namelen) && (*ep)[namelen] == '=')
	  break;
	else
	  ++size;
    }

  if (__environ == NULL || *ep == NULL)
    {
      char **new_environ;
      if (__environ == last_environ && __environ != NULL)
	/* We allocated this space; we can extend it.  */
	new_environ = (char **) realloc (last_environ,
					 (size + 2) * sizeof (char *));
      else
	new_environ = (char **) malloc ((size + 2) * sizeof (char *));

      if (new_environ == NULL)
	{
	  UNLOCK;
	  return -1;
	}

      new_environ[size] = (char *) malloc (namelen + 1 + vallen);
      if (new_environ[size] == NULL)
	{
	  free ((char *) new_environ);
	  __set_errno (ENOMEM);
	  UNLOCK;
	  return -1;
	}

      if (__environ != last_environ)
	memcpy ((char *) new_environ, (char *) __environ,
		size * sizeof (char *));

      memcpy (new_environ[size], name, namelen);
      new_environ[size][namelen] = '=';
      memcpy (&new_environ[size][namelen + 1], value, vallen);

      new_environ[size + 1] = NULL;

      last_environ = __environ = new_environ;
    }
  else if (replace)
    {
      size_t len = strlen (*ep);
      if (len + 1 < namelen + 1 + vallen)
	{
	  /* The existing string is too short; malloc a new one.  */
	  char *new_string = (char *) malloc (namelen + 1 + vallen);
	  if (new_string == NULL)
	    {
	      UNLOCK;
	      return -1;
	    }
	  *ep = new_string;
	}
      memcpy (*ep, name, namelen);
      (*ep)[namelen] = '=';
      memcpy (&(*ep)[namelen + 1], value, vallen);
    }

  UNLOCK;

  return 0;
}

void
unsetenv (const char *name)
{
  const size_t len = strlen (name);
  char **ep;

  LOCK;

  for (ep = __environ; *ep; ++ep)
    if (!strncmp (*ep, name, len) && (*ep)[len] == '=')
      {
	/* Found it.  Remove this pointer by moving later ones back.  */
	char **dp = ep;
	do
	  dp[0] = dp[1];
	while (*dp++);
	/* Continue the loop in case NAME appears again.  */
      }

  UNLOCK;
}
