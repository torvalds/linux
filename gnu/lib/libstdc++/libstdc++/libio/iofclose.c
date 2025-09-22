/* Copyright (C) 1993, 1995, 1997-1999, 2000 Free Software Foundation, Inc.
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
#if _LIBC
# include "../iconv/gconv_int.h"
# include <shlib-compat.h>
#else
# define SHLIB_COMPAT(a, b, c) 0
# define _IO_new_fclose fclose
#endif

int
_IO_new_fclose (fp)
     _IO_FILE *fp;
{
  int status;

  CHECK_FILE(fp, EOF);

#if SHLIB_COMPAT (libc, GLIBC_2_0, GLIBC_2_1)
  /* We desperately try to help programs which are using streams in a
     strange way and mix old and new functions.  Detect old streams
     here.  */
  if (fp->_vtable_offset != 0)
    return _IO_old_fclose (fp);
#endif

  _IO_cleanup_region_start ((void (*) __P ((void *))) _IO_funlockfile, fp);
  _IO_flockfile (fp);
  if (fp->_IO_file_flags & _IO_IS_FILEBUF)
    status = _IO_file_close_it (fp);
  else
    status = fp->_flags & _IO_ERR_SEEN ? -1 : 0;
  _IO_FINISH (fp);
  _IO_funlockfile (fp);
  if (fp->_mode > 0)
    {
#if _LIBC
      /* This stream has a wide orientation.  This means we have to free
	 the conversion functions.  */
      struct _IO_codecvt *cc = fp->_codecvt;

      if (cc->__cd_in.__cd.__steps->__shlib_handle != NULL)
	{
	  --cc->__cd_in.__cd.__steps->__counter;
	  __gconv_close_transform (cc->__cd_in.__cd.__steps, 1);
	}
      if (cc->__cd_out.__cd.__steps->__shlib_handle != NULL)
	{
	  --cc->__cd_out.__cd.__steps->__counter;
	  __gconv_close_transform (cc->__cd_out.__cd.__steps, 1);
	}
#endif
    }
  _IO_cleanup_region_end (0);
  if (_IO_have_backup (fp))
    _IO_free_backup_area (fp);
  if (fp != _IO_stdin && fp != _IO_stdout && fp != _IO_stderr)
    {
      fp->_IO_file_flags = 0;
      free(fp);
    }

  return status;
}

#ifdef _LIBC
versioned_symbol (libc, _IO_new_fclose, _IO_fclose, GLIBC_2_1);
strong_alias (_IO_new_fclose, __new_fclose)
versioned_symbol (libc, __new_fclose, fclose, GLIBC_2_1);
#endif
