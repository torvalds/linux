/* histlib.h -- internal definitions for the history library. */
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

#if !defined (_HISTLIB_H_)
#define _HISTLIB_H_

#if defined (HAVE_STRING_H)
#  include <string.h>
#else
#  include <strings.h>
#endif /* !HAVE_STRING_H */

#if !defined (STREQ)
#define STREQ(a, b)	(((a)[0] == (b)[0]) && (strcmp ((a), (b)) == 0))
#define STREQN(a, b, n) (((n) == 0) ? (1) \
				    : ((a)[0] == (b)[0]) && (strncmp ((a), (b), (n)) == 0))
#endif

#if !defined (savestring)
#include <stdio.h>
static char *
xstrdup(const char *s)
{
	char * cp;
	cp = strdup(s);
	if (cp == NULL) {
		fprintf (stderr, "xstrdup: out of virtual memory\n");
		exit (2);
	}
	return(cp);
}
#define savestring(x) xstrdup(x)
#endif /* !savestring */

#ifndef whitespace
#define whitespace(c) (((c) == ' ') || ((c) == '\t'))
#endif

#ifndef _rl_digit_p
#define _rl_digit_p(c)  ((c) >= '0' && (c) <= '9')
#endif

#ifndef _rl_digit_value
#define _rl_digit_value(c) ((c) - '0')
#endif

#ifndef member
#  ifndef strchr
extern char *strchr ();
#  endif
#define member(c, s) ((c) ? ((char *)strchr ((s), (c)) != (char *)NULL) : 0)
#endif

#ifndef FREE
#  define FREE(x)	if (x) free (x)
#endif

/* Possible history errors passed to hist_error. */
#define EVENT_NOT_FOUND 0
#define BAD_WORD_SPEC	1
#define SUBST_FAILED	2
#define BAD_MODIFIER	3
#define NO_PREV_SUBST	4

/* Possible definitions for history starting point specification. */
#define ANCHORED_SEARCH 1
#define NON_ANCHORED_SEARCH 0

/* Possible definitions for what style of writing the history file we want. */
#define HISTORY_APPEND 0
#define HISTORY_OVERWRITE 1

/* Some variable definitions shared across history source files. */
extern int history_offset;

#endif /* !_HISTLIB_H_ */
