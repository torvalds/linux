/* macro.c -- keyboard macros for readline. */

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
/*			Hacking Keyboard Macros			    */
/*								    */
/* **************************************************************** */

/* The currently executing macro string.  If this is non-zero,
   then it is a malloc ()'ed string where input is coming from. */
char *rl_executing_macro = (char *)NULL;

/* The offset in the above string to the next character to be read. */
static int executing_macro_index;

/* The current macro string being built.  Characters get stuffed
   in here by add_macro_char (). */
static char *current_macro = (char *)NULL;

/* The size of the buffer allocated to current_macro. */
static int current_macro_size;

/* The index at which characters are being added to current_macro. */
static int current_macro_index;

/* A structure used to save nested macro strings.
   It is a linked list of string/index for each saved macro. */
struct saved_macro {
  struct saved_macro *next;
  char *string;
  int sindex;
};

/* The list of saved macros. */
static struct saved_macro *macro_list = (struct saved_macro *)NULL;

/* Set up to read subsequent input from STRING.
   STRING is free ()'ed when we are done with it. */
void
_rl_with_macro_input (string)
     char *string;
{
  _rl_push_executing_macro ();
  rl_executing_macro = string;
  executing_macro_index = 0;
  RL_SETSTATE(RL_STATE_MACROINPUT);
}

/* Return the next character available from a macro, or 0 if
   there are no macro characters. */
int
_rl_next_macro_key ()
{
  if (rl_executing_macro == 0)
    return (0);

  if (rl_executing_macro[executing_macro_index] == 0)
    {
      _rl_pop_executing_macro ();
      return (_rl_next_macro_key ());
    }

  return (rl_executing_macro[executing_macro_index++]);
}

/* Save the currently executing macro on a stack of saved macros. */
void
_rl_push_executing_macro ()
{
  struct saved_macro *saver;

  saver = (struct saved_macro *)xmalloc (sizeof (struct saved_macro));
  saver->next = macro_list;
  saver->sindex = executing_macro_index;
  saver->string = rl_executing_macro;

  macro_list = saver;
}

/* Discard the current macro, replacing it with the one
   on the top of the stack of saved macros. */
void
_rl_pop_executing_macro ()
{
  struct saved_macro *macro;

  FREE (rl_executing_macro);
  rl_executing_macro = (char *)NULL;
  executing_macro_index = 0;

  if (macro_list)
    {
      macro = macro_list;
      rl_executing_macro = macro_list->string;
      executing_macro_index = macro_list->sindex;
      macro_list = macro_list->next;
      free (macro);
    }

  if (rl_executing_macro == 0)
    RL_UNSETSTATE(RL_STATE_MACROINPUT);
}

/* Add a character to the macro being built. */
void
_rl_add_macro_char (c)
     int c;
{
  if (current_macro_index + 1 >= current_macro_size)
    {
      if (current_macro == 0)
	current_macro = (char *)xmalloc (current_macro_size = 25);
      else
	current_macro = (char *)xrealloc (current_macro, current_macro_size += 25);
    }

  current_macro[current_macro_index++] = c;
  current_macro[current_macro_index] = '\0';
}

void
_rl_kill_kbd_macro ()
{
  if (current_macro)
    {
      free (current_macro);
      current_macro = (char *) NULL;
    }
  current_macro_size = current_macro_index = 0;

  FREE (rl_executing_macro);
  rl_executing_macro = (char *) NULL;
  executing_macro_index = 0;

  RL_UNSETSTATE(RL_STATE_MACRODEF);
}

/* Begin defining a keyboard macro.
   Keystrokes are recorded as they are executed.
   End the definition with rl_end_kbd_macro ().
   If a numeric argument was explicitly typed, then append this
   definition to the end of the existing macro, and start by
   re-executing the existing macro. */
int
rl_start_kbd_macro (ignore1, ignore2)
     int ignore1, ignore2;
{
  if (RL_ISSTATE (RL_STATE_MACRODEF))
    {
      _rl_abort_internal ();
      return -1;
    }

  if (rl_explicit_arg)
    {
      if (current_macro)
	_rl_with_macro_input (savestring (current_macro));
    }
  else
    current_macro_index = 0;

  RL_SETSTATE(RL_STATE_MACRODEF);
  return 0;
}

/* Stop defining a keyboard macro.
   A numeric argument says to execute the macro right now,
   that many times, counting the definition as the first time. */
int
rl_end_kbd_macro (count, ignore)
     int count, ignore;
{
  if (RL_ISSTATE (RL_STATE_MACRODEF) == 0)
    {
      _rl_abort_internal ();
      return -1;
    }

  current_macro_index -= rl_key_sequence_length - 1;
  current_macro[current_macro_index] = '\0';

  RL_UNSETSTATE(RL_STATE_MACRODEF);

  return (rl_call_last_kbd_macro (--count, 0));
}

/* Execute the most recently defined keyboard macro.
   COUNT says how many times to execute it. */
int
rl_call_last_kbd_macro (count, ignore)
     int count, ignore;
{
  if (current_macro == 0)
    _rl_abort_internal ();

  if (RL_ISSTATE (RL_STATE_MACRODEF))
    {
      rl_ding ();		/* no recursive macros */
      current_macro[--current_macro_index] = '\0';	/* erase this char */
      return 0;
    }

  while (count--)
    _rl_with_macro_input (savestring (current_macro));
  return 0;
}

void
rl_push_macro_input (macro)
     char *macro;
{
  _rl_with_macro_input (macro);
}
