/* Copyright (C) 1993, 1995, 1997-1999, 2000 Free Software Foundation, Inc.
   This file is part of the GNU IO Library.
   Written by Per Bothner <bothner@cygnus.com>.

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


#ifndef _POSIX_SOURCE
# define _POSIX_SOURCE
#endif
#include "libioP.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#ifdef __STDC__
#include <stdlib.h>
#endif
#if _LIBC
# include "../wcsmbs/wcsmbsload.h"
# include <shlib-compat.h>
#endif
#ifndef errno
extern int errno;
#endif
#ifndef __set_errno
# define __set_errno(Val) errno = (Val)
#endif


#ifdef _LIBC
# define open(Name, Flags, Prot) __open (Name, Flags, Prot)
# define close(FD) __close (FD)
# define lseek(FD, Offset, Whence) __lseek (FD, Offset, Whence)
# define read(FD, Buf, NBytes) __read (FD, Buf, NBytes)
# define write(FD, Buf, NBytes) __write (FD, Buf, NBytes)
#else
# define _IO_new_do_write _IO_do_write
# define _IO_new_file_attach _IO_file_attach
# define _IO_new_file_close_it _IO_file_close_it
# define _IO_new_file_finish _IO_file_finish
# define _IO_new_file_fopen _IO_file_fopen
# define _IO_new_file_init _IO_file_init
# define _IO_new_file_setbuf _IO_file_setbuf
# define _IO_new_file_sync _IO_file_sync
# define _IO_new_file_overflow _IO_file_overflow
# define _IO_new_file_seekoff _IO_file_seekoff
# define _IO_new_file_underflow _IO_file_underflow
# define _IO_new_file_write _IO_file_write
# define _IO_new_file_xsputn _IO_file_xsputn
#endif

/* An fstream can be in at most one of put mode, get mode, or putback mode.
   Putback mode is a variant of get mode.

   In a filebuf, there is only one current position, instead of two
   separate get and put pointers.  In get mode, the current position
   is that of gptr(); in put mode that of pptr().

   The position in the buffer that corresponds to the position
   in external file system is normally _IO_read_end, except in putback
   mode, when it is _IO_save_end.
   If the field _fb._offset is >= 0, it gives the offset in
   the file as a whole corresponding to eGptr(). (?)

   PUT MODE:
   If a filebuf is in put mode, then all of _IO_read_ptr, _IO_read_end,
   and _IO_read_base are equal to each other.  These are usually equal
   to _IO_buf_base, though not necessarily if we have switched from
   get mode to put mode.  (The reason is to maintain the invariant
   that _IO_read_end corresponds to the external file position.)
   _IO_write_base is non-NULL and usually equal to _IO_base_base.
   We also have _IO_write_end == _IO_buf_end, but only in fully buffered mode.
   The un-flushed character are those between _IO_write_base and _IO_write_ptr.

   GET MODE:
   If a filebuf is in get or putback mode, eback() != egptr().
   In get mode, the unread characters are between gptr() and egptr().
   The OS file position corresponds to that of egptr().

   PUTBACK MODE:
   Putback mode is used to remember "excess" characters that have
   been sputbackc'd in a separate putback buffer.
   In putback mode, the get buffer points to the special putback buffer.
   The unread characters are the characters between gptr() and egptr()
   in the putback buffer, as well as the area between save_gptr()
   and save_egptr(), which point into the original reserve buffer.
   (The pointers save_gptr() and save_egptr() are the values
   of gptr() and egptr() at the time putback mode was entered.)
   The OS position corresponds to that of save_egptr().

   LINE BUFFERED OUTPUT:
   During line buffered output, _IO_write_base==base() && epptr()==base().
   However, ptr() may be anywhere between base() and ebuf().
   This forces a call to filebuf::overflow(int C) on every put.
   If there is more space in the buffer, and C is not a '\n',
   then C is inserted, and pptr() incremented.

   UNBUFFERED STREAMS:
   If a filebuf is unbuffered(), the _shortbuf[1] is used as the buffer.
*/

#define CLOSED_FILEBUF_FLAGS \
  (_IO_IS_FILEBUF+_IO_NO_READS+_IO_NO_WRITES+_IO_TIED_PUT_GET)


void
_IO_new_file_init (fp)
     struct _IO_FILE_plus *fp;
{
  /* POSIX.1 allows another file handle to be used to change the position
     of our file descriptor.  Hence we actually don't know the actual
     position before we do the first fseek (and until a following fflush). */
  fp->file._offset = _IO_pos_BAD;
  fp->file._IO_file_flags |= CLOSED_FILEBUF_FLAGS;

