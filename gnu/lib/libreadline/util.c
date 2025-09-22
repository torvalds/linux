/* util.c -- readline utility functions */

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
#include <fcntl.h>
#include "posixjmp.h"

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>           /* for _POSIX_VERSION */
#endif /* HAVE_UNISTD_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include <stdio.h>
#include <ctype.h>

/* System-specific feature definitions and include files. */
#include "rldefs.h"

#if defined (TIOCSTAT_IN_SYS_IOCTL)
#  include <sys/ioctl.h>
#endif /* TIOCSTAT_IN_SYS_IOCTL */

/* Some standard library routines. */
#include "readline.h"

#include "rlprivate.h"
#include "xmalloc.h"

/* **************************************************************** */
/*								    */
/*			Utility Functions			    */
/*								    */
/* **************************************************************** */

/* Return 0 if C is not a member of the class of characters that belong
   in words, or 1 if it is. */

int _rl_allow_pathname_alphabetic_chars = 0;
static const char *pathname_alphabetic_chars = "/-_=~.#$";

int
rl_alphabetic (c)
     int c;
{
  if (ALPHABETIC (c))
    return (1);

  return (_rl_allow_pathname_alphabetic_chars &&
	    strchr (pathname_alphabetic_chars, c) != NULL);
}

/* How to abort things. */
int
_rl_abort_internal ()
{
  rl_ding ();
  rl_clear_message ();
  _rl_init_argument ();
  rl_clear_pending_input ();

  RL_UNSETSTATE (RL_STATE_MACRODEF);
  while (rl_executing_macro)
    _rl_pop_executing_macro ();

  rl_last_func = (rl_command_func_t *)NULL;
  longjmp (readline_top_level, 1);
  return (0);
}

int
rl_abort (count, key)
     int count, key;
{
  return (_rl_abort_internal ());
}

int
rl_tty_status (count, key)
     int count, key;
{
#if defined (TIOCSTAT)
  ioctl (1, TIOCSTAT, (char *)0);
  rl_refresh_line (count, key);
#else
  rl_ding ();
#endif
  return 0;
}

/* Return a copy of the string between FROM and TO.
   FROM is inclusive, TO is not. */
char *
rl_copy_text (from, to)
     int from, to;
{
  register int length;
  char *copy;

  /* Fix it if the caller is confused. */
  if (from > to)
    SWAP (from, to);

  length = to - from;
  copy = (char *)xmalloc (1 + length);
  strncpy (copy, rl_line_buffer + from, length);
  copy[length] = '\0';
  return (copy);
}

/* Increase the size of RL_LINE_BUFFER until it has enough space to hold
   LEN characters. */
void
rl_extend_line_buffer (len)
     int len;
{
  while (len >= rl_line_buffer_len)
    {
      rl_line_buffer_len += DEFAULT_BUFFER_SIZE;
      rl_line_buffer = (char *)xrealloc (rl_line_buffer, rl_line_buffer_len);
    }

  _rl_set_the_line ();
}


/* A function for simple tilde expansion. */
int
rl_tilde_expand (ignore, key)
     int ignore, key;
{
  register int start, end;
  char *homedir, *temp;
  int len;

  end = rl_point;
  start = end - 1;

  if (rl_point == rl_end && rl_line_buffer[rl_point] == '~')
    {
      homedir = tilde_expand ("~");
      _rl_replace_text (homedir, start, end);
      return (0);
    }
  else if (rl_line_buffer[start] != '~')
    {
      for (; !whitespace (rl_line_buffer[start]) && start >= 0; start--)
        ;
      start++;
    }

  end = start;
  do
    end++;
  while (whitespace (rl_line_buffer[end]) == 0 && end < rl_end);

  if (whitespace (rl_line_buffer[end]) || end >= rl_end)
    end--;

  /* If the first character of the current word is a tilde, perform
     tilde expansion and insert the result.  If not a tilde, do
     nothing. */
  if (rl_line_buffer[start] == '~')
    {
      len = end - start + 1;
      temp = (char *)xmalloc (len + 1);
      strncpy (temp, rl_line_buffer + start, len);
      temp[len] = '\0';
      homedir = tilde_expand (temp);
      free (temp);

      _rl_replace_text (homedir, start, end);
    }

  return (0);
}

