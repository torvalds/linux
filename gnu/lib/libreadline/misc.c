/* misc.c -- miscellaneous bindable readline functions. */

/* Copyright (C) 1987-2002 Free Software Foundation, Inc.

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

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#if defined (HAVE_LOCALE_H)
#  include <locale.h>
#endif

#include <stdio.h>

/* System-specific feature definitions and include files. */
#include "rldefs.h"
#include "rlmbutil.h"

/* Some standard library routines. */
#include "readline.h"
#include "history.h"

#include "rlprivate.h"
#include "rlshell.h"
#include "xmalloc.h"

static int rl_digit_loop PARAMS((void));
static void _rl_history_set_point PARAMS((void));

/* Forward declarations used in this file */
void _rl_free_history_entry PARAMS((HIST_ENTRY *));

/* If non-zero, rl_get_previous_history and rl_get_next_history attempt
   to preserve the value of rl_point from line to line. */
int _rl_history_preserve_point = 0;

/* Saved target point for when _rl_history_preserve_point is set.  Special
   value of -1 means that point is at the end of the line. */
int _rl_history_saved_point = -1;

/* **************************************************************** */
/*								    */
/*			Numeric Arguments			    */
/*								    */
/* **************************************************************** */

/* Handle C-u style numeric args, as well as M--, and M-digits. */
static int
rl_digit_loop ()
{
  int key, c, sawminus, sawdigits;

  rl_save_prompt ();

  RL_SETSTATE(RL_STATE_NUMERICARG);
  sawminus = sawdigits = 0;
  while (1)
    {
      if (rl_numeric_arg > 1000000)
	{
	  sawdigits = rl_explicit_arg = rl_numeric_arg = 0;
	  rl_ding ();
	  rl_restore_prompt ();
	  rl_clear_message ();
	  RL_UNSETSTATE(RL_STATE_NUMERICARG);
	  return 1;
	}
      rl_message ("(arg: %d) ", rl_arg_sign * rl_numeric_arg);
      RL_SETSTATE(RL_STATE_MOREINPUT);
      key = c = rl_read_key ();
      RL_UNSETSTATE(RL_STATE_MOREINPUT);

      if (c < 0)
	{
	  _rl_abort_internal ();
	  return -1;
	}

      /* If we see a key bound to `universal-argument' after seeing digits,
	 it ends the argument but is otherwise ignored. */
      if (_rl_keymap[c].type == ISFUNC &&
	  _rl_keymap[c].function == rl_universal_argument)
	{
	  if (sawdigits == 0)
	    {
	      rl_numeric_arg *= 4;
	      continue;
	    }
	  else
	    {
	      RL_SETSTATE(RL_STATE_MOREINPUT);
	      key = rl_read_key ();
	      RL_UNSETSTATE(RL_STATE_MOREINPUT);
	      rl_restore_prompt ();
	      rl_clear_message ();
	      RL_UNSETSTATE(RL_STATE_NUMERICARG);
	      return (_rl_dispatch (key, _rl_keymap));
	    }
	}

      c = UNMETA (c);

      if (_rl_digit_p (c))
	{
	  rl_numeric_arg = rl_explicit_arg ? (rl_numeric_arg * 10) + c - '0' : c - '0';
	  sawdigits = rl_explicit_arg = 1;
	}
      else if (c == '-' && rl_explicit_arg == 0)
	{
	  rl_numeric_arg = sawminus = 1;
	  rl_arg_sign = -1;
	}
      else
	{
	  /* Make M-- command equivalent to M--1 command. */
	  if (sawminus && rl_numeric_arg == 1 && rl_explicit_arg == 0)
	    rl_explicit_arg = 1;
	  rl_restore_prompt ();
	  rl_clear_message ();
	  RL_UNSETSTATE(RL_STATE_NUMERICARG);
	  return (_rl_dispatch (key, _rl_keymap));
	}
    }

  /*NOTREACHED*/
}

/* Add the current digit to the argument in progress. */
int
rl_digit_argument (ignore, key)
     int ignore, key;
{
  rl_execute_next (key);
  return (rl_digit_loop ());
}

/* What to do when you abort reading an argument. */
int
rl_discard_argument ()
{
  rl_ding ();
  rl_clear_message ();
  _rl_init_argument ();
  return 0;
}