  _IO_link_in (fp);
  fp->file._fileno = -1;
}

int
_IO_new_file_close_it (fp)
     _IO_FILE *fp;
{
  int write_status, close_status;
  if (!_IO_file_is_open (fp))
    return EOF;

  write_status = _IO_do_flush (fp);

  _IO_unsave_markers(fp);

  close_status = _IO_SYSCLOSE (fp);

  /* Free buffer. */
  if (fp->_mode <= 0)
    {
      _IO_setb (fp, NULL, NULL, 0);
      _IO_setg (fp, NULL, NULL, NULL);
      _IO_setp (fp, NULL, NULL);
    }
#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  else
    {
      _IO_wsetb (fp, NULL, NULL, 0);
      _IO_wsetg (fp, NULL, NULL, NULL);
      _IO_wsetp (fp, NULL, NULL);
    }
#endif

  _IO_un_link ((struct _IO_FILE_plus *) fp);
  fp->_flags = _IO_MAGIC|CLOSED_FILEBUF_FLAGS;
  fp->_fileno = -1;
  fp->_offset = _IO_pos_BAD;

  return close_status ? close_status : write_status;
}

void
_IO_new_file_finish (fp, dummy)
     _IO_FILE *fp;
     int dummy;
{
  if (_IO_file_is_open (fp))
    {
      _IO_do_flush (fp);
      if (!(fp->_flags & _IO_DELETE_DONT_CLOSE))
	_IO_SYSCLOSE (fp);
    }
  _IO_default_finish (fp, 0);
}

#if defined __GNUC__ && __GNUC__ >= 2
__inline__
#endif
_IO_FILE *
_IO_file_open (fp, filename, posix_mode, prot, read_write, is32not64)
     _IO_FILE *fp;
     const char *filename;
     int posix_mode;
     int prot;
     int read_write;
     int is32not64;
{
  int fdesc;
#ifdef _G_OPEN64
  fdesc = (is32not64
	   ? open (filename, posix_mode, prot)
	   : _G_OPEN64 (filename, posix_mode, prot));
#else
  fdesc = open (filename, posix_mode, prot);
#endif
  if (fdesc < 0)
    return NULL;
  fp->_fileno = fdesc;
  _IO_mask_flags (fp, read_write,_IO_NO_READS+_IO_NO_WRITES+_IO_IS_APPENDING);
  if (read_write & _IO_IS_APPENDING)
    if (_IO_SEEKOFF (fp, (_IO_off64_t)0, _IO_seek_end, _IOS_INPUT|_IOS_OUTPUT)
	== _IO_pos_BAD && errno != ESPIPE)
      return NULL;
  _IO_link_in ((struct _IO_FILE_plus *) fp);
  return fp;
}

