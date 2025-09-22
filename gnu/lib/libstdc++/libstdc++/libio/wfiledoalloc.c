/* Copyright (C) 1993, 1997, 1999, 2000 Free Software Foundation, Inc.
   This file is part of the GNU IO Library.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this library; see the file COPYING.  If not, write to
   the Free Software Foundation, 59 Temple Place - Suite 330, Boston,
   MA 02111-1307, USA.

   As a special exception, if you link this library with files
   compiled with a GNU compiler to produce an executable, this does
   not cause the resulting executable to be covered by the GNU General
   Public License.  This exception does not however invalidate any
   other reasons why the executable file might be covered by the GNU
   General Public License.  */

/*
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* Modified for GNU iostream by Per Bothner 1991, 1992. */

#ifndef _POSIX_SOURCE
# define _POSIX_SOURCE
#endif
#include "libioP.h"
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
#include <sys/types.h>
#include <sys/stat.h>
#ifdef __STDC__
#include <stdlib.h>
#include <unistd.h>
#endif

#ifdef _LIBC
# undef isatty
# define isatty(Fd) __isatty (Fd)
#endif

/*
 * Allocate a file buffer, or switch to unbuffered I/O.
 * Per the ANSI C standard, ALL tty devices default to line buffered.
 *
 * As a side effect, we set __SOPT or __SNPT (en/dis-able fseek
 * optimisation) right after the _fstat() that finds the buffer size.
 */

int
_IO_wfile_doallocate (fp)
     _IO_FILE *fp;
{
  _IO_size_t size;
  int couldbetty;
  wchar_t *p;
  struct _G_stat64 st;

  /* Allocate room for the external buffer.  */
  if (fp->_IO_buf_base == NULL)
    _IO_file_doallocate (fp);

  if (fp->_fileno < 0 || _IO_SYSSTAT (fp, &st) < 0)
    {
      couldbetty = 0;
      size = _IO_BUFSIZ;
#if 0
      /* do not try to optimise fseek() */
      fp->_flags |= __SNPT;
#endif
    }
  else
    {
      couldbetty = S_ISCHR (st.st_mode);
#if defined(_IO_HAVE_ST_BLKSIZE)
      size = st.st_blksize <= 0 ? _IO_BUFSIZ : st.st_blksize;
#else
      size = _IO_BUFSIZ;
#endif
    }
  ALLOC_WBUF (p, size * sizeof (wchar_t), EOF);
  _IO_wsetb (fp, p, p + size, 1);
  if (couldbetty && isatty (fp->_fileno))
    fp->_flags |= _IO_LINE_BUF;
  return 1;
}

#endif /* _GLIBCPP_USE_WCHAR_T */
