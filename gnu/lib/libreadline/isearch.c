/* **************************************************************** */
/*								    */
/*			I-Search and Searching			    */
/*								    */
/* **************************************************************** */

/* Copyright (C) 1987-2002 Free Software Foundation, Inc.

   This file contains the Readline Library (the Library), a set of
   routines for providing Emacs style line input to programs that ask
   for it.

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

#include <sys/types.h>

#include <stdio.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif

#include "rldefs.h"
#include "rlmbutil.h"

#include "readline.h"
#include "history.h"

#include "rlprivate.h"
#include "xmalloc.h"

/* Variables exported to other files in the readline library. */
char *_rl_isearch_terminators = (char *)NULL;

/* Variables imported from other files in the readline library. */
extern HIST_ENTRY *_rl_saved_line_for_history;

/* Forward declarations */
static int rl_search_history PARAMS((int, int));

/* Last line found by the current incremental search, so we don't `find'
   identical lines many times in a row. */
static char *prev_line_found;

/* Last search string and its length. */
static char *last_isearch_string;
static int last_isearch_string_len;

static char *default_isearch_terminators = "\033\012";

/* Search backwards through the history looking for a string which is typed
   interactively.  Start with the current line. */
int
rl_reverse_search_history (sign, key)
     int sign, key;
{
  return (rl_search_history (-sign, key));
}

/* Search forwards through the history looking for a string which is typed
   interactively.  Start with the current line. */
int
rl_forward_search_history (sign, key)
     int sign, key;
{
  return (rl_search_history (sign, key));
}

/* Display the current state of the search in the echo-area.
   SEARCH_STRING contains the string that is being searched for,
   DIRECTION is zero for forward, or 1 for reverse,
   WHERE is the history list number of the current line.  If it is
   -1, then this line is the starting one. */
static void
rl_display_search (search_string, reverse_p, where)
     char *search_string;
     int reverse_p, where;
{
  char *message;
  int msglen, searchlen, mlen;

  searchlen = (search_string && *search_string) ? strlen (search_string) : 0;

  mlen = searchlen + 33;
  message = (char *)xmalloc (mlen);
  msglen = 0;

#if defined (NOTDEF)
  if (where != -1)
    {
      snprintf (message, mlen, "[%d]", where + history_base);
      msglen = strlen (message);
    }
#endif /* NOTDEF */

  message[msglen++] = '(';
  message[msglen] = '\0';

  if (reverse_p)
    {
      strlcat (message, "reverse-", mlen);
    }

  strlcat (message, "i-search)`", mlen);

  if (search_string)
    {
      strlcat (message, search_string, mlen);
    }

  strlcat (message, "': ", mlen);

  rl_message ("%s", message);
  free (message);
  (*rl_redisplay_function) ();
}

/* Search through the history looking for an interactively typed string.
   This is analogous to i-search.  We start the search in the current line.
   DIRECTION is which direction to search; >= 0 means forward, < 0 means
   backwards. */