_IO_FILE *
_IO_new_file_fopen (fp, filename, mode, is32not64)
     _IO_FILE *fp;
     const char *filename;
     const char *mode;
     int is32not64;
{
  int oflags = 0, omode;
  int read_write;
  int oprot = 0666;
  int i;
  _IO_FILE *result;
#if _LIBC
  const char *cs;
#endif

  if (_IO_file_is_open (fp))
    return 0;
  switch (*mode)
    {
    case 'r':
      omode = O_RDONLY;
      read_write = _IO_NO_WRITES;
      break;
    case 'w':
      omode = O_WRONLY;
      oflags = O_CREAT|O_TRUNC;
      read_write = _IO_NO_READS;
      break;
    case 'a':
      omode = O_WRONLY;
      oflags = O_CREAT|O_APPEND;
      read_write = _IO_NO_READS|_IO_IS_APPENDING;
      break;
    default:
      __set_errno (EINVAL);
      return NULL;
    }
  for (i = 1; i < 4; ++i)
    {
      switch (*++mode)
	{
	case '\0':
	  break;
	case '+':
	  omode = O_RDWR;
	  read_write &= _IO_IS_APPENDING;
	  continue;
	case 'x':
	  oflags |= O_EXCL;
	  continue;
	case 'b':
	default:
	  /* Ignore.  */
	  continue;
	}
      break;
    }

  result = _IO_file_open (fp, filename, omode|oflags, oprot, read_write,
			  is32not64);


#if _LIBC
  /* Test whether the mode string specifies the conversion.  */
  cs = strstr (mode, ",ccs=");
  if (cs != NULL)
    {
      /* Yep.  Load the appropriate conversions and set the orientation
	 to wide.  */
	struct gconv_fcts fcts;
	struct _IO_codecvt *cc;

	if (! _IO_CHECK_WIDE (fp) || __wcsmbs_named_conv (&fcts, cs + 5) != 0)
	  {
	    /* Something went wrong, we cannot load the conversion modules.
	       This means we cannot proceed since the user explicitly asked
	       for these.  */
	    _IO_new_fclose (result);
	    return NULL;
	  }

	cc = fp->_codecvt = &fp->_wide_data->_codecvt;

	/* The functions are always the same.  */
	*cc = __libio_codecvt;

	cc->__cd_in.__cd.__nsteps = 1; /* Only one step allowed.  */
	cc->__cd_in.__cd.__steps = fcts.towc;

	cc->__cd_in.__cd.__data[0].__invocation_counter = 0;
	cc->__cd_in.__cd.__data[0].__internal_use = 1;
	cc->__cd_in.__cd.__data[0].__flags = __GCONV_IS_LAST;
	cc->__cd_in.__cd.__data[0].__statep = &result->_wide_data->_IO_state;

	cc->__cd_out.__cd.__nsteps = 1; /* Only one step allowed.  */
	cc->__cd_out.__cd.__steps = fcts.tomb;

	cc->__cd_out.__cd.__data[0].__invocation_counter = 0;
	cc->__cd_out.__cd.__data[0].__internal_use = 1;
	cc->__cd_out.__cd.__data[0].__flags = __GCONV_IS_LAST;
	cc->__cd_out.__cd.__data[0].__statep = &result->_wide_data->_IO_state;

	/* Set the mode now.  */
	result->_mode = 1;
    }
#endif	/* GNU libc */

  return result;
}

_IO_FILE *
_IO_new_file_attach (fp, fd)
     _IO_FILE *fp;
     int fd;
{
  if (_IO_file_is_open (fp))
    return NULL;
  fp->_fileno = fd;
  fp->_flags &= ~(_IO_NO_READS+_IO_NO_WRITES);
  fp->_flags |= _IO_DELETE_DONT_CLOSE;
  /* Get the current position of the file. */
  /* We have to do that since that may be junk. */
  fp->_offset = _IO_pos_BAD;
  if (_IO_SEEKOFF (fp, (_IO_off64_t)0, _IO_seek_cur, _IOS_INPUT|_IOS_OUTPUT)
      == _IO_pos_BAD && errno != ESPIPE)
    return NULL;
  return fp;
}

_IO_FILE *
_IO_new_file_setbuf (fp, p, len)
     _IO_FILE *fp;
     char *p;
     _IO_ssize_t len;
{
  if (_IO_default_setbuf (fp, p, len) == NULL)
    return NULL;

  fp->_IO_write_base = fp->_IO_write_ptr = fp->_IO_write_end
    = fp->_IO_buf_base;
  _IO_setg (fp, fp->_IO_buf_base, fp->_IO_buf_base, fp->_IO_buf_base);

  return fp;
}

static int new_do_write __P ((_IO_FILE *, const char *, _IO_size_t));

/* Write TO_DO bytes from DATA to FP.
   Then mark FP as having empty buffers. */

int
_IO_new_do_write (fp, data, to_do)
     _IO_FILE *fp;
     const char *data;
     _IO_size_t to_do;
{
  return (to_do == 0 || new_do_write (fp, data, to_do) == to_do) ? 0 : EOF;
}

static
int
new_do_write (fp, data, to_do)
     _IO_FILE *fp;
     const char *data;
     _IO_size_t to_do;
{
  _IO_size_t count;
  if (fp->_flags & _IO_IS_APPENDING)
    /* On a system without a proper O_APPEND implementation,
       you would need to sys_seek(0, SEEK_END) here, but is
       is not needed nor desirable for Unix- or Posix-like systems.
       Instead, just indicate that offset (before and after) is
       unpredictable. */
    fp->_offset = _IO_pos_BAD;
  else if (fp->_IO_read_end != fp->_IO_write_base)
    {
      _IO_off64_t new_pos
	= _IO_SYSSEEK (fp, fp->_IO_write_base - fp->_IO_read_end, 1);
      if (new_pos == _IO_pos_BAD)
	return 0;
      fp->_offset = new_pos;
    }
  count = _IO_SYSWRITE (fp, data, to_do);
  if (fp->_cur_column && count)
    fp->_cur_column = _IO_adjust_column (fp->_cur_column - 1, data, count) + 1;
  _IO_setg (fp, fp->_IO_buf_base, fp->_IO_buf_base, fp->_IO_buf_base);
  fp->_IO_write_base = fp->_IO_write_ptr = fp->_IO_buf_base;
  fp->_IO_write_end = (fp->_mode < 0
		       && (fp->_flags & (_IO_LINE_BUF+_IO_UNBUFFERED))
		       ? fp->_IO_buf_base : fp->_IO_buf_end);
  return count;
}

