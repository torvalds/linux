/* mbutil.c -- readline multibyte character utility functions */

/* Copyright (C) 2001 Free Software Foundation, Inc.

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
#  include <unistd.h>	   /* for _POSIX_VERSION */
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
#include "rlmbutil.h"

#if defined (TIOCSTAT_IN_SYS_IOCTL)
#  include <sys/ioctl.h>
#endif /* TIOCSTAT_IN_SYS_IOCTL */

/* Some standard library routines. */
#include "readline.h"

#include "rlprivate.h"
#include "xmalloc.h"

/* Declared here so it can be shared between the readline and history
   libraries. */
#if defined (HANDLE_MULTIBYTE)
int rl_byte_oriented = 0;
#else
int rl_byte_oriented = 1;
#endif

/* **************************************************************** */
/*								    */
/*		Multibyte Character Utility Functions		    */
/*								    */
/* **************************************************************** */

#if defined(HANDLE_MULTIBYTE)

static int
_rl_find_next_mbchar_internal (string, seed, count, find_non_zero)
     char *string;
     int seed, count, find_non_zero;
{
  size_t tmp = 0;
  mbstate_t ps;
  int point = 0;
  wchar_t wc;

  memset(&ps, 0, sizeof (mbstate_t));
  if (seed < 0)
    seed = 0;
  if (count <= 0)
    return seed;

  point = seed + _rl_adjust_point(string, seed, &ps);
  /* if this is true, means that seed was not pointed character
     started byte.  So correct the point and consume count */
  if (seed < point)
    count --;

  while (count > 0)
    {
      tmp = mbrtowc (&wc, string+point, strlen(string + point), &ps);
      if ((size_t)(tmp) == (size_t)-1 || (size_t)(tmp) == (size_t)-2)
	{
	  /* invalid bytes. asume a byte represents a character */
	  point++;
	  count--;
	  /* reset states. */
	  memset(&ps, 0, sizeof(mbstate_t));
	}
      else if (tmp == (size_t)0)
	/* found '\0' char */
	break;
      else
	{
	  /* valid bytes */
	  point += tmp;
	  if (find_non_zero)
	    {
	      if (wcwidth (wc) == 0)
		continue;
	      else
		count--;
	    }
	  else
	    count--;
	}
    }

  if (find_non_zero)
    {
      tmp = mbrtowc (&wc, string + point, strlen (string + point), &ps);
      while (wcwidth (wc) == 0)
	{
	  point += tmp;
	  tmp = mbrtowc (&wc, string + point, strlen (string + point), &ps);
	  if (tmp == (size_t)(0) || tmp == (size_t)(-1) || tmp == (size_t)(-2))
	    break;
	}
    }
    return point;
}

static int
_rl_find_prev_mbchar_internal (string, seed, find_non_zero)
     char *string;
     int seed, find_non_zero;
{
  mbstate_t ps;
  int prev, non_zero_prev, point, length;
  size_t tmp;
  wchar_t wc;

  memset(&ps, 0, sizeof(mbstate_t));
  length = strlen(string);

  if (seed < 0)
    return 0;
  else if (length < seed)
    return length;

  prev = non_zero_prev = point = 0;
  while (point < seed)
    {
      tmp = mbrtowc (&wc, string + point, length - point, &ps);
      if ((size_t)(tmp) == (size_t)-1 || (size_t)(tmp) == (size_t)-2)
	{
	  /* in this case, bytes are invalid or shorted to compose
	     multibyte char, so assume that the first byte represents
	     a single character anyway. */
	  tmp = 1;
	  /* clear the state of the byte sequence, because
	     in this case effect of mbstate is undefined  */
	  memset(&ps, 0, sizeof (mbstate_t));
	}
      else if (tmp == 0)
	break;			/* Found '\0' char.  Can this happen? */
      else
	{
	  if (find_non_zero)
	    {
	      if (wcwidth (wc) != 0)
		prev = point;
	    }
	  else
	    prev = point;
	}

      point += tmp;
    }

  return prev;
}

/* return the number of bytes parsed from the multibyte sequence starting
   at src, if a non-L'\0' wide character was recognized. It returns 0,
   if a L'\0' wide character was recognized. It  returns (size_t)(-1),
   if an invalid multibyte sequence was encountered. It returns (size_t)(-2)
   if it couldn't parse a complete  multibyte character.  */