/* Create a default argument. */
int
_rl_init_argument ()
{
  rl_numeric_arg = rl_arg_sign = 1;
  rl_explicit_arg = 0;
  return 0;
}

/* C-u, universal argument.  Multiply the current argument by 4.
   Read a key.  If the key has nothing to do with arguments, then
   dispatch on it.  If the key is the abort character then abort. */
int
rl_universal_argument (count, key)
     int count, key;
{
  rl_numeric_arg *= 4;
  return (rl_digit_loop ());
}

/* **************************************************************** */
/*								    */
/*			History Utilities			    */
/*								    */
/* **************************************************************** */

/* We already have a history library, and that is what we use to control
   the history features of readline.  This is our local interface to
   the history mechanism. */

/* While we are editing the history, this is the saved
   version of the original line. */
HIST_ENTRY *_rl_saved_line_for_history = (HIST_ENTRY *)NULL;

/* Set the history pointer back to the last entry in the history. */
void
_rl_start_using_history ()
{
  using_history ();
  if (_rl_saved_line_for_history)
    _rl_free_history_entry (_rl_saved_line_for_history);

  _rl_saved_line_for_history = (HIST_ENTRY *)NULL;
}

/* Free the contents (and containing structure) of a HIST_ENTRY. */
void
_rl_free_history_entry (entry)
     HIST_ENTRY *entry;
{
  if (entry == 0)
    return;
  if (entry->line)
    free (entry->line);
  free (entry);
}

/* Perhaps put back the current line if it has changed. */
int
rl_maybe_replace_line ()
{
  HIST_ENTRY *temp;

  temp = current_history ();
  /* If the current line has changed, save the changes. */
  if (temp && ((UNDO_LIST *)(temp->data) != rl_undo_list))
    {
      temp = replace_history_entry (where_history (), rl_line_buffer, (histdata_t)rl_undo_list);
      free (temp->line);
      free (temp);
    }
  return 0;
}

/* Restore the _rl_saved_line_for_history if there is one. */
int
rl_maybe_unsave_line ()
{
  if (_rl_saved_line_for_history)
    {
      rl_replace_line (_rl_saved_line_for_history->line, 0);
      rl_undo_list = (UNDO_LIST *)_rl_saved_line_for_history->data;
      _rl_free_history_entry (_rl_saved_line_for_history);
      _rl_saved_line_for_history = (HIST_ENTRY *)NULL;
      rl_point = rl_end;	/* rl_replace_line sets rl_end */
    }
  else
    rl_ding ();
  return 0;
}

/* Save the current line in _rl_saved_line_for_history. */
int
rl_maybe_save_line ()
{
  if (_rl_saved_line_for_history == 0)
    {
      _rl_saved_line_for_history = (HIST_ENTRY *)xmalloc (sizeof (HIST_ENTRY));
      _rl_saved_line_for_history->line = savestring (rl_line_buffer);
      _rl_saved_line_for_history->data = (char *)rl_undo_list;
    }
  return 0;
}

int
_rl_free_saved_history_line ()
{
  if (_rl_saved_line_for_history)
    {
      _rl_free_history_entry (_rl_saved_line_for_history);
      _rl_saved_line_for_history = (HIST_ENTRY *)NULL;
    }
  return 0;
}

static void
_rl_history_set_point ()
{
  rl_point = (_rl_history_preserve_point && _rl_history_saved_point != -1)
		? _rl_history_saved_point
		: rl_end;
  if (rl_point > rl_end)
    rl_point = rl_end;

#if defined (VI_MODE)
  if (rl_editing_mode == vi_mode)
    rl_point = 0;
#endif /* VI_MODE */

  if (rl_editing_mode == emacs_mode)
    rl_mark = (rl_point == rl_end ? 0 : rl_end);
}

void
rl_replace_from_history (entry, flags)
     HIST_ENTRY *entry;
     int flags;			/* currently unused */
{
  rl_replace_line (entry->line, 0);
  rl_undo_list = (UNDO_LIST *)entry->data;
  rl_point = rl_end;
  rl_mark = 0;

#if defined (VI_MODE)
  if (rl_editing_mode == vi_mode)
    {
      rl_point = 0;
      rl_mark = rl_end;
    }
#endif
}