int
_IO_new_file_underflow (fp)
     _IO_FILE *fp;
{
  _IO_ssize_t count;
#if 0
  /* SysV does not make this test; take it out for compatibility */
  if (fp->_flags & _IO_EOF_SEEN)
    return (EOF);
#endif

  if (fp->_flags & _IO_NO_READS)
    {
      fp->_flags |= _IO_ERR_SEEN;
      __set_errno (EBADF);
      return EOF;
    }
  if (fp->_IO_read_ptr < fp->_IO_read_end)
    return *(unsigned char *) fp->_IO_read_ptr;

  if (fp->_IO_buf_base == NULL)
    {
      /* Maybe we already have a push back pointer.  */
      if (fp->_IO_save_base != NULL)
	{
	  free (fp->_IO_save_base);
	  fp->_flags &= ~_IO_IN_BACKUP;
	}
      _IO_doallocbuf (fp);
    }

  /* Flush all line buffered files before reading. */
  /* FIXME This can/should be moved to genops ?? */
  if (fp->_flags & (_IO_LINE_BUF|_IO_UNBUFFERED))
    _IO_flush_all_linebuffered ();

  _IO_switch_to_get_mode (fp);

  /* This is very tricky. We have to adjust those
     pointers before we call _IO_SYSREAD () since
     we may longjump () out while waiting for
     input. Those pointers may be screwed up. H.J. */
  fp->_IO_read_base = fp->_IO_read_ptr = fp->_IO_buf_base;
  fp->_IO_read_end = fp->_IO_buf_base;
  fp->_IO_write_base = fp->_IO_write_ptr = fp->_IO_write_end
    = fp->_IO_buf_base;

  count = _IO_SYSREAD (fp, fp->_IO_buf_base,
		       fp->_IO_buf_end - fp->_IO_buf_base);
  if (count <= 0)
    {
      if (count == 0)
	fp->_flags |= _IO_EOF_SEEN;
      else
	fp->_flags |= _IO_ERR_SEEN, count = 0;
  }
  fp->_IO_read_end += count;
  if (count == 0)
    return EOF;
  if (fp->_offset != _IO_pos_BAD)
    _IO_pos_adjust (fp->_offset, count);
  return *(unsigned char *) fp->_IO_read_ptr;
}

int
_IO_new_file_overflow (f, ch)
      _IO_FILE *f;
      int ch;
{
  if (f->_flags & _IO_NO_WRITES) /* SET ERROR */
    {
      f->_flags |= _IO_ERR_SEEN;
      __set_errno (EBADF);
      return EOF;
    }
  /* If currently reading or no buffer allocated. */
  if ((f->_flags & _IO_CURRENTLY_PUTTING) == 0 || f->_IO_write_base == 0)
    {
      /* Allocate a buffer if needed. */
      if (f->_IO_write_base == 0)
	{
	  _IO_doallocbuf (f);
	  _IO_setg (f, f->_IO_buf_base, f->_IO_buf_base, f->_IO_buf_base);
	}
      /* Otherwise must be currently reading.
	 If _IO_read_ptr (and hence also _IO_read_end) is at the buffer end,
	 logically slide the buffer forwards one block (by setting the
	 read pointers to all point at the beginning of the block).  This
	 makes room for subsequent output.
	 Otherwise, set the read pointers to _IO_read_end (leaving that
	 alone, so it can continue to correspond to the external position). */
      if (f->_IO_read_ptr == f->_IO_buf_end)
	f->_IO_read_end = f->_IO_read_ptr = f->_IO_buf_base;
      f->_IO_write_ptr = f->_IO_read_ptr;
      f->_IO_write_base = f->_IO_write_ptr;
      f->_IO_write_end = f->_IO_buf_end;
      f->_IO_read_base = f->_IO_read_ptr = f->_IO_read_end;

      f->_flags |= _IO_CURRENTLY_PUTTING;
      if (f->_mode < 0 && f->_flags & (_IO_LINE_BUF+_IO_UNBUFFERED))
	f->_IO_write_end = f->_IO_write_ptr;
    }
  if (ch == EOF)
    return _IO_new_do_write(f, f->_IO_write_base,
			    f->_IO_write_ptr - f->_IO_write_base);
  if (f->_IO_write_ptr == f->_IO_buf_end ) /* Buffer is really full */
    if (_IO_do_flush (f) == EOF)
      return EOF;
  *f->_IO_write_ptr++ = ch;
  if ((f->_flags & _IO_UNBUFFERED)
      || ((f->_flags & _IO_LINE_BUF) && ch == '\n'))
    if (_IO_new_do_write(f, f->_IO_write_base,
			 f->_IO_write_ptr - f->_IO_write_base) == EOF)
      return EOF;
  return (unsigned char) ch;
}

