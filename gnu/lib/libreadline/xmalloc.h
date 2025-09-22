/* xmalloc.h -- memory allocation that aborts on errors. */

/* Copyright (C) 1999 Free Software Foundation, Inc.

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

#if !defined (_XMALLOC_H_)
#define _XMALLOC_H_

#if defined (READLINE_LIBRARY)
#  include "rlstdc.h"
#else
#  include <readline/rlstdc.h>
#endif

#include <stdio.h>

#ifndef PTR_T

#ifdef __STDC__
#  define PTR_T	void *
#else
#  define PTR_T	char *
#endif

#endif /* !PTR_T */

extern PTR_T xmalloc PARAMS((size_t));
extern PTR_T xrealloc PARAMS((void *, size_t));
extern void xfree PARAMS((void *));

static void
memory_error_and_abort (fname)
     char *fname;
{
  fprintf (stderr, "%s: out of virtual memory\n", fname);
  exit (2);
}

#endif /* _XMALLOC_H_ */
