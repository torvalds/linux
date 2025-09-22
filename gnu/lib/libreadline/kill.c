/* kill.c -- kill ring management. */

/* Copyright (C) 1994 Free Software Foundation, Inc.

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
#  include <unistd.h>           /* for _POSIX_VERSION */
#endif /* HAVE_UNISTD_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include <stdio.h>

/* System-specific feature definitions and include files. */
#include "rldefs.h"

/* Some standard library routines. */
#include "readline.h"
#include "history.h"

#include "rlprivate.h"
#include "xmalloc.h"

/* **************************************************************** */
/*								    */
/*			Killing Mechanism			    */
/*								    */
/* **************************************************************** */

/* What we assume for a max number of kills. */
#define DEFAULT_MAX_KILLS 10

/* The real variable to look at to find out when to flush kills. */
static int rl_max_kills =  DEFAULT_MAX_KILLS;

/* Where to store killed text. */
static char **rl_kill_ring = (char **)NULL;

/* Where we are in the kill ring. */
static int rl_kill_index;

/* How many slots we have in the kill ring. */
static int rl_kill_ring_length;

static int _rl_copy_to_kill_ring PARAMS((char *, int));
static int region_kill_internal PARAMS((int));
static int _rl_copy_word_as_kill PARAMS((int, int));
static int rl_yank_nth_arg_internal PARAMS((int, int, int));

/* How to say that you only want to save a certain amount
   of kill material. */
int
rl_set_retained_kills (num)
     int num;
{
  return 0;
}

/* Add TEXT to the kill ring, allocating a new kill ring slot as necessary.
   This uses TEXT directly, so the caller must not free it.  If APPEND is
   non-zero, and the last command was a kill, the text is appended to the
   current kill ring slot, otherwise prepended. */
static int
_rl_copy_to_kill_ring (text, append)
     char *text;
     int append;
{
  char *old, *new;
  int slot;

  /* First, find the slot to work with. */
  if (_rl_last_command_was_kill == 0)
    {
      /* Get a new slot.  */
      if (rl_kill_ring == 0)
	{
	  /* If we don't have any defined, then make one. */
	  rl_kill_ring = (char **)
	    xmalloc (((rl_kill_ring_length = 1) + 1) * sizeof (char *));
	  rl_kill_ring[slot = 0] = (char *)NULL;
	}
      else
	{
	  /* We have to add a new slot on the end, unless we have
	     exceeded the max limit for remembering kills. */
	  slot = rl_kill_ring_length;
	  if (slot == rl_max_kills)
	    {
	      register int i;
	      free (rl_kill_ring[0]);
	      for (i = 0; i < slot; i++)
		rl_kill_ring[i] = rl_kill_ring[i + 1];
	    }
	  else
	    {
	      slot = rl_kill_ring_length += 1;
	      rl_kill_ring = (char **)xrealloc (rl_kill_ring, slot * sizeof (char *));
	    }
	  rl_kill_ring[--slot] = (char *)NULL;
	}
    }
  else
    slot = rl_kill_ring_length - 1;

  /* If the last command was a kill, prepend or append. */
  if (_rl_last_command_was_kill && rl_editing_mode != vi_mode)
    {
      int len;
      old = rl_kill_ring[slot];
      len = 1 + strlen (old) + strlen (text);
      new = (char *)xmalloc (len);

      if (append)
	{
	  strlcpy (new, old, len);
	  strlcat (new, text, len);
	}
      else
	{
	  strlcpy (new, text, len);
	  strlcat (new, old, len);
	}
      free (old);
      free (text);
      rl_kill_ring[slot] = new;
    }
  else
    rl_kill_ring[slot] = text;

  rl_kill_index = slot;
  return 0;
}

/* The way to kill something.  This appends or prepends to the last
   kill, if the last command was a kill command.  if FROM is less
   than TO, then the text is appended, otherwise prepended.  If the
   last command was not a kill command, then a new slot is made for
   this kill. */
