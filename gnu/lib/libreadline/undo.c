/* readline.c -- a general facility for reading lines of input
   with emacs style editing and completion. */

/* Copyright (C) 1987, 1989, 1992 Free Software Foundation, Inc.

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

/* Non-zero tells rl_delete_text and rl_insert_text to not add to
   the undo list. */
int _rl_doing_an_undo = 0;

/* How many unclosed undo groups we currently have. */
int _rl_undo_group_level = 0;

/* The current undo list for THE_LINE. */
UNDO_LIST *rl_undo_list = (UNDO_LIST *)NULL;

/* **************************************************************** */
/*								    */
/*			Undo, and Undoing			    */
/*								    */
/* **************************************************************** */

/* Remember how to undo something.  Concatenate some undos if that
   seems right. */
void
rl_add_undo (what, start, end, text)
     enum undo_code what;
     int start, end;
     char *text;
{
  UNDO_LIST *temp = (UNDO_LIST *)xmalloc (sizeof (UNDO_LIST));
  temp->what = what;
  temp->start = start;
  temp->end = end;
  temp->text = text;
  temp->next = rl_undo_list;
  rl_undo_list = temp;
}

/* Free the existing undo list. */
void
rl_free_undo_list ()
{
  while (rl_undo_list)
    {
      UNDO_LIST *release = rl_undo_list;
      rl_undo_list = rl_undo_list->next;

      if (release->what == UNDO_DELETE)
	free (release->text);

      free (release);
    }
  rl_undo_list = (UNDO_LIST *)NULL;
}

/* Undo the next thing in the list.  Return 0 if there
   is nothing to undo, or non-zero if there was. */
int
rl_do_undo ()
{
  UNDO_LIST *release;
  int waiting_for_begin, start, end;

#define TRANS(i) ((i) == -1 ? rl_point : ((i) == -2 ? rl_end : (i)))

  start = end = waiting_for_begin = 0;
  do
    {
      if (!rl_undo_list)
	return (0);

      _rl_doing_an_undo = 1;
      RL_SETSTATE(RL_STATE_UNDOING);

      /* To better support vi-mode, a start or end value of -1 means
	 rl_point, and a value of -2 means rl_end. */
      if (rl_undo_list->what == UNDO_DELETE || rl_undo_list->what == UNDO_INSERT)
	{
	  start = TRANS (rl_undo_list->start);
	  end = TRANS (rl_undo_list->end);
	}

      switch (rl_undo_list->what)
	{
	/* Undoing deletes means inserting some text. */
	case UNDO_DELETE:
	  rl_point = start;
	  rl_insert_text (rl_undo_list->text);
	  free (rl_undo_list->text);
	  break;

	/* Undoing inserts means deleting some text. */
	case UNDO_INSERT:
	  rl_delete_text (start, end);
	  rl_point = start;
	  break;

	/* Undoing an END means undoing everything 'til we get to a BEGIN. */
	case UNDO_END:
	  waiting_for_begin++;
	  break;

	/* Undoing a BEGIN means that we are done with this group. */
	case UNDO_BEGIN:
	  if (waiting_for_begin)
	    waiting_for_begin--;
	  else
	    rl_ding ();
	  break;
	}

      _rl_doing_an_undo = 0;
      RL_UNSETSTATE(RL_STATE_UNDOING);

      release = rl_undo_list;
      rl_undo_list = rl_undo_list->next;
      free (release);
    }
  while (waiting_for_begin);

  return (1);
}
#undef TRANS

int
_rl_fix_last_undo_of_type (type, start, end)
     int type, start, end;
{
  UNDO_LIST *rl;

  for (rl = rl_undo_list; rl; rl = rl->next)
    {
      if (rl->what == type)
	{
	  rl->start = start;
	  rl->end = end;
	  return 0;
	}
    }
  return 1;
}

/* Begin a group.  Subsequent undos are undone as an atomic operation. */
int
rl_begin_undo_group ()
{
  rl_add_undo (UNDO_BEGIN, 0, 0, 0);
  _rl_undo_group_level++;
  return 0;
}

/* End an undo group started with rl_begin_undo_group (). */
int
rl_end_undo_group ()
{
  rl_add_undo (UNDO_END, 0, 0, 0);
  _rl_undo_group_level--;
  return 0;
}

/* Save an undo entry for the text from START to END. */
int
rl_modifying (start, end)
     int start, end;
{
  if (start > end)
    {
      SWAP (start, end);
    }

  if (start != end)
    {
      char *temp = rl_copy_text (start, end);
      rl_begin_undo_group ();
      rl_add_undo (UNDO_DELETE, start, end, temp);
      rl_add_undo (UNDO_INSERT, start, end, (char *)NULL);
      rl_end_undo_group ();
    }
  return 0;
}

/* Revert the current line to its previous state. */
int
rl_revert_line (count, key)
     int count, key;
{
  if (!rl_undo_list)
    rl_ding ();
  else
    {
      while (rl_undo_list)
	rl_do_undo ();
    }
  return 0;
}

/* Do some undoing of things that were done. */
int
rl_undo_command (count, key)
     int count, key;
{
  if (count < 0)
    return 0;	/* Nothing to do. */

  while (count)
    {
      if (rl_do_undo ())
	count--;
      else
	{
	  rl_ding ();
	  break;
	}
    }
  return 0;
}