int
_IO_new_file_sync (fp)
     _IO_FILE *fp;
{
  _IO_ssize_t delta;
  int retval = 0;

  /*    char* ptr = cur_ptr(); */
  if (fp->_IO_write_ptr > fp->_IO_write_base)
    if (_IO_do_flush(fp)) return EOF;
  delta = fp->_IO_read_ptr - fp->_IO_read_end;
  if (delta != 0)
    {
#ifdef TODO
      if (_IO_in_backup (fp))
	delta -= eGptr () - Gbase ();
#endif
      _IO_off64_t new_pos = _IO_SYSSEEK (fp, delta, 1);
      if (new_pos != (_IO_off64_t) EOF)
	fp->_IO_read_end = fp->_IO_read_ptr;
#ifdef ESPIPE
      else if (errno == ESPIPE)
	; /* Ignore error from unseekable devices. */
#endif
      else
	retval = EOF;
    }
  if (retval != EOF)
    fp->_offset = _IO_pos_BAD;
  /* FIXME: Cleanup - can this be shared? */
  /*    setg(base(), ptr, ptr); */
  return retval;
}

_IO_off64_t
_IO_new_file_seekoff (fp, offset, dir, mode)
     _IO_FILE *fp;
     _IO_off64_t offset;
     int dir;
     int mode;
{
  _IO_off64_t result;
  _IO_off64_t delta, new_offset;
  long count;
  /* POSIX.1 8.2.3.7 says that after a call the fflush() the file
     offset of the underlying file must be exact.  */
  int must_be_exact = (fp->_IO_read_base == fp->_IO_read_end
		       && fp->_IO_write_base == fp->_IO_write_ptr);

  if (mode == 0)
    dir = _IO_seek_cur, offset = 0; /* Don't move any pointers. */

  /* Flush unwritten characters.
     (This may do an unneeded write if we seek within the buffer.
     But to be able to switch to reading, we would need to set
     egptr to ptr.  That can't be done in the current design,
     which assumes file_ptr() is eGptr.  Anyway, since we probably
     end up flushing when we close(), it doesn't make much difference.)
     FIXME: simulate mem-papped files. */

  if (fp->_IO_write_ptr > fp->_IO_write_base || _IO_in_put_mode (fp))
    if (_IO_switch_to_get_mode (fp))
      return EOF;

  if (fp->_IO_buf_base == NULL)
    {
      /* It could be that we already have a pushback buffer.  */
      if (fp->_IO_read_base != NULL)
	{
	  free (fp->_IO_read_base);
	  fp->_flags &= ~_IO_IN_BACKUP;
	}
      _IO_doallocbuf (fp);
      _IO_setp (fp, fp->_IO_buf_base, fp->_IO_buf_base);
      _IO_setg (fp, fp->_IO_buf_base, fp->_IO_buf_base, fp->_IO_buf_base);
    }

  switch (dir)
    {
    case _IO_seek_cur:
      /* Adjust for read-ahead (bytes is buffer). */
      offset -= fp->_IO_read_end - fp->_IO_read_ptr;
      if (fp->_offset == _IO_pos_BAD)
	goto dumb;
      /* Make offset absolute, assuming current pointer is file_ptr(). */
      offset += fp->_offset;

      dir = _IO_seek_set;
      break;
    case _IO_seek_set:
      break;
    case _IO_seek_end:
      {
	struct _G_stat64 st;
	if (_IO_SYSSTAT (fp, &st) == 0 && S_ISREG (st.st_mode))
	  {
	    offset += st.st_size;
	    dir = _IO_seek_set;
	  }
	else
	  goto dumb;
      }
    }
  /* At this point, dir==_IO_seek_set. */

  /* If we are only interested in the current position we've found it now.  */
  if (mode == 0)
    return offset;

  /* If destination is within current buffer, optimize: */
  if (fp->_offset != _IO_pos_BAD && fp->_IO_read_base != NULL
      && !_IO_in_backup (fp))
    {
      /* Offset relative to start of main get area. */
      _IO_off64_t rel_offset = (offset - fp->_offset
				+ (fp->_IO_read_end - fp->_IO_read_base));
      if (rel_offset >= 0)
	{
#if 0
	  if (_IO_in_backup (fp))
	    _IO_switch_to_main_get_area (fp);
#endif
	  if (rel_offset <= fp->_IO_read_end - fp->_IO_read_base)
	    {
	      _IO_setg (fp, fp->_IO_buf_base, fp->_IO_buf_base + rel_offset,
			fp->_IO_read_end);
	      _IO_setp (fp, fp->_IO_buf_base, fp->_IO_buf_base);
	      {
		_IO_mask_flags (fp, 0, _IO_EOF_SEEN);
		goto resync;
	      }
	    }
#ifdef TODO
	    /* If we have streammarkers, seek forward by reading ahead. */
	    if (_IO_have_markers (fp))
	      {
		int to_skip = rel_offset
		  - (fp->_IO_read_ptr - fp->_IO_read_base);
		if (ignore (to_skip) != to_skip)
		  goto dumb;
		_IO_mask_flags (fp, 0, _IO_EOF_SEEN);
		goto resync;
	      }
#endif
	}
#ifdef TODO
      if (rel_offset < 0 && rel_offset >= Bbase () - Bptr ())
	{
	  if (!_IO_in_backup (fp))
	    _IO_switch_to_backup_area (fp);
	  gbump (fp->_IO_read_end + rel_offset - fp->_IO_read_ptr);
	  _IO_mask_flags (fp, 0, _IO_EOF_SEEN);
	  goto resync;
	}
#endif
    }

#ifdef TODO
  _IO_unsave_markers (fp);
#endif

  if (fp->_flags & _IO_NO_READS)
    goto dumb;

  /* Try to seek to a block boundary, to improve kernel page management. */
  new_offset = offset & ~(fp->_IO_buf_end - fp->_IO_buf_base - 1);
  delta = offset - new_offset;
  if (delta > fp->_IO_buf_end - fp->_IO_buf_base)
    {
      new_offset = offset;
      delta = 0;
    }
  result = _IO_SYSSEEK (fp, new_offset, 0);
  if (result < 0)
    return EOF;
  if (delta == 0)
    count = 0;
  else
    {
      count = _IO_SYSREAD (fp, fp->_IO_buf_base,
			   (must_be_exact
			    ? delta : fp->_IO_buf_end - fp->_IO_buf_base));
      if (count < delta)
	{
	  /* We weren't allowed to read, but try to seek the remainder. */
	  offset = count == EOF ? delta : delta-count;
	  dir = _IO_seek_cur;
	  goto dumb;
	}
    }
  _IO_setg (fp, fp->_IO_buf_base, fp->_IO_buf_base + delta,
	    fp->_IO_buf_base + count);
  _IO_setp (fp, fp->_IO_buf_base, fp->_IO_buf_base);
  fp->_offset = result + count;
  _IO_mask_flags (fp, 0, _IO_EOF_SEEN);
  return offset;
 dumb:

  _IO_unsave_markers (fp);
  result = _IO_SYSSEEK (fp, offset, dir);
  if (result != EOF)
    {
      _IO_mask_flags (fp, 0, _IO_EOF_SEEN);
      fp->_offset = result;
      _IO_setg (fp, fp->_IO_buf_base, fp->_IO_buf_base, fp->_IO_buf_base);
      _IO_setp (fp, fp->_IO_buf_base, fp->_IO_buf_base);
    }
  return result;

resync:
  /* We need to do it since it is possible that the file offset in
     the kernel may be changed behind our back. It may happen when
     we fopen a file and then do a fork. One process may access the
     the file and the kernel file offset will be changed. */
  if (fp->_offset >= 0)
    _IO_SYSSEEK (fp, fp->_offset, 0);

  return offset;
}