int
rl_kill_text (from, to)
     int from, to;
{
  char *text;

  /* Is there anything to kill? */
  if (from == to)
    {
      _rl_last_command_was_kill++;
      return 0;
    }

  text = rl_copy_text (from, to);

  /* Delete the copied text from the line. */
  rl_delete_text (from, to);

  _rl_copy_to_kill_ring (text, from < to);

  _rl_last_command_was_kill++;
  return 0;
}

/* Now REMEMBER!  In order to do prepending or appending correctly, kill
   commands always make rl_point's original position be the FROM argument,
   and rl_point's extent be the TO argument. */

/* **************************************************************** */
/*								    */
/*			Killing Commands			    */
/*								    */
/* **************************************************************** */

/* Delete the word at point, saving the text in the kill ring. */
int
rl_kill_word (count, key)
     int count, key;
{
  int orig_point;

  if (count < 0)
    return (rl_backward_kill_word (-count, key));
  else
    {
      orig_point = rl_point;
      rl_forward_word (count, key);

      if (rl_point != orig_point)
	rl_kill_text (orig_point, rl_point);

      rl_point = orig_point;
      if (rl_editing_mode == emacs_mode)
	rl_mark = rl_point;
    }
  return 0;
}

/* Rubout the word before point, placing it on the kill ring. */
int
rl_backward_kill_word (count, ignore)
     int count, ignore;
{
  int orig_point;

  if (count < 0)
    return (rl_kill_word (-count, ignore));
  else
    {
      orig_point = rl_point;
      rl_backward_word (count, ignore);

      if (rl_point != orig_point)
	rl_kill_text (orig_point, rl_point);

      if (rl_editing_mode == emacs_mode)
	rl_mark = rl_point;
    }
  return 0;
}

/* Kill from here to the end of the line.  If DIRECTION is negative, kill
   back to the line start instead. */
int
rl_kill_line (direction, ignore)
     int direction, ignore;
{
  int orig_point;

  if (direction < 0)
    return (rl_backward_kill_line (1, ignore));
  else
    {
      orig_point = rl_point;
      rl_end_of_line (1, ignore);
      if (orig_point != rl_point)
	rl_kill_text (orig_point, rl_point);
      rl_point = orig_point;
      if (rl_editing_mode == emacs_mode)
	rl_mark = rl_point;
    }
  return 0;
}

/* Kill backwards to the start of the line.  If DIRECTION is negative, kill
   forwards to the line end instead. */
int
rl_backward_kill_line (direction, ignore)
     int direction, ignore;
{
  int orig_point;

  if (direction < 0)
    return (rl_kill_line (1, ignore));
  else
    {
      if (!rl_point)
	rl_ding ();
      else
	{
	  orig_point = rl_point;
	  rl_beg_of_line (1, ignore);
	  if (rl_point != orig_point)
	    rl_kill_text (orig_point, rl_point);
	  if (rl_editing_mode == emacs_mode)
	    rl_mark = rl_point;
	}
    }
  return 0;
}

/* Kill the whole line, no matter where point is. */
int
rl_kill_full_line (count, ignore)
     int count, ignore;
{
  rl_begin_undo_group ();
  rl_point = 0;
  rl_kill_text (rl_point, rl_end);
  rl_mark = 0;
  rl_end_undo_group ();
  return 0;
}

/* The next two functions mimic unix line editing behaviour, except they
   save the deleted text on the kill ring.  This is safer than not saving
   it, and since we have a ring, nobody should get screwed. */

/* This does what C-w does in Unix.  We can't prevent people from
   using behaviour that they expect. */
