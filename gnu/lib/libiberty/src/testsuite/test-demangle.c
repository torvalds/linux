/* Demangler test program,
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Zack Weinberg <zack@codesourcery.com

   This file is part of GNU libiberty.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. 
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ansidecl.h"
#include <stdio.h>
#include "libiberty.h"
#include "demangle.h"
#include <string.h>
#include <stdlib.h>

struct line
{
  size_t alloced;
  char *data;
};

static unsigned int lineno;

/* Safely read a single line of arbitrary length from standard input.  */

#define LINELEN 80

static void
get_line(buf)
     struct line *buf;
{
  char *data = buf->data;
  size_t alloc = buf->alloced;
  size_t count = 0;
  int c;

  if (data == 0)
    {
      data = xmalloc (LINELEN);
      alloc = LINELEN;
    }

  /* Skip comment lines.  */
  while ((c = getchar()) == '#')
    {
      while ((c = getchar()) != EOF && c != '\n');
      lineno++;
    }

  /* c is the first character on the line, and it's not a comment
     line: copy this line into the buffer and return.  */
  while (c != EOF && c != '\n')
    {
      if (count + 1 >= alloc)
	{
	  alloc *= 2;
	  data = xrealloc (data, alloc);
	}
      data[count++] = c;
      c = getchar();
    }
  lineno++;
  data[count] = '\0';

  buf->data = data;
  buf->alloced = alloc;
}

/* The tester operates on a data file consisting of triples of lines:
   format switch
   input to be demangled
   expected output

   The format switch is expected to be either the empty string, a
   line of the form --format=<name>, or just <name> by itself.  */

#define FORMATS "--format="
#define FORMATL (sizeof FORMATS - 1)

int
main(argc, argv)
     int argc;
     char **argv;
{
  enum demangling_styles style;
  struct line format;
  struct line input;
  struct line expect;
  char *fstyle;
  char *result;
  int failures = 0;
  int tests = 0;

  if (argc > 1)
    {
      fprintf (stderr, "usage: %s < test-set\n", argv[0]);
      return 2;
    }

  format.data = 0;
  input.data = 0;
  expect.data = 0;

  for (;;)
    {
      get_line (&format);
      if (feof (stdin))
	break;

      get_line (&input);
      get_line (&expect);

      tests++;

      fstyle = format.data;
      if (!strncmp (fstyle, FORMATS, FORMATL))
	fstyle += FORMATL;

      if (fstyle[0] == '\0')
	style = auto_demangling;
      else
	style = cplus_demangle_name_to_style (fstyle);

      if (style == unknown_demangling)
	{
	  printf ("FAIL at line %d: unknown demangling style %s\n",
		  lineno, fstyle);
	  failures++;
	  continue;
	}

      cplus_demangle_set_style (style);

      result = cplus_demangle (input.data,
			       DMGL_PARAMS|DMGL_ANSI|DMGL_VERBOSE|DMGL_TYPES);

      if (result
	  ? strcmp (result, expect.data)
	  : strcmp (input.data, expect.data))
	{
	  printf ("\
FAIL at line %d, style %s:\n\
in:  %s\n\
out: %s\n\
exp: %s\n",
		   lineno, fstyle,
		   input.data,
		   result,
		   expect.data);
	  failures++;
	}
      free (result);
    }

  free (format.data);
  free (input.data);
  free (expect.data);

  printf ("%s: %d tests, %d failures\n", argv[0], tests, failures);
  return failures ? 1 : 0;
}
