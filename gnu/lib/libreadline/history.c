/* History.c -- standalone history library */

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

/* The goal is to make the implementation transparent, so that you
   don't have to know what data types are used, just what functions
   you can call.  I think I have done that. */
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

#include "xmalloc.h"

/* The number of slots to increase the_history by. */
#define DEFAULT_HISTORY_GROW_SIZE 50

/* **************************************************************** */
/*								    */
/*			History Functions			    */
/*								    */
/* **************************************************************** */

/* An array of HIST_ENTRY.  This is where we store the history. */
static HIST_ENTRY **the_history = (HIST_ENTRY **)NULL;

/* Non-zero means that we have enforced a limit on the amount of
   history that we save. */
static int history_stifled;

/* The current number of slots allocated to the input_history. */
static int history_size;

/* If HISTORY_STIFLED is non-zero, then this is the maximum number of
   entries to remember. */
int history_max_entries;
int max_input_history;	/* backwards compatibility */

/* The current location of the interactive history pointer.  Just makes
   life easier for outside callers. */
int history_offset;

/* The number of strings currently stored in the history list. */
int history_length;

/* The logical `base' of the history array.  It defaults to 1. */
int history_base = 1;

/* Return the current HISTORY_STATE of the history. */
HISTORY_STATE *
history_get_history_state ()
{
  HISTORY_STATE *state;

  state = (HISTORY_STATE *)xmalloc (sizeof (HISTORY_STATE));
  state->entries = the_history;
  state->offset = history_offset;
  state->length = history_length;
  state->size = history_size;
  state->flags = 0;
  if (history_stifled)
    state->flags |= HS_STIFLED;

  return (state);
}

/* Set the state of the current history array to STATE. */
void
history_set_history_state (state)
     HISTORY_STATE *state;
{
  the_history = state->entries;
  history_offset = state->offset;
  history_length = state->length;
  history_size = state->size;
  if (state->flags & HS_STIFLED)
    history_stifled = 1;
}

/* Begin a session in which the history functions might be used.  This
   initializes interactive variables. */
void
using_history ()
{
  history_offset = history_length;
}

/* Return the number of bytes that the primary history entries are using.
   This just adds up the lengths of the_history->lines. */
int
history_total_bytes ()
{
  register int i, result;

  for (i = result = 0; the_history && the_history[i]; i++)
    result += strlen (the_history[i]->line);

  return (result);
}

/* Returns the magic number which says what history element we are
   looking at now.  In this implementation, it returns history_offset. */
int
where_history ()
{
  return (history_offset);
}

/* Make the current history item be the one at POS, an absolute index.
   Returns zero if POS is out of range, else non-zero. */
int
history_set_pos (pos)
     int pos;
{
  if (pos > history_length || pos < 0 || !the_history)
    return (0);
  history_offset = pos;
  return (1);
}

/* Return the current history array.  The caller has to be carefull, since this
   is the actual array of data, and could be bashed or made corrupt easily.
   The array is terminated with a NULL pointer. */
HIST_ENTRY **
history_list ()
{
  return (the_history);
}

/* Return the history entry at the current position, as determined by
   history_offset.  If there is no entry there, return a NULL pointer. */
HIST_ENTRY *
current_history ()
{
  return ((history_offset == history_length) || the_history == 0)
		? (HIST_ENTRY *)NULL
		: the_history[history_offset];
}

/* Back up history_offset to the previous history entry, and return
   a pointer to that entry.  If there is no previous entry then return
   a NULL pointer. */
HIST_ENTRY *
previous_history ()
{
  return history_offset ? the_history[--history_offset] : (HIST_ENTRY *)NULL;
}

/* Move history_offset forward to the next history entry, and return
   a pointer to that entry.  If there is no next entry then return a
   NULL pointer. */
HIST_ENTRY *
next_history ()
{
  return (history_offset == history_length) ? (HIST_ENTRY *)NULL : the_history[++history_offset];
}

/* Return the history entry which is logically at OFFSET in the history array.
   OFFSET is relative to history_base. */
HIST_ENTRY *
history_get (offset)
     int offset;
{
  int local_index;

  local_index = offset - history_base;
  return (local_index >= history_length || local_index < 0 || !the_history)
		? (HIST_ENTRY *)NULL
		: the_history[local_index];
}

