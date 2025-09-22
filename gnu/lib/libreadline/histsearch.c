/* histsearch.c -- searching the history list. */

/* Copyright (C) 1989, 1992 Free Software Foundation, Inc.

   This file contains the GNU History Library (the Library), a set of
   routines for managing the text of previously typed lines.

   The Library is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The Library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   59 Temple Place, Suite 330, Boston, MA 02111 USA. */

#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <stdio.h>
#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#if defined (HAVE_UNISTD_H)
#  ifdef _MINIX
#    include <sys/types.h>
#  endif
#  include <unistd.h>
#endif

#include "history.h"
#include "histlib.h"

/* The list of alternate characters that can delimit a history search
   string. */
char *history_search_delimiter_chars = (char *)NULL;

static int history_search_internal PARAMS((const char *, int, int));

/* Search the history for STRING, starting at history_offset.
   If DIRECTION < 0, then the search is through previous entries, else
   through subsequent.  If ANCHORED is non-zero, the string must
   appear at the beginning of a history line, otherwise, the string
   may appear anywhere in the line.  If the string is found, then
   current_history () is the history entry, and the value of this
   function is the offset in the line of that history entry that the
   string was found in.  Otherwise, nothing is changed, and a -1 is
   returned. */

static int
history_search_internal (string, direction, anchored)
     const char *string;
     int direction, anchored;
{
  register int i, reverse;
  register char *line;
  register int line_index;
  int string_len;
  HIST_ENTRY **the_history;	/* local */

  i = history_offset;
  reverse = (direction < 0);

  /* Take care of trivial cases first. */
  if (string == 0 || *string == '\0')
    return (-1);

  if (!history_length || ((i == history_length) && !reverse))
    return (-1);

  if (reverse && (i == history_length))
    i--;

#define NEXT_LINE() do { if (reverse) i--; else i++; } while (0)

  the_history = history_list ();
  string_len = strlen (string);
  while (1)
    {
      /* Search each line in the history list for STRING. */

      /* At limit for direction? */
      if ((reverse && i < 0) || (!reverse && i == history_length))
	return (-1);

      line = the_history[i]->line;
      line_index = strlen (line);

      /* If STRING is longer than line, no match. */
      if (string_len > line_index)
	{
	  NEXT_LINE ();
	  continue;
	}

      /* Handle anchored searches first. */
      if (anchored == ANCHORED_SEARCH)
	{
	  if (STREQN (string, line, string_len))
	    {
	      history_offset = i;
	      return (0);
	    }

	  NEXT_LINE ();
	  continue;
	}

      /* Do substring search. */
      if (reverse)
	{
	  line_index -= string_len;

	  while (line_index >= 0)
	    {
	      if (STREQN (string, line + line_index, string_len))
		{
		  history_offset = i;
		  return (line_index);
		}
	      line_index--;
	    }
	}
      else
	{
	  register int limit;

	  limit = line_index - string_len + 1;
	  line_index = 0;

	  while (line_index < limit)
	    {
	      if (STREQN (string, line + line_index, string_len))
		{
		  history_offset = i;
		  return (line_index);
		}
	      line_index++;
	    }
	}
      NEXT_LINE ();
    }
}

/* Do a non-anchored search for STRING through the history in DIRECTION. */
int
history_search (string, direction)
     const char *string;
     int direction;
{
  return (history_search_internal (string, direction, NON_ANCHORED_SEARCH));
}

/* Do an anchored search for string through the history in DIRECTION. */
int
history_search_prefix (string, direction)
     const char *string;
     int direction;
{
  return (history_search_internal (string, direction, ANCHORED_SEARCH));
}

/* Search for STRING in the history list.  DIR is < 0 for searching
   backwards.  POS is an absolute index into the history list at
   which point to begin searching. */
int
history_search_pos (string, dir, pos)
     const char *string;
     int dir, pos;
{
  int ret, old;

  old = where_history ();
  history_set_pos (pos);
  if (history_search (string, dir) == -1)
    {
      history_set_pos (old);
      return (-1);
    }
  ret = where_history ();
  history_set_pos (old);
  return ret;
}
