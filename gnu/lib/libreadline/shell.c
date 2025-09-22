/* shell.c -- readline utility functions that are normally provided by
	      bash when readline is linked as part of the shell. */

/* Copyright (C) 1997 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library, a library for
   reading lines of text with interactive input and history editing.

   The GNU Readline Library is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2, or
   (at your option) any later version.

   The GNU Readline Library is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   59 Temple Place, Suite 330, Boston, MA 02111 USA. */
#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <sys/types.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#if defined (HAVE_STRING_H)
#  include <string.h>
#else
#  include <strings.h>
#endif /* !HAVE_STRING_H */

#if defined (HAVE_LIMITS_H)
#  include <limits.h>
#endif

#include <fcntl.h>
#include <pwd.h>

#include <stdio.h>

#include "rlstdc.h"
#include "rlshell.h"
#include "xmalloc.h"

#if !defined (HAVE_GETPW_DECLS)
extern struct passwd *getpwuid PARAMS((uid_t));
#endif /* !HAVE_GETPW_DECLS */

#ifndef NULL
#  define NULL 0
#endif

#ifndef CHAR_BIT
#  define CHAR_BIT 8
#endif

/* Nonzero if the integer type T is signed.  */
#define TYPE_SIGNED(t) (! ((t) 0 < (t) -1))

/* Bound on length of the string representing an integer value of type T.
   Subtract one for the sign bit if T is signed;
   302 / 1000 is log10 (2) rounded up;
   add one for integer division truncation;
   add one more for a minus sign if t is signed.  */
#define INT_STRLEN_BOUND(t) \
  ((sizeof (t) * CHAR_BIT - TYPE_SIGNED (t)) * 302 / 1000 \
   + 1 + TYPE_SIGNED (t))

/* All of these functions are resolved from bash if we are linking readline
   as part of bash. */

/* Does shell-like quoting using single quotes. */
char *
sh_single_quote (string)
     char *string;
{
  register int c;
  char *result, *r, *s;

  result = (char *)xmalloc (3 + (4 * strlen (string)));
  r = result;
  *r++ = '\'';

  for (s = string; s && (c = *s); s++)
    {
      *r++ = c;

      if (c == '\'')
	{
	  *r++ = '\\';	/* insert escaped single quote */
	  *r++ = '\'';
	  *r++ = '\'';	/* start new quoted string */
	}
    }

  *r++ = '\'';
  *r = '\0';

  return (result);
}

/* Set the environment variables LINES and COLUMNS to lines and cols,
   respectively. */
void
sh_set_lines_and_columns (lines, cols)
     int lines, cols;
{
  char *b;

#if defined (HAVE_PUTENV)
  if (asprintf (&b, "LINES=%d", lines) == -1)
	  memory_error_and_abort("asprintf");
  putenv (b);
  if (asprintf (&b, "COLUMNS=%d", cols) == -1)
	  memory_error_and_abort("asprintf");
  putenv (b);
#else /* !HAVE_PUTENV */
#  if defined (HAVE_SETENV)
  if (asprintf(&b, "%d", lines) == -1)
	  memory_error_and_abort("asprintf");
  setenv ("LINES", b, 1);
  if (asprintf (&b, "%d", cols) == -1)
	  memory_error_and_abort("asprintf");
  setenv ("COLUMNS", b, 1);
#  endif /* HAVE_SETENV */
#endif /* !HAVE_PUTENV */
}

char *
sh_get_env_value (varname)
     const char *varname;
{
  return ((char *)getenv (varname));
}

char *
sh_get_home_dir ()
{
  char *home_dir;
  struct passwd *entry;

  home_dir = (char *)NULL;
  entry = getpwuid (getuid ());
  if (entry)
    home_dir = entry->pw_dir;
  return (home_dir);
}

#if !defined (O_NDELAY)
#  if defined (FNDELAY)
#    define O_NDELAY FNDELAY
#  endif
#endif

int
sh_unset_nodelay_mode (fd)
     int fd;
{
  int flags, bflags;

  if ((flags = fcntl (fd, F_GETFL, 0)) < 0)
    return -1;

  bflags = 0;

#ifdef O_NONBLOCK
  bflags |= O_NONBLOCK;
#endif

#ifdef O_NDELAY
  bflags |= O_NDELAY;
#endif

  if (flags & bflags)
    {
      flags &= ~bflags;
      return (fcntl (fd, F_SETFL, flags));
    }

  return 0;
}