/* Place STRING at the end of the history list.  The data field
   is  set to NULL. */
void
add_history (string)
     const char *string;
{
  HIST_ENTRY *temp;

  if (history_stifled && (history_length == history_max_entries))
    {
      register int i;

      /* If the history is stifled, and history_length is zero,
	 and it equals history_max_entries, we don't save items. */
      if (history_length == 0)
	return;

      /* If there is something in the slot, then remove it. */
      if (the_history[0])
	{
	  free (the_history[0]->line);
	  free (the_history[0]);
	}

      /* Copy the rest of the entries, moving down one slot. */
      for (i = 0; i < history_length; i++)
	the_history[i] = the_history[i + 1];

      history_base++;
    }
  else
    {
      if (history_size == 0)
	{
	  history_size = DEFAULT_HISTORY_GROW_SIZE;
	  the_history = (HIST_ENTRY **)xmalloc (history_size * sizeof (HIST_ENTRY *));
	  history_length = 1;
	}
      else
	{
	  if (history_length == (history_size - 1))
	    {
	      history_size += DEFAULT_HISTORY_GROW_SIZE;
	      the_history = (HIST_ENTRY **)
		xrealloc (the_history, history_size * sizeof (HIST_ENTRY *));
	    }
	  history_length++;
	}
    }

  temp = (HIST_ENTRY *)xmalloc (sizeof (HIST_ENTRY));
  temp->line = savestring (string);
  temp->data = (char *)NULL;

  the_history[history_length] = (HIST_ENTRY *)NULL;
  the_history[history_length - 1] = temp;
}

/* Make the history entry at WHICH have LINE and DATA.  This returns
   the old entry so you can dispose of the data.  In the case of an
   invalid WHICH, a NULL pointer is returned. */
HIST_ENTRY *
replace_history_entry (which, line, data)
     int which;
     const char *line;
     histdata_t data;
{
  HIST_ENTRY *temp, *old_value;

  if (which >= history_length)
    return ((HIST_ENTRY *)NULL);

  temp = (HIST_ENTRY *)xmalloc (sizeof (HIST_ENTRY));
  old_value = the_history[which];

  temp->line = savestring (line);
  temp->data = data;
  the_history[which] = temp;

  return (old_value);
}

/* Remove history element WHICH from the history.  The removed
   element is returned to you so you can free the line, data,
   and containing structure. */
HIST_ENTRY *
remove_history (which)
     int which;
{
  HIST_ENTRY *return_value;
  register int i;

  if (which >= history_length || !history_length)
    return_value = (HIST_ENTRY *)NULL;
  else
    {
      return_value = the_history[which];

      for (i = which; i < history_length; i++)
	the_history[i] = the_history[i + 1];

      history_length--;
    }

  return (return_value);
}

/* Stifle the history list, remembering only MAX number of lines. */
void
stifle_history (max)
     int max;
{
  register int i, j;

  if (max < 0)
    max = 0;

  if (history_length > max)
    {
      /* This loses because we cannot free the data. */
      for (i = 0, j = history_length - max; i < j; i++)
	{
	  free (the_history[i]->line);
	  free (the_history[i]);
	}

      history_base = i;
      for (j = 0, i = history_length - max; j < max; i++, j++)
	the_history[j] = the_history[i];
      the_history[j] = (HIST_ENTRY *)NULL;
      history_length = j;
    }

  history_stifled = 1;
  max_input_history = history_max_entries = max;
}

/* Stop stifling the history.  This returns the previous maximum
   number of history entries.  The value is positive if the history
   was stifled,  negative if it wasn't. */
int
unstifle_history ()
{
  if (history_stifled)
    {
      history_stifled = 0;
      return (history_max_entries);
    }
  else
    return (-history_max_entries);
}

int
history_is_stifled ()
{
  return (history_stifled);
}

void
clear_history ()
{
  register int i;

  /* This loses because we cannot free the data. */
  for (i = 0; i < history_length; i++)
    {
      free (the_history[i]->line);
      free (the_history[i]);
      the_history[i] = (HIST_ENTRY *)NULL;
    }

  history_offset = history_length = 0;
}