int
rl_unix_word_rubout (count, key)
     int count, key;
{
  int orig_point;

  if (rl_point == 0)
    rl_ding ();
  else
    {
      orig_point = rl_point;
      if (count <= 0)
	count = 1;

      while (count--)
	{
	  while (rl_point && whitespace (rl_line_buffer[rl_point - 1]))
	    rl_point--;

	  while (rl_point && (whitespace (rl_line_buffer[rl_point - 1]) == 0))
	    rl_point--;
	}

      rl_kill_text (orig_point, rl_point);
      if (rl_editing_mode == emacs_mode)
	rl_mark = rl_point;
    }
  return 0;
}

/* Here is C-u doing what Unix does.  You don't *have* to use these
   key-bindings.  We have a choice of killing the entire line, or
   killing from where we are to the start of the line.  We choose the
   latter, because if you are a Unix weenie, then you haven't backspaced
   into the line at all, and if you aren't, then you know what you are
   doing. */
int
rl_unix_line_discard (count, key)
     int count, key;
{
  if (rl_point == 0)
    rl_ding ();
  else
    {
      rl_kill_text (rl_point, 0);
      rl_point = 0;
      if (rl_editing_mode == emacs_mode)
	rl_mark = rl_point;
    }
  return 0;
}

/* Copy the text in the `region' to the kill ring.  If DELETE is non-zero,
   delete the text from the line as well. */
static int
region_kill_internal (delete)
     int delete;
{
  char *text;

  if (rl_mark != rl_point)
    {
      text = rl_copy_text (rl_point, rl_mark);
      if (delete)
	rl_delete_text (rl_point, rl_mark);
      _rl_copy_to_kill_ring (text, rl_point < rl_mark);
    }

  _rl_last_command_was_kill++;
  return 0;
}

/* Copy the text in the region to the kill ring. */
int
rl_copy_region_to_kill (count, ignore)
     int count, ignore;
{
  return (region_kill_internal (0));
}

/* Kill the text between the point and mark. */
int
rl_kill_region (count, ignore)
     int count, ignore;
{
  int r, npoint;

  npoint = (rl_point < rl_mark) ? rl_point : rl_mark;
  r = region_kill_internal (1);
  _rl_fix_point (1);
  rl_point = npoint;
  return r;
}

/* Copy COUNT words to the kill ring.  DIR says which direction we look
   to find the words. */
static int
_rl_copy_word_as_kill (count, dir)
     int count, dir;
{
  int om, op, r;

  om = rl_mark;
  op = rl_point;

  if (dir > 0)
    rl_forward_word (count, 0);
  else
    rl_backward_word (count, 0);

  rl_mark = rl_point;

  if (dir > 0)
    rl_backward_word (count, 0);
  else
    rl_forward_word (count, 0);

  r = region_kill_internal (0);

  rl_mark = om;
  rl_point = op;

  return r;
}

int
rl_copy_forward_word (count, key)
     int count, key;
{
  if (count < 0)
    return (rl_copy_backward_word (-count, key));

  return (_rl_copy_word_as_kill (count, 1));
}

int
rl_copy_backward_word (count, key)
     int count, key;
{
  if (count < 0)
    return (rl_copy_forward_word (-count, key));

  return (_rl_copy_word_as_kill (count, -1));
}

/* Yank back the last killed text.  This ignores arguments. */
int
rl_yank (count, ignore)
     int count, ignore;
{
  if (rl_kill_ring == 0)
    {
      _rl_abort_internal ();
      return -1;
    }

  _rl_set_mark_at_pos (rl_point);
  rl_insert_text (rl_kill_ring[rl_kill_index]);
  return 0;
}

/* If the last command was yank, or yank_pop, and the text just
   before point is identical to the current kill item, then
   delete that text from the line, rotate the index down, and
   yank back some other text. */