static int
rl_search_history (direction, invoking_key)
     int direction, invoking_key;
{
  /* The string that the user types in to search for. */
  char *search_string;

  /* The current length of SEARCH_STRING. */
  int search_string_index;

  /* The amount of space that SEARCH_STRING has allocated to it. */
  int search_string_size;

  /* The list of lines to search through. */
  char **lines, *allocated_line;

  /* The length of LINES. */
  int hlen;

  /* Where we get LINES from. */
  HIST_ENTRY **hlist;

  register int i;
  int orig_point, orig_mark, orig_line, last_found_line;
  int c, found, failed, sline_len;
  int n, wstart, wlen;
#if defined (HANDLE_MULTIBYTE)
  char mb[MB_LEN_MAX];
#endif

  /* The line currently being searched. */
  char *sline;

  /* Offset in that line. */
  int line_index;

  /* Non-zero if we are doing a reverse search. */
  int reverse;

  /* The list of characters which terminate the search, but are not
     subsequently executed.  If the variable isearch-terminators has
     been set, we use that value, otherwise we use ESC and C-J. */
  char *isearch_terminators;

  RL_SETSTATE(RL_STATE_ISEARCH);
  orig_point = rl_point;
  orig_mark = rl_mark;
  last_found_line = orig_line = where_history ();
  reverse = direction < 0;
  hlist = history_list ();
  allocated_line = (char *)NULL;

  isearch_terminators = _rl_isearch_terminators ? _rl_isearch_terminators
						: default_isearch_terminators;

  /* Create an arrary of pointers to the lines that we want to search. */
  rl_maybe_replace_line ();
  i = 0;
  if (hlist)
    for (i = 0; hlist[i]; i++);

  /* Allocate space for this many lines, +1 for the current input line,
     and remember those lines. */
  lines = (char **)xmalloc ((1 + (hlen = i)) * sizeof (char *));
  for (i = 0; i < hlen; i++)
    lines[i] = hlist[i]->line;

  if (_rl_saved_line_for_history)
    lines[i] = _rl_saved_line_for_history->line;
  else
    {
      /* Keep track of this so we can free it. */
      allocated_line = strdup(rl_line_buffer);
      if (allocated_line == NULL)
	      memory_error_and_abort("strdup");
      lines[i] = allocated_line;
    }

  hlen++;

  /* The line where we start the search. */
  i = orig_line;

  rl_save_prompt ();

  /* Initialize search parameters. */
  search_string = (char *)xmalloc (search_string_size = 128);
  *search_string = '\0';
  search_string_index = 0;
  prev_line_found = (char *)0;		/* XXX */

  /* Normalize DIRECTION into 1 or -1. */
  direction = (direction >= 0) ? 1 : -1;

  rl_display_search (search_string, reverse, -1);

  sline = rl_line_buffer;
  sline_len = strlen (sline);
  line_index = rl_point;

  found = failed = 0;
  for (;;)
    {
      rl_command_func_t *f = (rl_command_func_t *)NULL;

      /* Read a key and decide how to proceed. */
      RL_SETSTATE(RL_STATE_MOREINPUT);
      c = rl_read_key ();
      RL_UNSETSTATE(RL_STATE_MOREINPUT);

#if defined (HANDLE_MULTIBYTE)
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	c = _rl_read_mbstring (c, mb, MB_LEN_MAX);
#endif

      /* Translate the keys we do something with to opcodes. */
      if (c >= 0 && _rl_keymap[c].type == ISFUNC)
	{
	  f = _rl_keymap[c].function;

	  if (f == rl_reverse_search_history)
	    c = reverse ? -1 : -2;
	  else if (f == rl_forward_search_history)
	    c =  !reverse ? -1 : -2;
	  else if (f == rl_rubout)
	    c = -3;
	  else if (c == CTRL ('G'))
	    c = -4;
	  else if (c == CTRL ('W'))	/* XXX */
	    c = -5;
	  else if (c == CTRL ('Y'))	/* XXX */
	    c = -6;
	}

      /* The characters in isearch_terminators (set from the user-settable
	 variable isearch-terminators) are used to terminate the search but
	 not subsequently execute the character as a command.  The default
	 value is "\033\012" (ESC and C-J). */
      if (strchr (isearch_terminators, c))
	{
	  /* ESC still terminates the search, but if there is pending
	     input or if input arrives within 0.1 seconds (on systems
	     with select(2)) it is used as a prefix character
	     with rl_execute_next.  WATCH OUT FOR THIS!  This is intended
	     to allow the arrow keys to be used like ^F and ^B are used
	     to terminate the search and execute the movement command.
	     XXX - since _rl_input_available depends on the application-
	     settable keyboard timeout value, this could alternatively
	     use _rl_input_queued(100000) */
	  if (c == ESC && _rl_input_available ())
	    rl_execute_next (ESC);
	  break;
	}

#define ENDSRCH_CHAR(c) \
  ((CTRL_CHAR (c) || META_CHAR (c) || (c) == RUBOUT) && ((c) != CTRL ('G')))

#if defined (HANDLE_MULTIBYTE)
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  if (c >= 0 && strlen (mb) == 1 && ENDSRCH_CHAR (c))
	    {
	      /* This sets rl_pending_input to c; it will be picked up the next
		 time rl_read_key is called. */
	      rl_execute_next (c);
	      break;
	    }
	}
      else
#endif
      if (c >= 0 && ENDSRCH_CHAR (c))
	{
	  /* This sets rl_pending_input to c; it will be picked up the next
	     time rl_read_key is called. */
	  rl_execute_next (c);
	  break;
	}

      switch (c)
	{
	case -1:
	  if (search_string_index == 0)
	    {
	      if (last_isearch_string)
		{
		  search_string_size = 64 + last_isearch_string_len;
		  search_string = (char *)xrealloc (search_string, search_string_size);
		  strlcpy (search_string, last_isearch_string, search_string_size);
		  search_string_index = last_isearch_string_len;
		  rl_display_search (search_string, reverse, -1);
		  break;
		}
	      continue;
	    }
	  else if (reverse)
	    --line_index;
	  else if (line_index != sline_len)
	    ++line_index;
	  else
	    rl_ding ();
	  break;

	  /* switch directions */
	case -2:
	  direction = -direction;
	  reverse = direction < 0;
	  break;

	/* delete character from search string. */
	case -3:	/* C-H, DEL */
	  /* This is tricky.  To do this right, we need to keep a
	     stack of search positions for the current search, with
	     sentinels marking the beginning and end.  But this will
	     do until we have a real isearch-undo. */
	  if (search_string_index == 0)
	    rl_ding ();
	  else
	    search_string[--search_string_index] = '\0';

	  break;

	case -4:	/* C-G */
	  rl_replace_line (lines[orig_line], 0);
	  rl_point = orig_point;
	  rl_mark = orig_mark;
	  rl_restore_prompt();
	  rl_clear_message ();
	  if (allocated_line)
	    free (allocated_line);
	  free (lines);
	  RL_UNSETSTATE(RL_STATE_ISEARCH);
	  return 0;

	case -5:	/* C-W */
	  /* skip over portion of line we already matched */
	  wstart = rl_point + search_string_index;
	  if (wstart >= rl_end)
	    {
	      rl_ding ();
	      break;
	    }

	  /* if not in a word, move to one. */
	  if (rl_alphabetic(rl_line_buffer[wstart]) == 0)
	    {
	      rl_ding ();
	      break;
	    }
	  n = wstart;
	  while (n < rl_end && rl_alphabetic(rl_line_buffer[n]))
	    n++;
	  wlen = n - wstart + 1;
	  if (search_string_index + wlen + 1 >= search_string_size)
	    {
	      search_string_size += wlen + 1;
	      search_string = (char *)xrealloc (search_string, search_string_size);
	    }
	  for (; wstart < n; wstart++)
	    search_string[search_string_index++] = rl_line_buffer[wstart];
	  search_string[search_string_index] = '\0';
	  break;

	case -6:	/* C-Y */
	  /* skip over portion of line we already matched */
	  wstart = rl_point + search_string_index;
	  if (wstart >= rl_end)
	    {
	      rl_ding ();
	      break;
	    }
	  n = rl_end - wstart + 1;
	  if (search_string_index + n + 1 >= search_string_size)
	    {
	      search_string_size += n + 1;
	      search_string = (char *)xrealloc (search_string, search_string_size);
	    }
	  for (n = wstart; n < rl_end; n++)
	    search_string[search_string_index++] = rl_line_buffer[n];
	  search_string[search_string_index] = '\0';
	  break;

	default:
	  /* Add character to search string and continue search. */
	  if (search_string_index + 2 >= search_string_size)
	    {
	      search_string_size += 128;
	      search_string = (char *)xrealloc (search_string, search_string_size);
	    }
#if defined (HANDLE_MULTIBYTE)
	  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	    {
	      int j, l;
	      for (j = 0, l = strlen (mb); j < l; )
		search_string[search_string_index++] = mb[j++];
	    }
	  else
#endif
	    search_string[search_string_index++] = c;
	  search_string[search_string_index] = '\0';
	  break;
	}

      for (found = failed = 0;;)
	{
	  int limit = sline_len - search_string_index + 1;

	  /* Search the current line. */
	  while (reverse ? (line_index >= 0) : (line_index < limit))
	    {
	      if (STREQN (search_string, sline + line_index, search_string_index))
		{
		  found++;
		  break;
		}
	      else
		line_index += direction;
	    }
	  if (found)
	    break;

	  /* Move to the next line, but skip new copies of the line
	     we just found and lines shorter than the string we're
	     searching for. */
	  do
	    {
	      /* Move to the next line. */
	      i += direction;

	      /* At limit for direction? */
	      if (reverse ? (i < 0) : (i == hlen))
		{
		  failed++;
		  break;
		}

	      /* We will need these later. */
	      sline = lines[i];
	      sline_len = strlen (sline);
	    }
	  while ((prev_line_found && STREQ (prev_line_found, lines[i])) ||
		 (search_string_index > sline_len));

	  if (failed)
	    break;

	  /* Now set up the line for searching... */
	  line_index = reverse ? sline_len - search_string_index : 0;
	}

      if (failed)
	{
	  /* We cannot find the search string.  Ding the bell. */
	  rl_ding ();
	  i = last_found_line;
	  continue; 		/* XXX - was break */
	}

      /* We have found the search string.  Just display it.  But don't
	 actually move there in the history list until the user accepts
	 the location. */
      if (found)
	{
	  prev_line_found = lines[i];
	  rl_replace_line (lines[i], 0);
	  rl_point = line_index;
	  last_found_line = i;
	  rl_display_search (search_string, reverse, (i == orig_line) ? -1 : i);
	}
    }

  /* The searching is over.  The user may have found the string that she
     was looking for, or else she may have exited a failing search.  If
     LINE_INDEX is -1, then that shows that the string searched for was
     not found.  We use this to determine where to place rl_point. */

  /* First put back the original state. */
  strlcpy (rl_line_buffer, lines[orig_line], rl_line_buffer_len);

  rl_restore_prompt ();

  /* Save the search string for possible later use. */
  FREE (last_isearch_string);
  last_isearch_string = search_string;
  last_isearch_string_len = search_string_index;

  if (last_found_line < orig_line)
    rl_get_previous_history (orig_line - last_found_line, 0);
  else
    rl_get_next_history (last_found_line - orig_line, 0);

  /* If the string was not found, put point at the end of the last matching
     line.  If last_found_line == orig_line, we didn't find any matching
     history lines at all, so put point back in its original position. */
  if (line_index < 0)
    {
      if (last_found_line == orig_line)
	line_index = orig_point;
      else
	line_index = strlen (rl_line_buffer);
      rl_mark = orig_mark;
    }

  rl_point = line_index;
  /* Don't worry about where to put the mark here; rl_get_previous_history
     and rl_get_next_history take care of it. */

  rl_clear_message ();

  FREE (allocated_line);
  free (lines);

  RL_UNSETSTATE(RL_STATE_ISEARCH);

  return 0;
}