/* **************************************************************** */
/*								    */
/*			String Utility Functions		    */
/*								    */
/* **************************************************************** */

/* Determine if s2 occurs in s1.  If so, return a pointer to the
   match in s1.  The compare is case insensitive. */
char *
_rl_strindex (s1, s2)
     register const char *s1, *s2;
{
  register int i, l, len;

  for (i = 0, l = strlen (s2), len = strlen (s1); (len - i) >= l; i++)
    if (_rl_strnicmp (s1 + i, s2, l) == 0)
      return ((char *) (s1 + i));
  return ((char *)NULL);
}

#ifndef HAVE_STRPBRK
/* Find the first occurrence in STRING1 of any character from STRING2.
   Return a pointer to the character in STRING1. */
char *
_rl_strpbrk (string1, string2)
     const char *string1, *string2;
{
  register const char *scan;
#if defined (HANDLE_MULTIBYTE)
  mbstate_t ps;
  register int i, v;

  memset (&ps, 0, sizeof (mbstate_t));
#endif

  for (; *string1; string1++)
    {
      for (scan = string2; *scan; scan++)
	{
	  if (*string1 == *scan)
	    return ((char *)string1);
	}
#if defined (HANDLE_MULTIBYTE)
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  v = _rl_get_char_len (string1, &ps);
	  if (v > 1)
	    string += v - 1;	/* -1 to account for auto-increment in loop */
	}
#endif
    }
  return ((char *)NULL);
}
#endif

#if !defined (HAVE_STRCASECMP)
/* Compare at most COUNT characters from string1 to string2.  Case
   doesn't matter. */
int
_rl_strnicmp (string1, string2, count)
     char *string1, *string2;
     int count;
{
  register char ch1, ch2;

  while (count)
    {
      ch1 = *string1++;
      ch2 = *string2++;
      if (_rl_to_upper(ch1) == _rl_to_upper(ch2))
	count--;
      else
        break;
    }
  return (count);
}

/* strcmp (), but caseless. */
int
_rl_stricmp (string1, string2)
     char *string1, *string2;
{
  register char ch1, ch2;

  while (*string1 && *string2)
    {
      ch1 = *string1++;
      ch2 = *string2++;
      if (_rl_to_upper(ch1) != _rl_to_upper(ch2))
	return (1);
    }
  return (*string1 - *string2);
}
#endif /* !HAVE_STRCASECMP */

/* Stupid comparison routine for qsort () ing strings. */
int
_rl_qsort_string_compare (s1, s2)
  char **s1, **s2;
{
#if defined (HAVE_STRCOLL)
  return (strcoll (*s1, *s2));
#else
  int result;

  result = **s1 - **s2;
  if (result == 0)
    result = strcmp (*s1, *s2);

  return result;
#endif
}

/* Function equivalents for the macros defined in chardefs.h. */
#define FUNCTION_FOR_MACRO(f)	int (f) (c) int c; { return f (c); }

FUNCTION_FOR_MACRO (_rl_digit_p)
FUNCTION_FOR_MACRO (_rl_digit_value)
FUNCTION_FOR_MACRO (_rl_lowercase_p)
FUNCTION_FOR_MACRO (_rl_pure_alphabetic)
FUNCTION_FOR_MACRO (_rl_to_lower)
FUNCTION_FOR_MACRO (_rl_to_upper)
FUNCTION_FOR_MACRO (_rl_uppercase_p)

/* Backwards compatibility, now that savestring has been removed from
   all `public' readline header files. */
#undef _rl_savestring
char *
_rl_savestring (s)
     const char *s;
{
	char *cp;
	cp = strdup(s);
	if (cp == NULL)
		memory_error_and_abort ("savestring");
	return(cp);
}