int
_rl_get_char_len (src, ps)
     char *src;
     mbstate_t *ps;
{
  size_t tmp;

  tmp = mbrlen((const char *)src, (size_t)strlen (src), ps);
  if (tmp == (size_t)(-2))
    {
      /* shorted to compose multibyte char */
      if (ps)
	memset (ps, 0, sizeof(mbstate_t));
      return -2;
    }
  else if (tmp == (size_t)(-1))
    {
      /* invalid to compose multibyte char */
      /* initialize the conversion state */
      if (ps)
	memset (ps, 0, sizeof(mbstate_t));
      return -1;
    }
  else if (tmp == (size_t)0)
    return 0;
  else
    return (int)tmp;
}

/* compare the specified two characters. If the characters matched,
   return 1. Otherwise return 0. */
int
_rl_compare_chars (buf1, pos1, ps1, buf2, pos2, ps2)
     char *buf1;
     int pos1;
     mbstate_t *ps1;
     char *buf2;
     int pos2;
     mbstate_t *ps2;
{
  int i, w1, w2;

  if ((w1 = _rl_get_char_len (&buf1[pos1], ps1)) <= 0 ||
	(w2 = _rl_get_char_len (&buf2[pos2], ps2)) <= 0 ||
	(w1 != w2) ||
	(buf1[pos1] != buf2[pos2]))
    return 0;

  for (i = 1; i < w1; i++)
    if (buf1[pos1+i] != buf2[pos2+i])
      return 0;

  return 1;
}

/* adjust pointed byte and find mbstate of the point of string.
   adjusted point will be point <= adjusted_point, and returns
   differences of the byte(adjusted_point - point).
   if point is invalied (point < 0 || more than string length),
   it returns -1 */
int
_rl_adjust_point(string, point, ps)
     char *string;
     int point;
     mbstate_t *ps;
{
  size_t tmp = 0;
  int length;
  int pos = 0;

  length = strlen(string);
  if (point < 0)
    return -1;
  if (length < point)
    return -1;

  while (pos < point)
    {
      tmp = mbrlen (string + pos, length - pos, ps);
      if((size_t)(tmp) == (size_t)-1 || (size_t)(tmp) == (size_t)-2)
	{
	  /* in this case, bytes are invalid or shorted to compose
	     multibyte char, so assume that the first byte represents
	     a single character anyway. */
	  pos++;
	  /* clear the state of the byte sequence, because
	     in this case effect of mbstate is undefined  */
	  if (ps)
	    memset (ps, 0, sizeof (mbstate_t));
	}
      else if (tmp == 0)
	pos++;
      else
	pos += tmp;
    }

  return (pos - point);
}

int
_rl_is_mbchar_matched (string, seed, end, mbchar, length)
     char *string;
     int seed, end;
     char *mbchar;
     int length;
{
  int i;

  if ((end - seed) < length)
    return 0;

  for (i = 0; i < length; i++)
    if (string[seed + i] != mbchar[i])
      return 0;
  return 1;
}
#endif /* HANDLE_MULTIBYTE */

/* Find next `count' characters started byte point of the specified seed.
   If flags is MB_FIND_NONZERO, we look for non-zero-width multibyte
   characters. */
#undef _rl_find_next_mbchar
int
_rl_find_next_mbchar (string, seed, count, flags)
     char *string;
     int seed, count, flags;
{
#if defined (HANDLE_MULTIBYTE)
  return _rl_find_next_mbchar_internal (string, seed, count, flags);
#else
  return (seed + count);
#endif
}

/* Find previous character started byte point of the specified seed.
   Returned point will be point <= seed.  If flags is MB_FIND_NONZERO,
   we look for non-zero-width multibyte characters. */
#undef _rl_find_prev_mbchar
int
_rl_find_prev_mbchar (string, seed, flags)
     char *string;
     int seed, flags;
{
#if defined (HANDLE_MULTIBYTE)
  return _rl_find_prev_mbchar_internal (string, seed, flags);
#else
  return ((seed == 0) ? seed : seed - 1);
#endif
}
