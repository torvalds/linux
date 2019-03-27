/* xmalloc.c -- malloc with out of memory checking
   Copyright (C) 1990-1999, 2000 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <sys/types.h>

#if STDC_HEADERS
# include <stdlib.h>
#else
void *calloc ();
void *malloc ();
void *realloc ();
void free ();
#endif

#if ENABLE_NLS
# include <libintl.h>
# define _(Text) gettext (Text)
#else
# define textdomain(Domain)
# define _(Text) Text
#endif
#define N_(Text) Text

#include "error.h"
#include "xalloc.h"

#ifndef EXIT_FAILURE
# define EXIT_FAILURE 1
#endif

#ifndef HAVE_DONE_WORKING_MALLOC_CHECK
"you must run the autoconf test for a properly working malloc -- see malloc.m4"
#endif

#ifndef HAVE_DONE_WORKING_REALLOC_CHECK
"you must run the autoconf test for a properly working realloc --see realloc.m4"
#endif

/* Exit value when the requested amount of memory is not available.
   The caller may set it to some other value.  */
int xalloc_exit_failure = EXIT_FAILURE;

/* If non NULL, call this function when memory is exhausted. */
void (*xalloc_fail_func) PARAMS ((void)) = 0;

/* If XALLOC_FAIL_FUNC is NULL, or does return, display this message
   before exiting when memory is exhausted.  Goes through gettext. */
char const xalloc_msg_memory_exhausted[] = N_("memory exhausted");

void
xalloc_die (void)
{
  if (xalloc_fail_func)
    (*xalloc_fail_func) ();
  error (xalloc_exit_failure, 0, "%s", _(xalloc_msg_memory_exhausted));
  /* The `noreturn' cannot be given to error, since it may return if
     its first argument is 0.  To help compilers understand the
     xalloc_die does terminate, call exit. */
  exit (EXIT_FAILURE);
}

/* Allocate N bytes of memory dynamically, with error checking.  */

void *
xmalloc (size_t n)
{
  void *p;

  p = malloc (n);
  if (p == 0)
    xalloc_die ();
  return p;
}

/* Change the size of an allocated block of memory P to N bytes,
   with error checking.  */

void *
xrealloc (void *p, size_t n)
{
  p = realloc (p, n);
  if (p == 0)
    xalloc_die ();
  return p;
}

/* Allocate memory for N elements of S bytes, with error checking.  */

void *
xcalloc (size_t n, size_t s)
{
  void *p;

  p = calloc (n, s);
  if (p == 0)
    xalloc_die ();
  return p;
}
