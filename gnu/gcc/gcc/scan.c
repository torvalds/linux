/* Utility functions for scan-decls and fix-header programs.
   Copyright (C) 1993, 1994, 1998, 2002, 2003 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "scan.h"

int lineno = 1;
int source_lineno = 1;
sstring source_filename;

void
make_sstring_space (sstring *str, int count)
{
  int cur_pos = str->ptr - str->base;
  int cur_size = str->limit - str->base;
  int new_size = cur_pos + count + 100;

  if (new_size <= cur_size)
    return;

  str->base = xrealloc (str->base, new_size);
  str->ptr = str->base + cur_size;
  str->limit = str->base + new_size;
}

void
sstring_append (sstring *dst, sstring *src)
{
  char *d, *s;
  int count = SSTRING_LENGTH (src);

  MAKE_SSTRING_SPACE (dst, count + 1);
  d = dst->ptr;
  s = src->base;
  while (--count >= 0) *d++ = *s++;
  dst->ptr = d;
  *d = 0;
}

int
scan_ident (FILE *fp, sstring *s, int c)
{
  s->ptr = s->base;
  if (ISIDST (c))
    {
      for (;;)
	{
	  SSTRING_PUT (s, c);
	  c = getc (fp);
	  if (c == EOF || ! ISIDNUM (c))
	    break;
	}
    }
  MAKE_SSTRING_SPACE (s, 1);
  *s->ptr = 0;
  return c;
}

int
scan_string (FILE *fp, sstring *s, int init)
{
  int c;

  for (;;)
    {
      c = getc (fp);
      if (c == EOF || c == '\n')
	break;
      if (c == init)
	{
	  c = getc (fp);
	  break;
	}
      if (c == '\\')
	{
	  c = getc (fp);
	  if (c == EOF)
	    break;
	  if (c == '\n')
	    continue;
	}
      SSTRING_PUT (s, c);
    }
  MAKE_SSTRING_SPACE (s, 1);
  *s->ptr = 0;
  return c;
}

/* Skip horizontal white spaces (spaces, tabs, and C-style comments).  */

int
skip_spaces (FILE *fp, int c)
{
  for (;;)
    {
      if (c == ' ' || c == '\t')
	c = getc (fp);
      else if (c == '/')
	{
	  c = getc (fp);
	  if (c != '*')
	    {
	      ungetc (c, fp);
	      return '/';
	    }
	  c = getc (fp);
	  for (;;)
	    {
	      if (c == EOF)
		return EOF;
	      else if (c != '*')
		{
		  if (c == '\n')
		    source_lineno++, lineno++;
		  c = getc (fp);
		}
	      else if ((c = getc (fp)) == '/')
		return getc (fp);
	    }
	}
      else
	break;
    }
  return c;
}

int
read_upto (FILE *fp, sstring *str, int delim)
{
  int ch;

  for (;;)
    {
      ch = getc (fp);
      if (ch == EOF || ch == delim)
	break;
      SSTRING_PUT (str, ch);
    }
  MAKE_SSTRING_SPACE (str, 1);
  *str->ptr = 0;
  return ch;
}

int
get_token (FILE *fp, sstring *s)
{
  int c;

  s->ptr = s->base;
 retry:
  c = ' ';
  c = skip_spaces (fp, c);
  if (c == '\n')
    {
      source_lineno++;
      lineno++;
      goto retry;
    }
  if (c == '#')
    {
      c = get_token (fp, s);
      if (c == INT_TOKEN)
	{
	  source_lineno = atoi (s->base) - 1; /* '\n' will add 1 */
	  get_token (fp, &source_filename);
	}
      for (;;)
	{
	  c = getc (fp);
	  if (c == EOF)
	    return EOF;
	  if (c == '\n')
	    {
	    source_lineno++;
	    lineno++;
	    goto retry;
	    }
	}
    }
  if (c == EOF)
    return EOF;
  if (ISDIGIT (c))
    {
      do
	{
	  SSTRING_PUT (s, c);
	  c = getc (fp);
	} while (c != EOF && ISDIGIT (c));
      ungetc (c, fp);
      c = INT_TOKEN;
      goto done;
    }
  if (ISIDST (c))
    {
      c = scan_ident (fp, s, c);
      ungetc (c, fp);
      return IDENTIFIER_TOKEN;
    }
  if (c == '\'' || c == '"')
    {
      c = scan_string (fp, s, c);
      ungetc (c, fp);
      return c == '\'' ? CHAR_TOKEN : STRING_TOKEN;
    }
  SSTRING_PUT (s, c);
 done:
  MAKE_SSTRING_SPACE (s, 1);
  *s->ptr = 0;
  return c;
}

unsigned int
hashstr (const char *str, unsigned int len)
{
  unsigned int n = len;
  unsigned int r = 0;
  const unsigned char *s = (const unsigned char *) str;

  do
    r = r * 67 + (*s++ - 113);
  while (--n);
  return r + len;
}