_IO_ssize_t
_IO_file_read (fp, buf, size)
     _IO_FILE *fp;
     void *buf;
     _IO_ssize_t size;
{
  return read (fp->_fileno, buf, size);
}

_IO_off64_t
_IO_file_seek (fp, offset, dir)
     _IO_FILE *fp;
     _IO_off64_t offset;
     int dir;
{
#ifdef _G_LSEEK64
  return _G_LSEEK64 (fp->_fileno, offset, dir);
#else
  return lseek (fp->_fileno, offset, dir);
#endif
}

int
_IO_file_stat (fp, st)
     _IO_FILE *fp;
     void *st;
{
#ifdef _G_FSTAT64
  return _G_FSTAT64 (fp->_fileno, (struct _G_stat64 *) st);
#else
  return fstat (fp->_fileno, (struct stat *) st);
#endif
}

int
_IO_file_close (fp)
     _IO_FILE *fp;
{
  return close (fp->_fileno);
}

_IO_ssize_t
_IO_new_file_write (f, data, n)
     _IO_FILE *f;
     const void *data;
     _IO_ssize_t n;
{
  _IO_ssize_t to_do = n;
  while (to_do > 0)
    {
      _IO_ssize_t count = write (f->_fileno, data, to_do);
      if (count < 0)
	{
	  f->_flags |= _IO_ERR_SEEN;
	  break;
        }
      to_do -= count;
      data = (void *) ((char *) data + count);
    }
  n -= to_do;
  if (f->_offset >= 0)
    f->_offset += n;
  return n;
}

