/* parens.c -- Implementation of matching parentheses feature. */

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

#include "rlconf.h"

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <stdio.h>
#include <sys/types.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#if defined (FD_SET) && !defined (HAVE_SELECT)
#  define HAVE_SELECT
#endif

#if defined (HAVE_SELECT)
#  include <sys/time.h>
#endif /* HAVE_SELECT */
#if defined (HAVE_SYS_SELECT_H)
#  include <sys/select.h>
#endif

#if defined (HAVE_STRING_H)
#  include <string.h>
#else /* !HAVE_STRING_H */
#  include <strings.h>
#endif /* !HAVE_STRING_H */

#if !defined (strchr) && !defined (__STDC__)
extern char *strchr (), *strrchr ();
#endif /* !strchr && !__STDC__ */

#include "readline.h"
#include "rlprivate.h"

static int find_matching_open PARAMS((char *, int, int));

/* Non-zero means try to blink the matching open parenthesis when the
   close parenthesis is inserted. */
#if defined (HAVE_SELECT)
int rl_blink_matching_paren = 1;
#else /* !HAVE_SELECT */
int rl_blink_matching_paren = 0;
#endif /* !HAVE_SELECT */

static int _paren_blink_usec = 500000;

/* Change emacs_standard_keymap to have bindings for paren matching when
   ON_OR_OFF is 1, change them back to self_insert when ON_OR_OFF == 0. */
void
_rl_enable_paren_matching (on_or_off)
     int on_or_off;
{
  if (on_or_off)
    {	/* ([{ */
      rl_bind_key_in_map (')', rl_insert_close, emacs_standard_keymap);
      rl_bind_key_in_map (']', rl_insert_close, emacs_standard_keymap);
      rl_bind_key_in_map ('}', rl_insert_close, emacs_standard_keymap);
    }
  else
    {	/* ([{ */
      rl_bind_key_in_map (')', rl_insert, emacs_standard_keymap);
      rl_bind_key_in_map (']', rl_insert, emacs_standard_keymap);
      rl_bind_key_in_map ('}', rl_insert, emacs_standard_keymap);
    }
}

int
rl_set_paren_blink_timeout (u)
     int u;
{
  int o;

  o = _paren_blink_usec;
  if (u > 0)
    _paren_blink_usec = u;
  return (o);
}

int
rl_insert_close (count, invoking_key)
     int count, invoking_key;
{
  if (rl_explicit_arg || !rl_blink_matching_paren)
    _rl_insert_char (count, invoking_key);
  else
    {
#if defined (HAVE_SELECT)
      int orig_point, match_point, ready;
      struct timeval timer;
      fd_set readfds;

      _rl_insert_char (1, invoking_key);
      (*rl_redisplay_function) ();
      match_point =
	find_matching_open (rl_line_buffer, rl_point - 2, invoking_key);

      /* Emacs might message or ring the bell here, but I don't. */
      if (match_point < 0)
	return -1;

      FD_ZERO (&readfds);
      FD_SET (fileno (rl_instream), &readfds);
      timer.tv_sec = 0;
      timer.tv_usec = _paren_blink_usec;

      orig_point = rl_point;
      rl_point = match_point;
      (*rl_redisplay_function) ();
      ready = select (1, &readfds, (fd_set *)NULL, (fd_set *)NULL, &timer);
      rl_point = orig_point;
#else /* !HAVE_SELECT */
      _rl_insert_char (count, invoking_key);
#endif /* !HAVE_SELECT */
    }
  return 0;
}

static int
find_matching_open (string, from, closer)
     char *string;
     int from, closer;
{
  register int i;
  int opener, level, delimiter;

  switch (closer)
    {
    case ']': opener = '['; break;
    case '}': opener = '{'; break;
    case ')': opener = '('; break;
    default:
      return (-1);
    }

  level = 1;			/* The closer passed in counts as 1. */
  delimiter = 0;		/* Delimited state unknown. */

  for (i = from; i > -1; i--)
    {
      if (delimiter && (string[i] == delimiter))
	delimiter = 0;
      else if (rl_basic_quote_characters && strchr (rl_basic_quote_characters, string[i]))
	delimiter = string[i];
      else if (!delimiter && (string[i] == closer))
	level++;
      else if (!delimiter && (string[i] == opener))
	level--;

      if (!level)
	break;
    }
  return (i);
}