int
rl_yank_pop (count, key)
     int count, key;
{
  int l, n;

  if (((rl_last_func != rl_yank_pop) && (rl_last_func != rl_yank)) ||
      !rl_kill_ring)
    {
      _rl_abort_internal ();
      return -1;
    }

  l = strlen (rl_kill_ring[rl_kill_index]);
  n = rl_point - l;
  if (n >= 0 && STREQN (rl_line_buffer + n, rl_kill_ring[rl_kill_index], l))
    {
      rl_delete_text (n, rl_point);
      rl_point = n;
      rl_kill_index--;
      if (rl_kill_index < 0)
	rl_kill_index = rl_kill_ring_length - 1;
      rl_yank (1, 0);
      return 0;
    }
  else
    {
      _rl_abort_internal ();
      return -1;
    }
}

/* Yank the COUNTh argument from the previous history line, skipping
   HISTORY_SKIP lines before looking for the `previous line'. */
static int
rl_yank_nth_arg_internal (count, ignore, history_skip)
     int count, ignore, history_skip;
{
  register HIST_ENTRY *entry;
  char *arg;
  int i, pos;

  pos = where_history ();

  if (history_skip)
    {
      for (i = 0; i < history_skip; i++)
	entry = previous_history ();
    }

  entry = previous_history ();

  history_set_pos (pos);

  if (entry == 0)
    {
      rl_ding ();
      return -1;
    }

  arg = history_arg_extract (count, count, entry->line);
  if (!arg || !*arg)
    {
      rl_ding ();
      return -1;
    }

  rl_begin_undo_group ();

  _rl_set_mark_at_pos (rl_point);

#if defined (VI_MODE)
  /* Vi mode always inserts a space before yanking the argument, and it
     inserts it right *after* rl_point. */
  if (rl_editing_mode == vi_mode)
    {
      rl_vi_append_mode (1, ignore);
      rl_insert_text (" ");
    }
#endif /* VI_MODE */

  rl_insert_text (arg);
  free (arg);

  rl_end_undo_group ();
  return 0;
}

/* Yank the COUNTth argument from the previous history line. */
int
rl_yank_nth_arg (count, ignore)
     int count, ignore;
{
  return (rl_yank_nth_arg_internal (count, ignore, 0));
}

/* Yank the last argument from the previous history line.  This `knows'
   how rl_yank_nth_arg treats a count of `$'.  With an argument, this
   behaves the same as rl_yank_nth_arg. */
int
rl_yank_last_arg (count, key)
     int count, key;
{
  static int history_skip = 0;
  static int explicit_arg_p = 0;
  static int count_passed = 1;
  static int direction = 1;
  static int undo_needed = 0;
  int retval;

  if (rl_last_func != rl_yank_last_arg)
    {
      history_skip = 0;
      explicit_arg_p = rl_explicit_arg;
      count_passed = count;
      direction = 1;
    }
  else
    {
      if (undo_needed)
	rl_do_undo ();
      if (count < 1)
        direction = -direction;
      history_skip += direction;
      if (history_skip < 0)
	history_skip = 0;
    }

  if (explicit_arg_p)
    retval = rl_yank_nth_arg_internal (count_passed, key, history_skip);
  else
    retval = rl_yank_nth_arg_internal ('$', key, history_skip);

  undo_needed = retval == 0;
  return retval;
}

/* A special paste command for users of Cygnus's cygwin32. */
#if defined (__CYGWIN__)
#include <windows.h>

int
rl_paste_from_clipboard (count, key)
     int count, key;
{
  char *data, *ptr;
  int len;

  if (OpenClipboard (NULL) == 0)
    return (0);

  data = (char *)GetClipboardData (CF_TEXT);
  if (data)
    {
      ptr = strchr (data, '\r');
      if (ptr)
	{
	  len = ptr - data;
	  ptr = (char *)xmalloc (len + 1);
	  ptr[len] = '\0';
	  strncpy (ptr, data, len);
	}
      else
        ptr = data;
      _rl_set_mark_at_pos (rl_point);
      rl_insert_text (ptr);
      if (ptr != data)
	free (ptr);
      CloseClipboard ();
    }
  return (0);
}
#endif /* __CYGWIN__ */