_IO_size_t
_IO_new_file_xsputn (f, data, n)
     _IO_FILE *f;
     const void *data;
     _IO_size_t n;
{
  register const char *s = (const char *) data;
  _IO_size_t to_do = n;
  int must_flush = 0;
  _IO_size_t count;

  if (n <= 0)
    return 0;
  /* This is an optimized implementation.
     If the amount to be written straddles a block boundary
     (or the filebuf is unbuffered), use sys_write directly. */

  /* First figure out how much space is available in the buffer. */
  count = f->_IO_write_end - f->_IO_write_ptr; /* Space available. */
  if ((f->_flags & _IO_LINE_BUF) && (f->_flags & _IO_CURRENTLY_PUTTING))
    {
      count = f->_IO_buf_end - f->_IO_write_ptr;
      if (count >= n)
	{
	  register const char *p;
	  for (p = s + n; p > s; )
	    {
	      if (*--p == '\n')
		{
		  count = p - s + 1;
		  must_flush = 1;
		  break;
		}
	    }
	}
    }
  /* Then fill the buffer. */
  if (count > 0)
    {
      if (count > to_do)
	count = to_do;
      if (count > 20)
	{
#ifdef _LIBC
	  f->_IO_write_ptr = __mempcpy (f->_IO_write_ptr, s, count);
#else
	  memcpy (f->_IO_write_ptr, s, count);
	  f->_IO_write_ptr += count;
#endif
	  s += count;
	}
      else
	{
	  register char *p = f->_IO_write_ptr;
	  register int i = (int) count;
	  while (--i >= 0)
	    *p++ = *s++;
	  f->_IO_write_ptr = p;
	}
      to_do -= count;
    }
  if (to_do + must_flush > 0)
    {
      _IO_size_t block_size, do_write;
      /* Next flush the (full) buffer. */
      if (_IO_OVERFLOW (f, EOF) == EOF)
	return n - to_do;

      /* Try to maintain alignment: write a whole number of blocks.
	 dont_write is what gets left over. */
      block_size = f->_IO_buf_end - f->_IO_buf_base;
      do_write = to_do - (block_size >= 128 ? to_do % block_size : 0);

      if (do_write)
        {
	  count = new_do_write (f, s, do_write);
	  to_do -= count;
	  if (count < do_write)
	    return n - to_do;
        }

      /* Now write out the remainder.  Normally, this will fit in the
	 buffer, but it's somewhat messier for line-buffered files,
	 so we let _IO_default_xsputn handle the general case. */
      if (to_do)
	to_do -= _IO_default_xsputn (f, s+do_write, to_do);
    }
  return n - to_do;
}

