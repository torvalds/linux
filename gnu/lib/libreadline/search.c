/* search.c - code for non-incremental searching in emacs and vi modes. */

/* Copyright (C) 1992 Free Software Foundation, Inc.

   This file is part of the Readline Library (the Library), a set of
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

#ifdef abs
#  undef abs
#endif
#define abs(x)		(((x) >= 0) ? (x) : -(x))

extern HIST_ENTRY *_rl_saved_line_for_history;

/* Functions imported from the rest of the library. */
extern int _rl_free_history_entry PARAMS((HIST_ENTRY *));

static char *noninc_search_string = (char *) NULL;
static int noninc_history_pos;

static char *prev_line_found = (char *) NULL;

static int rl_history_search_len;
static int rl_history_search_pos;
static char *history_search_string;
static int history_string_size;

static void make_history_line_current PARAMS((HIST_ENTRY *));
static int noninc_search_from_pos PARAMS((char *, int, int));
static void noninc_dosearch PARAMS((char *, int));
static void noninc_search PARAMS((int, int));
static int rl_history_search_internal PARAMS((int, int));
static void rl_history_search_reinit PARAMS((void));

/* Make the data from the history entry ENTRY be the contents of the
   current line.  This doesn't do anything with rl_point; the caller
   must set it. */
static void
make_history_line_current (entry)
     HIST_ENTRY *entry;
{
  rl_replace_line (entry->line, 0);
  rl_undo_list = (UNDO_LIST *)entry->data;

  if (_rl_saved_line_for_history)
    _rl_free_history_entry (_rl_saved_line_for_history);
  _rl_saved_line_for_history = (HIST_ENTRY *)NULL;
}

/* Search the history list for STRING starting at absolute history position
   POS.  If STRING begins with `^', the search must match STRING at the
   beginning of a history line, otherwise a full substring match is performed
   for STRING.  DIR < 0 means to search backwards through the history list,
   DIR >= 0 means to search forward. */
static int
noninc_search_from_pos (string, pos, dir)
     char *string;
     int pos, dir;
{
  int ret, old;

  if (pos < 0)
    return -1;

  old = where_history ();
  if (history_set_pos (pos) == 0)
    return -1;

  RL_SETSTATE(RL_STATE_SEARCH);
  if (*string == '^')
    ret = history_search_prefix (string + 1, dir);
  else
    ret = history_search (string, dir);
  RL_UNSETSTATE(RL_STATE_SEARCH);

  if (ret != -1)
    ret = where_history ();

  history_set_pos (old);
  return (ret);
}

/* Search for a line in the history containing STRING.  If DIR is < 0, the
   search is backwards through previous entries, else through subsequent
   entries. */
static void
noninc_dosearch (string, dir)
     char *string;
     int dir;
{
  int oldpos, pos;
  HIST_ENTRY *entry;

  if (string == 0 || *string == '\0' || noninc_history_pos < 0)
    {
      rl_ding ();
      return;
    }

  pos = noninc_search_from_pos (string, noninc_history_pos + dir, dir);
  if (pos == -1)
    {
      /* Search failed, current history position unchanged. */
      rl_maybe_unsave_line ();
      rl_clear_message ();
      rl_point = 0;
      rl_ding ();
      return;
    }

  noninc_history_pos = pos;

  oldpos = where_history ();
  history_set_pos (noninc_history_pos);
  entry = current_history ();
#if defined (VI_MODE)
  if (rl_editing_mode != vi_mode)
#endif
  history_set_pos (oldpos);

  make_history_line_current (entry);

  rl_point = 0;
  rl_mark = rl_end;

  rl_clear_message ();
}

/* Search non-interactively through the history list.  DIR < 0 means to
   search backwards through the history of previous commands; otherwise
   the search is for commands subsequent to the current position in the
   history list.  PCHAR is the character to use for prompting when reading
   the search string; if not specified (0), it defaults to `:'. */
static void
noninc_search (dir, pchar)
     int dir;
     int pchar;
{
  int saved_point, saved_mark, c;
  char *p;
#if defined (HANDLE_MULTIBYTE)
  char mb[MB_LEN_MAX];
#endif

  rl_maybe_save_line ();
  saved_point = rl_point;
  saved_mark = rl_mark;

  /* Use the line buffer to read the search string. */
  rl_line_buffer[0] = 0;
  rl_end = rl_point = 0;

  p = _rl_make_prompt_for_search (pchar ? pchar : ':');
  rl_message (p, 0, 0);
  free (p);

#define SEARCH_RETURN rl_restore_prompt (); RL_UNSETSTATE(RL_STATE_NSEARCH); return

  RL_SETSTATE(RL_STATE_NSEARCH);
  /* Read the search string. */
  while (1)
    {
      RL_SETSTATE(RL_STATE_MOREINPUT);
      c = rl_read_key ();
      RL_UNSETSTATE(RL_STATE_MOREINPUT);

#if defined (HANDLE_MULTIBYTE)
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	c = _rl_read_mbstring (c, mb, MB_LEN_MAX);
#endif

      if (c == 0)
	break;

      switch (c)
	{
	case CTRL('H'):
	case RUBOUT:
	  if (rl_point == 0)
	    {
	      rl_maybe_unsave_line ();
	      rl_clear_message ();
	      rl_point = saved_point;
	      rl_mark = saved_mark;
	      SEARCH_RETURN;
	    }
	  _rl_rubout_char (1, c);
	  break;

	case CTRL('W'):
	  rl_unix_word_rubout (1, c);
	  break;

	case CTRL('U'):
	  rl_unix_line_discard (1, c);
	  break;

	case RETURN:
	case NEWLINE:
	  goto dosearch;
	  /* NOTREACHED */
	  break;

	case CTRL('C'):
	case CTRL('G'):
	  rl_maybe_unsave_line ();
	  rl_clear_message ();
	  rl_point = saved_point;
	  rl_mark = saved_mark;
	  rl_ding ();
	  SEARCH_RETURN;

	default:
#if defined (HANDLE_MULTIBYTE)
	  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	    rl_insert_text (mb);
	  else
#endif
	    _rl_insert_char (1, c);
	  break;
	}
      (*rl_redisplay_function) ();
    }

 dosearch:
  rl_mark = saved_mark;

  /* If rl_point == 0, we want to re-use the previous search string and
     start from the saved history position.  If there's no previous search
     string, punt. */
  if (rl_point == 0)
    {
      if (!noninc_search_string)
	{
	  rl_ding ();
	  SEARCH_RETURN;
	}
    }
  else
    {
      /* We want to start the search from the current history position. */
      noninc_history_pos = where_history ();
      FREE (noninc_search_string);
      noninc_search_string = savestring (rl_line_buffer);
    }

  rl_restore_prompt ();
  noninc_dosearch (noninc_search_string, dir);
  RL_UNSETSTATE(RL_STATE_NSEARCH);
}