/* **************************************************************** */
/*								    */
/*			History Commands			    */
/*								    */
/* **************************************************************** */

/* Meta-< goes to the start of the history. */
int
rl_beginning_of_history (count, key)
     int count, key;
{
  return (rl_get_previous_history (1 + where_history (), key));
}

/* Meta-> goes to the end of the history.  (The current line). */
int
rl_end_of_history (count, key)
     int count, key;
{
  rl_maybe_replace_line ();
  using_history ();
  rl_maybe_unsave_line ();
  return 0;
}

/* Move down to the next history line. */
int
rl_get_next_history (count, key)
     int count, key;
{
  HIST_ENTRY *temp;

  if (count < 0)
    return (rl_get_previous_history (-count, key));

  if (count == 0)
    return 0;

  rl_maybe_replace_line ();

  /* either not saved by rl_newline or at end of line, so set appropriately. */
  if (_rl_history_saved_point == -1 && (rl_point || rl_end))
    _rl_history_saved_point = (rl_point == rl_end) ? -1 : rl_point;

  temp = (HIST_ENTRY *)NULL;
  while (count)
    {
      temp = next_history ();
      if (!temp)
	break;
      --count;
    }

  if (temp == 0)
    rl_maybe_unsave_line ();
  else
    {
      rl_replace_from_history (temp, 0);
      _rl_history_set_point ();
    }
  return 0;
}

/* Get the previous item out of our interactive history, making it the current
   line.  If there is no previous history, just ding. */
int
rl_get_previous_history (count, key)
     int count, key;
{
  HIST_ENTRY *old_temp, *temp;

  if (count < 0)
    return (rl_get_next_history (-count, key));

  if (count == 0)
    return 0;

  /* either not saved by rl_newline or at end of line, so set appropriately. */
  if (_rl_history_saved_point == -1 && (rl_point || rl_end))
    _rl_history_saved_point = (rl_point == rl_end) ? -1 : rl_point;

  /* If we don't have a line saved, then save this one. */
  rl_maybe_save_line ();

  /* If the current line has changed, save the changes. */
  rl_maybe_replace_line ();

  temp = old_temp = (HIST_ENTRY *)NULL;
  while (count)
    {
      temp = previous_history ();
      if (temp == 0)
	break;

      old_temp = temp;
      --count;
    }

  /* If there was a large argument, and we moved back to the start of the
     history, that is not an error.  So use the last value found. */
  if (!temp && old_temp)
    temp = old_temp;

  if (temp == 0)
    rl_ding ();
  else
    {
      rl_replace_from_history (temp, 0);
      _rl_history_set_point ();
    }
  return 0;
}

/* **************************************************************** */
/*								    */
/*			    Editing Modes			    */
/*								    */
/* **************************************************************** */
/* How to toggle back and forth between editing modes. */
int
rl_vi_editing_mode (count, key)
     int count, key;
{
#if defined (VI_MODE)
  _rl_set_insert_mode (RL_IM_INSERT, 1);	/* vi mode ignores insert mode */
  rl_editing_mode = vi_mode;
  rl_vi_insertion_mode (1, key);
#endif /* VI_MODE */

  return 0;
}

int
rl_emacs_editing_mode (count, key)
     int count, key;
{
  rl_editing_mode = emacs_mode;
  _rl_set_insert_mode (RL_IM_INSERT, 1); /* emacs mode default is insert mode */
  _rl_keymap = emacs_standard_keymap;
  return 0;
}

/* Function for the rest of the library to use to set insert/overwrite mode. */
void
_rl_set_insert_mode (im, force)
     int im, force;
{
#ifdef CURSOR_MODE
  _rl_set_cursor (im, force);
#endif

  rl_insert_mode = im;
}

/* Toggle overwrite mode.  A positive explicit argument selects overwrite
   mode.  A negative or zero explicit argument selects insert mode. */
int
rl_overwrite_mode (count, key)
     int count, key;
{
  if (rl_explicit_arg == 0)
    _rl_set_insert_mode (rl_insert_mode ^ 1, 0);
  else if (count > 0)
    _rl_set_insert_mode (RL_IM_OVERWRITE, 0);
  else
    _rl_set_insert_mode (RL_IM_INSERT, 0);

  return 0;
}