_IO_size_t
_IO_file_xsgetn (fp, data, n)
     _IO_FILE *fp;
     void *data;
     _IO_size_t n;
{
  register _IO_size_t want, have;
  register _IO_ssize_t count;
  register char *s = data;

  want = n;

  if (fp->_IO_buf_base == NULL)
    {
      /* Maybe we already have a push back pointer.  */
      if (fp->_IO_save_base != NULL)
	{
	  free (fp->_IO_save_base);
	  fp->_flags &= ~_IO_IN_BACKUP;
	}
      _IO_doallocbuf (fp);
    }

  while (want > 0)
    {
      have = fp->_IO_read_end - fp->_IO_read_ptr;
      if (want <= have)
	{
	  memcpy (s, fp->_IO_read_ptr, want);
	  fp->_IO_read_ptr += want;
	  want = 0;
	}
      else
	{
	  if (have > 0)
	    {
#ifdef _LIBC
	      s = __mempcpy (s, fp->_IO_read_ptr, have);
#else
	      memcpy (s, fp->_IO_read_ptr, have);
	      s += have;
#endif
	      want -= have;
	      fp->_IO_read_ptr += have;
	    }

	  /* Check for backup and repeat */
	  if (_IO_in_backup (fp))
	    {
	      _IO_switch_to_main_get_area (fp);
	      continue;
	    }

	  /* If we now want less than a buffer, underflow and repeat
	     the copy.  Otherwise, _IO_SYSREAD directly to
	     the user buffer. */
	  if (fp->_IO_buf_base && want < fp->_IO_buf_end - fp->_IO_buf_base)
	    {
	      if (__underflow (fp) == EOF)
		break;

	      continue;
	    }

	  /* These must be set before the sysread as we might longjmp out
	     waiting for input. */
	  _IO_setg (fp, fp->_IO_buf_base, fp->_IO_buf_base, fp->_IO_buf_base);
	  _IO_setp (fp, fp->_IO_buf_base, fp->_IO_buf_base);

	  /* Try to maintain alignment: read a whole number of blocks.  */
	  count = want;
	  if (fp->_IO_buf_base)
	    {
	      _IO_size_t block_size = fp->_IO_buf_end - fp->_IO_buf_base;
	      if (block_size >= 128)
		count -= want % block_size;
	    }

	  count = _IO_SYSREAD (fp, s, count);
	  if (count <= 0)
	    {
	      if (count == 0)
		fp->_flags |= _IO_EOF_SEEN;
	      else
		fp->_flags |= _IO_ERR_SEEN;

	      break;
	    }

	  s += count;
	  want -= count;
	  if (fp->_offset != _IO_pos_BAD)
	    _IO_pos_adjust (fp->_offset, count);
	}
    }

  return n - want;
}

struct _IO_jump_t _IO_file_jumps =
{
  JUMP_INIT_DUMMY,
  JUMP_INIT(finish, _IO_new_file_finish),
  JUMP_INIT(overflow, _IO_new_file_overflow),
  JUMP_INIT(underflow, _IO_new_file_underflow),
  JUMP_INIT(uflow, _IO_default_uflow),
  JUMP_INIT(pbackfail, _IO_default_pbackfail),
  JUMP_INIT(xsputn, _IO_new_file_xsputn),
  JUMP_INIT(xsgetn, _IO_file_xsgetn),
  JUMP_INIT(seekoff, _IO_new_file_seekoff),
  JUMP_INIT(seekpos, _IO_default_seekpos),
  JUMP_INIT(setbuf, _IO_new_file_setbuf),
  JUMP_INIT(sync, _IO_new_file_sync),
  JUMP_INIT(doallocate, _IO_file_doallocate),
  JUMP_INIT(read, _IO_file_read),
  JUMP_INIT(write, _IO_new_file_write),
  JUMP_INIT(seek, _IO_file_seek),
  JUMP_INIT(close, _IO_file_close),
  JUMP_INIT(stat, _IO_file_stat),
  JUMP_INIT(showmanyc, _IO_default_showmanyc),
  JUMP_INIT(imbue, _IO_default_imbue)
};

#ifdef _LIBC
versioned_symbol (libc, _IO_new_do_write, _IO_do_write, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_attach, _IO_file_attach, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_close_it, _IO_file_close_it, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_finish, _IO_file_finish, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_fopen, _IO_file_fopen, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_init, _IO_file_init, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_setbuf, _IO_file_setbuf, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_sync, _IO_file_sync, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_overflow, _IO_file_overflow, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_seekoff, _IO_file_seekoff, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_underflow, _IO_file_underflow, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_write, _IO_file_write, GLIBC_2_1);
versioned_symbol (libc, _IO_new_file_xsputn, _IO_file_xsputn, GLIBC_2_1);
#endif