/* Search forward through the history list for a string.  If the vi-mode
   code calls this, KEY will be `?'. */
int
rl_noninc_forward_search (count, key)
     int count, key;
{
  noninc_search (1, (key == '?') ? '?' : 0);
  return 0;
}

/* Reverse search the history list for a string.  If the vi-mode code
   calls this, KEY will be `/'. */
int
rl_noninc_reverse_search (count, key)
     int count, key;
{
  noninc_search (-1, (key == '/') ? '/' : 0);
  return 0;
}

/* Search forward through the history list for the last string searched
   for.  If there is no saved search string, abort. */
int
rl_noninc_forward_search_again (count, key)
     int count, key;
{
  if (!noninc_search_string)
    {
      rl_ding ();
      return (-1);
    }
  noninc_dosearch (noninc_search_string, 1);
  return 0;
}

/* Reverse search in the history list for the last string searched
   for.  If there is no saved search string, abort. */
int
rl_noninc_reverse_search_again (count, key)
     int count, key;
{
  if (!noninc_search_string)
    {
      rl_ding ();
      return (-1);
    }
  noninc_dosearch (noninc_search_string, -1);
  return 0;
}

static int
rl_history_search_internal (count, dir)
     int count, dir;
{
  HIST_ENTRY *temp;
  int ret, oldpos;

  rl_maybe_save_line ();
  temp = (HIST_ENTRY *)NULL;

  /* Search COUNT times through the history for a line whose prefix
     matches history_search_string.  When this loop finishes, TEMP,
     if non-null, is the history line to copy into the line buffer. */
  while (count)
    {
      ret = noninc_search_from_pos (history_search_string, rl_history_search_pos + dir, dir);
      if (ret == -1)
	break;

      /* Get the history entry we found. */
      rl_history_search_pos = ret;
      oldpos = where_history ();
      history_set_pos (rl_history_search_pos);
      temp = current_history ();
      history_set_pos (oldpos);

      /* Don't find multiple instances of the same line. */
      if (prev_line_found && STREQ (prev_line_found, temp->line))
        continue;
      prev_line_found = temp->line;
      count--;
    }

  /* If we didn't find anything at all, return. */
  if (temp == 0)
    {
      rl_maybe_unsave_line ();
      rl_ding ();
      /* If you don't want the saved history line (last match) to show up
         in the line buffer after the search fails, change the #if 0 to
         #if 1 */
#if 0
      if (rl_point > rl_history_search_len)
        {
          rl_point = rl_end = rl_history_search_len;
          rl_line_buffer[rl_end] = '\0';
          rl_mark = 0;
        }
#else
      rl_point = rl_history_search_len;	/* rl_maybe_unsave_line changes it */
      rl_mark = rl_end;
#endif
      return 1;
    }

  /* Copy the line we found into the current line buffer. */
  make_history_line_current (temp);

  rl_point = rl_history_search_len;
  rl_mark = rl_end;

  return 0;
}

static void
rl_history_search_reinit ()
{
  rl_history_search_pos = where_history ();
  rl_history_search_len = rl_point;
  prev_line_found = (char *)NULL;
  if (rl_point)
    {
      if (rl_history_search_len >= history_string_size - 2)
	{
	  history_string_size = rl_history_search_len + 2;
	  history_search_string = (char *)xrealloc (history_search_string, history_string_size);
	}
      history_search_string[0] = '^';
      strncpy (history_search_string + 1, rl_line_buffer, rl_point);
      history_search_string[rl_point + 1] = '\0';
    }
  _rl_free_saved_history_line ();
}

/* Search forward in the history for the string of characters
   from the start of the line to rl_point.  This is a non-incremental
   search. */
int
rl_history_search_forward (count, ignore)
     int count, ignore;
{
  if (count == 0)
    return (0);

  if (rl_last_func != rl_history_search_forward &&
      rl_last_func != rl_history_search_backward)
    rl_history_search_reinit ();

  if (rl_history_search_len == 0)
    return (rl_get_next_history (count, ignore));
  return (rl_history_search_internal (abs (count), (count > 0) ? 1 : -1));
}

/* Search backward through the history for the string of characters
   from the start of the line to rl_point.  This is a non-incremental
   search. */
int
rl_history_search_backward (count, ignore)
     int count, ignore;
{
  if (count == 0)
    return (0);

  if (rl_last_func != rl_history_search_forward &&
      rl_last_func != rl_history_search_backward)
    rl_history_search_reinit ();

  if (rl_history_search_len == 0)
    return (rl_get_previous_history (count, ignore));
  return (rl_history_search_internal (abs (count), (count > 0) ? -1 : 1));
}
