/* Copyright (C) 1993, 1997, 1998, 1999, 2000 Free Software Foundation, Inc.
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

#include "libioP.h"
#ifdef __STDC__
#include <stdlib.h>
#endif
#ifdef _LIBC
# include <shlib-compat.h>
#else
# define _IO_new_fopen fopen
#endif

_IO_FILE *
_IO_new_fopen (filename, mode)
     const char *filename;
     const char *mode;
{
  struct locked_FILE
  {
    struct _IO_FILE_plus fp;
#ifdef _IO_MTSAFE_IO
    _IO_lock_t lock;
#endif
#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    struct _IO_wide_data wd;
#endif /* !(defined _LIBC || defined _GLIBCPP_USE_WCHAR_T) */
  } *new_f = (struct locked_FILE *) malloc (sizeof (struct locked_FILE));

  if (new_f == NULL)
    return NULL;
#ifdef _IO_MTSAFE_IO
  new_f->fp.file._lock = &new_f->lock;
#endif
#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  _IO_no_init (&new_f->fp.file, 0, 0, &new_f->wd, &_IO_wfile_jumps);
#else
  _IO_no_init (&new_f->fp.file, 1, 0, NULL, NULL);
#endif
  _IO_JUMPS (&new_f->fp) = &_IO_file_jumps;
  _IO_file_init (&new_f->fp);
#if  !_IO_UNIFIED_JUMPTABLES
  new_f->fp.vtable = NULL;
#endif
  if (_IO_file_fopen ((_IO_FILE *) new_f, filename, mode, 1) != NULL)
    return (_IO_FILE *) &new_f->fp;
  _IO_un_link (&new_f->fp);
  free (new_f);
  return NULL;
}

#ifdef _LIBC
strong_alias (_IO_new_fopen, __new_fopen)
versioned_symbol (libc, _IO_new_fopen, _IO_fopen, GLIBC_2_1);
versioned_symbol (libc, __new_fopen, fopen, GLIBC_2_1);
#endif
