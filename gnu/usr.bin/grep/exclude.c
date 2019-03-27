/* exclude.c -- exclude file names
   Copyright 1992, 1993, 1994, 1997, 1999, 2000 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by Paul Eggert <eggert@twinsun.com>  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#ifndef errno
extern int errno;
#endif
#include <exclude.h>
#include <fnmatch.h>
#include <stdio.h>
#include <sys/types.h>

void *xmalloc PARAMS ((size_t));
void *xrealloc PARAMS ((void *, size_t));

/* Keep track of excluded file name patterns.  */

struct exclude
  {
    char const **exclude;
    int exclude_alloc;
    int exclude_count;
  };

struct exclude *
new_exclude (void)
{
  struct exclude *ex = (struct exclude *) xmalloc (sizeof (struct exclude));
  ex->exclude_count = 0;
  ex->exclude_alloc = 64;
  ex->exclude = (char const **) xmalloc (ex->exclude_alloc * sizeof (char *));
  return ex;
}

int
excluded_filename (struct exclude const *ex, char const *f, int options)
{
  char const * const *exclude = ex->exclude;
  int exclude_count = ex->exclude_count;
  int i;

  for (i = 0;  i < exclude_count;  i++)
    if (fnmatch (exclude[i], f, options) == 0)
      return 1;

  return 0;
}

void
add_exclude (struct exclude *ex, char const *pattern)
{
  if (ex->exclude_alloc <= ex->exclude_count)
    ex->exclude = (char const **) xrealloc (ex->exclude,
					    ((ex->exclude_alloc *= 2)
					     * sizeof (char *)));

  ex->exclude[ex->exclude_count++] = pattern;
}

int
add_exclude_file (void (*add_func) PARAMS ((struct exclude *, char const *)),
		  struct exclude *ex, char const *filename, char line_end)
{
  int use_stdin = filename[0] == '-' && !filename[1];
  FILE *in;
  char *buf;
  char *p;
  char const *pattern;
  char const *lim;
  size_t buf_alloc = 1024;
  size_t buf_count = 0;
  int c;
  int e = 0;

  if (use_stdin)
    in = stdin;
  else if (! (in = fopen (filename, "r")))
    return -1;

  buf = xmalloc (buf_alloc);

  while ((c = getc (in)) != EOF)
    {
      buf[buf_count++] = c;
      if (buf_count == buf_alloc)
	buf = xrealloc (buf, buf_alloc *= 2);
    }

  buf = xrealloc (buf, buf_count + 1);

  if (ferror (in))
    e = errno;

  if (!use_stdin && fclose (in) != 0)
    e = errno;

  for (pattern = p = buf, lim = buf + buf_count;  p <= lim;  p++)
    if (p < lim ? *p == line_end : buf < p && p[-1])
      {
	*p = '\0';
	(*add_func) (ex, pattern);
	pattern = p + 1;
      }

  errno = e;
  return e ? -1 : 0;
}
