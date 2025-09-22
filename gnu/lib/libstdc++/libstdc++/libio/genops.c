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

/* Generic or default I/O operations. */

#include "libioP.h"
#ifdef __STDC__
#include <stdlib.h>
#endif
#include <string.h>

#ifdef _IO_MTSAFE_IO
static _IO_lock_t list_all_lock = _IO_lock_initializer;
#endif

void
_IO_un_link (fp)
     struct _IO_FILE_plus *fp;
{
  if (fp->file._flags & _IO_LINKED)
    {
      struct _IO_FILE_plus **f;
#ifdef _IO_MTSAFE_IO
      _IO_lock_lock (list_all_lock);
#endif
      for (f = &_IO_list_all; *f; f = (struct _IO_FILE_plus **) &(*f)->file._chain)
	{
	  if (*f == fp)
	    {
	      *f = (struct _IO_FILE_plus *) fp->file._chain;
	      break;
	    }
	}
#ifdef _IO_MTSAFE_IO
      _IO_lock_unlock (list_all_lock);
#endif
      fp->file._flags &= ~_IO_LINKED;
    }
}

void
_IO_link_in (fp)
     struct _IO_FILE_plus *fp;
{
    if ((fp->file._flags & _IO_LINKED) == 0)
      {
	fp->file._flags |= _IO_LINKED;
#ifdef _IO_MTSAFE_IO
	_IO_lock_lock (list_all_lock);
#endif
	fp->file._chain = (_IO_FILE *) _IO_list_all;
	_IO_list_all = fp;
#ifdef _IO_MTSAFE_IO
	_IO_lock_unlock (list_all_lock);
#endif
      }
}

/* Return minimum _pos markers
   Assumes the current get area is the main get area. */
_IO_ssize_t _IO_least_marker __P ((_IO_FILE *fp, char *end_p));

_IO_ssize_t
_IO_least_marker (fp, end_p)
     _IO_FILE *fp;
     char *end_p;
{
  _IO_ssize_t least_so_far = end_p - fp->_IO_read_base;
  struct _IO_marker *mark;
  for (mark = fp->_markers; mark != NULL; mark = mark->_next)
    if (mark->_pos < least_so_far)
      least_so_far = mark->_pos;
  return least_so_far;
}

/* Switch current get area from backup buffer to (start of) main get area. */

void
_IO_switch_to_main_get_area (fp)
     _IO_FILE *fp;
{
  char *tmp;
  fp->_flags &= ~_IO_IN_BACKUP;
  /* Swap _IO_read_end and _IO_save_end. */
  tmp = fp->_IO_read_end;
  fp->_IO_read_end = fp->_IO_save_end;
  fp->_IO_save_end= tmp;
  /* Swap _IO_read_base and _IO_save_base. */
  tmp = fp->_IO_read_base;
  fp->_IO_read_base = fp->_IO_save_base;
  fp->_IO_save_base = tmp;
  /* Set _IO_read_ptr. */
  fp->_IO_read_ptr = fp->_IO_read_base;
}

/* Switch current get area from main get area to (end of) backup area. */

void
_IO_switch_to_backup_area (fp)
     _IO_FILE *fp;
{
  char *tmp;
  fp->_flags |= _IO_IN_BACKUP;
  /* Swap _IO_read_end and _IO_save_end. */
  tmp = fp->_IO_read_end;
  fp->_IO_read_end = fp->_IO_save_end;
  fp->_IO_save_end = tmp;
  /* Swap _IO_read_base and _IO_save_base. */
  tmp = fp->_IO_read_base;
  fp->_IO_read_base = fp->_IO_save_base;
  fp->_IO_save_base = tmp;
  /* Set _IO_read_ptr.  */
  fp->_IO_read_ptr = fp->_IO_read_end;
}

int
_IO_switch_to_get_mode (fp)
     _IO_FILE *fp;
{
  if (fp->_IO_write_ptr > fp->_IO_write_base)
    if (_IO_OVERFLOW (fp, EOF) == EOF)
      return EOF;
  if (_IO_in_backup (fp))
    fp->_IO_read_base = fp->_IO_backup_base;
  else
    {
      fp->_IO_read_base = fp->_IO_buf_base;
      if (fp->_IO_write_ptr > fp->_IO_read_end)
	fp->_IO_read_end = fp->_IO_write_ptr;
    }
  fp->_IO_read_ptr = fp->_IO_write_ptr;

  fp->_IO_write_base = fp->_IO_write_ptr = fp->_IO_write_end = fp->_IO_read_ptr;

  fp->_flags &= ~_IO_CURRENTLY_PUTTING;
  return 0;
}

void
_IO_free_backup_area (fp)
     _IO_FILE *fp;
{
  if (_IO_in_backup (fp))
    _IO_switch_to_main_get_area (fp);  /* Just in case. */
  free (fp->_IO_save_base);
  fp->_IO_save_base = NULL;
  fp->_IO_save_end = NULL;
  fp->_IO_backup_base = NULL;
}

#if 0
int
_IO_switch_to_put_mode (fp)
     _IO_FILE *fp;
{
  fp->_IO_write_base = fp->_IO_read_ptr;
  fp->_IO_write_ptr = fp->_IO_read_ptr;
  /* Following is wrong if line- or un-buffered? */
  fp->_IO_write_end = (fp->_flags & _IO_IN_BACKUP
		       ? fp->_IO_read_end : fp->_IO_buf_end);

  fp->_IO_read_ptr = fp->_IO_read_end;
  fp->_IO_read_base = fp->_IO_read_end;

  fp->_flags |= _IO_CURRENTLY_PUTTING;
  return 0;
}
#endif

int
__overflow (f, ch)
     _IO_FILE *f;
     int ch;
{
  /* This is a single-byte stream.  */
  if (f->_mode == 0)
    _IO_fwide (f, -1);
  return _IO_OVERFLOW (f, ch);
}

static int save_for_backup __P ((_IO_FILE *fp, char *end_p))
#ifdef _LIBC
     internal_function
#endif
     ;

static int
#ifdef _LIBC
internal_function
#endif
save_for_backup (fp, end_p)
     _IO_FILE *fp;
     char *end_p;
{
  /* Append [_IO_read_base..end_p] to backup area. */
  _IO_ssize_t least_mark = _IO_least_marker (fp, end_p);
  /* needed_size is how much space we need in the backup area. */
  _IO_size_t needed_size = (end_p - fp->_IO_read_base) - least_mark;
  /* FIXME: Dubious arithmetic if pointers are NULL */
  _IO_size_t current_Bsize = fp->_IO_save_end - fp->_IO_save_base;
  _IO_size_t avail; /* Extra space available for future expansion. */
  _IO_ssize_t delta;
  struct _IO_marker *mark;
  if (needed_size > current_Bsize)
    {
      char *new_buffer;
      avail = 100;
      new_buffer = (char *) malloc (avail + needed_size);
      if (new_buffer == NULL)
	return EOF;		/* FIXME */
      if (least_mark < 0)
	{
#ifdef _LIBC
	  __mempcpy (__mempcpy (new_buffer + avail,
				fp->_IO_save_end + least_mark,
				-least_mark),
		     fp->_IO_read_base,
		     end_p - fp->_IO_read_base);
#else
	  memcpy (new_buffer + avail,
		  fp->_IO_save_end + least_mark,
		  -least_mark);
	  memcpy (new_buffer + avail - least_mark,
		  fp->_IO_read_base,
		  end_p - fp->_IO_read_base);
#endif
	}
      else
	memcpy (new_buffer + avail,
		fp->_IO_read_base + least_mark,
		needed_size);
      if (fp->_IO_save_base)
	free (fp->_IO_save_base);
      fp->_IO_save_base = new_buffer;
      fp->_IO_save_end = new_buffer + avail + needed_size;
    }
  else
    {
      avail = current_Bsize - needed_size;
      if (least_mark < 0)
	{
	  memmove (fp->_IO_save_base + avail,
		   fp->_IO_save_end + least_mark,
		   -least_mark);
	  memcpy (fp->_IO_save_base + avail - least_mark,
		  fp->_IO_read_base,
		  end_p - fp->_IO_read_base);
	}
      else if (needed_size > 0)
	memcpy (fp->_IO_save_base + avail,
		fp->_IO_read_base + least_mark,
		needed_size);
    }
  fp->_IO_backup_base = fp->_IO_save_base + avail;
  /* Adjust all the streammarkers. */
  delta = end_p - fp->_IO_read_base;
  for (mark = fp->_markers; mark != NULL; mark = mark->_next)
    mark->_pos -= delta;
  return 0;
}

int
__underflow (fp)
     _IO_FILE *fp;
{
#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  if (fp->_vtable_offset == 0 && _IO_fwide (fp, -1) != -1)
    return EOF;
#endif

  if (fp->_mode == 0)
    _IO_fwide (fp, -1);
  if (_IO_in_put_mode (fp))
    if (_IO_switch_to_get_mode (fp) == EOF)
      return EOF;
  if (fp->_IO_read_ptr < fp->_IO_read_end)
    return *(unsigned char *) fp->_IO_read_ptr;
  if (_IO_in_backup (fp))
    {
      _IO_switch_to_main_get_area (fp);
      if (fp->_IO_read_ptr < fp->_IO_read_end)
	return *(unsigned char *) fp->_IO_read_ptr;
    }
  if (_IO_have_markers (fp))
    {
      if (save_for_backup (fp, fp->_IO_read_end))
	return EOF;
    }
  else if (_IO_have_backup (fp))
    _IO_free_backup_area (fp);
  return _IO_UNDERFLOW (fp);
}

int
__uflow (fp)
     _IO_FILE *fp;
{
#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  if (fp->_vtable_offset == 0 && _IO_fwide (fp, -1) != -1)
    return EOF;
#endif

  if (fp->_mode == 0)
    _IO_fwide (fp, -11);
  if (_IO_in_put_mode (fp))
    if (_IO_switch_to_get_mode (fp) == EOF)
      return EOF;
  if (fp->_IO_read_ptr < fp->_IO_read_end)
    return *(unsigned char *) fp->_IO_read_ptr++;
  if (_IO_in_backup (fp))
    {
      _IO_switch_to_main_get_area (fp);
      if (fp->_IO_read_ptr < fp->_IO_read_end)
	return *(unsigned char *) fp->_IO_read_ptr++;
    }
  if (_IO_have_markers (fp))
    {
      if (save_for_backup (fp, fp->_IO_read_end))
	return EOF;
    }
  else if (_IO_have_backup (fp))
    _IO_free_backup_area (fp);
  return _IO_UFLOW (fp);
}

void
_IO_setb (f, b, eb, a)
     _IO_FILE *f;
      char *b;
     char *eb;
     int a;
{
  if (f->_IO_buf_base && !(f->_flags & _IO_USER_BUF))
    FREE_BUF (f->_IO_buf_base, _IO_blen (f));
  f->_IO_buf_base = b;
  f->_IO_buf_end = eb;
  if (a)
    f->_flags &= ~_IO_USER_BUF;
  else
    f->_flags |= _IO_USER_BUF;
}

void
_IO_doallocbuf (fp)
     _IO_FILE *fp;
{
  if (fp->_IO_buf_base)
    return;
  if (!(fp->_flags & _IO_UNBUFFERED))
    if (_IO_DOALLOCATE (fp) != EOF)
      return;
  _IO_setb (fp, fp->_shortbuf, fp->_shortbuf+1, 0);
}

int
_IO_default_underflow (fp)
     _IO_FILE *fp;
{
  return EOF;
}

int
_IO_default_uflow (fp)
     _IO_FILE *fp;
{
  int ch = _IO_UNDERFLOW (fp);
  if (ch == EOF)
    return EOF;
  return *(unsigned char *) fp->_IO_read_ptr++;
}

_IO_size_t
_IO_default_xsputn (f, data, n)
     _IO_FILE *f;
     const void *data;
     _IO_size_t n;
{
  const char *s = (char *) data;
  _IO_size_t more = n;
  if (more <= 0)
    return 0;
  for (;;)
    {
      /* Space available. */
      _IO_ssize_t count = f->_IO_write_end - f->_IO_write_ptr;
      if (count > 0)
	{
	  if ((_IO_size_t) count > more)
	    count = more;
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
	  else if (count <= 0)
	    count = 0;
	  else
	    {
	      char *p = f->_IO_write_ptr;
	      _IO_ssize_t i;
	      for (i = count; --i >= 0; )
		*p++ = *s++;
	      f->_IO_write_ptr = p;
            }
	  more -= count;
        }
      if (more == 0 || _IO_OVERFLOW (f, (unsigned char) *s++) == EOF)
	break;
      more--;
    }
  return n - more;
}

_IO_size_t
_IO_sgetn (fp, data, n)
     _IO_FILE *fp;
     void *data;
     _IO_size_t n;
{
  /* FIXME handle putback buffer here! */
  return _IO_XSGETN (fp, data, n);
}

_IO_size_t
_IO_default_xsgetn (fp, data, n)
     _IO_FILE *fp;
     void *data;
     _IO_size_t n;
{
  _IO_size_t more = n;
  char *s = (char*) data;
  for (;;)
    {
      /* Data available. */
      _IO_ssize_t count = fp->_IO_read_end - fp->_IO_read_ptr;
      if (count > 0)
	{
	  if ((_IO_size_t) count > more)
	    count = more;
	  if (count > 20)
	    {
#ifdef _LIBC
	      s = __mempcpy (s, fp->_IO_read_ptr, count);
#else
	      memcpy (s, fp->_IO_read_ptr, count);
	      s += count;
#endif
	      fp->_IO_read_ptr += count;
	    }
	  else if (count <= 0)
	    count = 0;
	  else
	    {
	      char *p = fp->_IO_read_ptr;
	      int i = (int) count;
	      while (--i >= 0)
		*s++ = *p++;
	      fp->_IO_read_ptr = p;
            }
            more -= count;
        }
      if (more == 0 || __underflow (fp) == EOF)
	break;
    }
  return n - more;
}

#if 0
/* Seems not to be needed. --drepper */
int
_IO_sync (fp)
     _IO_FILE *fp;
{
  return 0;
}
#endif

_IO_FILE *
_IO_default_setbuf (fp, p, len)
     _IO_FILE *fp;
     char *p;
     _IO_ssize_t len;
{
    if (_IO_SYNC (fp) == EOF)
	return NULL;
    if (p == NULL || len == 0)
      {
	fp->_flags |= _IO_UNBUFFERED;
	_IO_setb (fp, fp->_shortbuf, fp->_shortbuf+1, 0);
      }
    else
      {
	fp->_flags &= ~_IO_UNBUFFERED;
	_IO_setb (fp, p, p+len, 0);
      }
    fp->_IO_write_base = fp->_IO_write_ptr = fp->_IO_write_end = 0;
    fp->_IO_read_base = fp->_IO_read_ptr = fp->_IO_read_end = 0;
    return fp;
}

_IO_off64_t
_IO_default_seekpos (fp, pos, mode)
     _IO_FILE *fp;
     _IO_off64_t pos;
     int mode;
{
  return _IO_SEEKOFF (fp, pos, 0, mode);
}

int
_IO_default_doallocate (fp)
     _IO_FILE *fp;
{
  char *buf;

  ALLOC_BUF (buf, _IO_BUFSIZ, EOF);
  _IO_setb (fp, buf, buf+_IO_BUFSIZ, 1);
  return 1;
}

void
_IO_init (fp, flags)
     _IO_FILE *fp;
     int flags;
{
  _IO_no_init (fp, flags, -1, NULL, NULL);
}

void
_IO_no_init (fp, flags, orientation, wd, jmp)
     _IO_FILE *fp;
     int flags;
     int orientation;
     struct _IO_wide_data *wd;
     struct _IO_jump_t *jmp;
{
  fp->_flags = _IO_MAGIC|flags;
  fp->_IO_buf_base = NULL;
  fp->_IO_buf_end = NULL;
  fp->_IO_read_base = NULL;
  fp->_IO_read_ptr = NULL;
  fp->_IO_read_end = NULL;
  fp->_IO_write_base = NULL;
  fp->_IO_write_ptr = NULL;
  fp->_IO_write_end = NULL;
  fp->_chain = NULL; /* Not necessary. */

  fp->_IO_save_base = NULL;
  fp->_IO_backup_base = NULL;
  fp->_IO_save_end = NULL;
  fp->_markers = NULL;
  fp->_cur_column = 0;
#if _IO_JUMPS_OFFSET
  fp->_vtable_offset = 0;
#endif
#ifdef _IO_MTSAFE_IO
  _IO_lock_init (*fp->_lock);
#endif
  fp->_mode = orientation;
#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  if (orientation >= 0)
    {
      fp->_wide_data = wd;
      fp->_wide_data->_IO_buf_base = NULL;
      fp->_wide_data->_IO_buf_end = NULL;
      fp->_wide_data->_IO_read_base = NULL;
      fp->_wide_data->_IO_read_ptr = NULL;
      fp->_wide_data->_IO_read_end = NULL;
      fp->_wide_data->_IO_write_base = NULL;
      fp->_wide_data->_IO_write_ptr = NULL;
      fp->_wide_data->_IO_write_end = NULL;
      fp->_wide_data->_IO_save_base = NULL;
      fp->_wide_data->_IO_backup_base = NULL;
      fp->_wide_data->_IO_save_end = NULL;

      fp->_wide_data->_wide_vtable = jmp;
    }
#endif
}

int
_IO_default_sync (fp)
     _IO_FILE *fp;
{
  return 0;
}

/* The way the C++ classes are mapped into the C functions in the
   current implementation, this function can get called twice! */

void
_IO_default_finish (fp, dummy)
     _IO_FILE *fp;
     int dummy;
{
  struct _IO_marker *mark;
  if (fp->_IO_buf_base && !(fp->_flags & _IO_USER_BUF))
    {
      FREE_BUF (fp->_IO_buf_base, _IO_blen (fp));
      fp->_IO_buf_base = fp->_IO_buf_end = NULL;
    }

  for (mark = fp->_markers; mark != NULL; mark = mark->_next)
    mark->_sbuf = NULL;

  if (fp->_IO_save_base)
    {
      free (fp->_IO_save_base);
      fp->_IO_save_base = NULL;
    }

#ifdef _IO_MTSAFE_IO
  _IO_lock_fini (*fp->_lock);
#endif

  _IO_un_link ((struct _IO_FILE_plus *) fp);
}

_IO_off64_t
_IO_default_seekoff (fp, offset, dir, mode)
     _IO_FILE *fp;
     _IO_off64_t offset;
     int dir;
     int mode;
{
    return _IO_pos_BAD;
}

int
_IO_sputbackc (fp, c)
     _IO_FILE *fp;
     int c;
{
  int result;

  if (fp->_IO_read_ptr > fp->_IO_read_base
      && (unsigned char)fp->_IO_read_ptr[-1] == (unsigned char)c)
    {
      fp->_IO_read_ptr--;
      result = (unsigned char) c;
    }
  else
    result = _IO_PBACKFAIL (fp, c);

  if (result != EOF)
    fp->_flags &= ~_IO_EOF_SEEN;

  return result;
}

int
_IO_sungetc (fp)
     _IO_FILE *fp;
{
  int result;

  if (fp->_IO_read_ptr > fp->_IO_read_base)
    {
      fp->_IO_read_ptr--;
      result = (unsigned char) *fp->_IO_read_ptr;
    }
  else
    result = _IO_PBACKFAIL (fp, EOF);

  if (result != EOF)
    fp->_flags &= ~_IO_EOF_SEEN;

  return result;
}

#if 0 /* Work in progress */
/* Seems not to be needed.  */
#if 0
void
_IO_set_column (fp, c)
     _IO_FILE *fp;
     int c;
{
  if (c == -1)
    fp->_column = -1;
  else
    fp->_column = c - (fp->_IO_write_ptr - fp->_IO_write_base);
}
#else
int
_IO_set_column (fp, i)
     _IO_FILE *fp;
     int i;
{
  fp->_cur_column = i + 1;
  return 0;
}
#endif
#endif


unsigned
_IO_adjust_column (start, line, count)
     unsigned start;
     const char *line;
     int count;
{
  const char *ptr = line + count;
  while (ptr > line)
    if (*--ptr == '\n')
      return line + count - ptr - 1;
  return start + count;
}

#if 0
/* Seems not to be needed. --drepper */
int
_IO_get_column (fp)
     _IO_FILE *fp;
{
  if (fp->_cur_column)
    return _IO_adjust_column (fp->_cur_column - 1,
			      fp->_IO_write_base,
			      fp->_IO_write_ptr - fp->_IO_write_base);
  return -1;
}
#endif

int
_IO_flush_all ()
{
  int result = 0;
  struct _IO_FILE *fp;
  for (fp = (_IO_FILE *) _IO_list_all; fp; fp = fp->_chain)
    if (((fp->_mode < 0 && fp->_IO_write_ptr > fp->_IO_write_base)
#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
	 || (fp->_vtable_offset == 0
	     && fp->_mode > 0 && (fp->_wide_data->_IO_write_ptr
				  > fp->_wide_data->_IO_write_base))
#endif
	 )
	&& _IO_OVERFLOW (fp, EOF) == EOF)
      result = EOF;
  return result;
}

void
_IO_flush_all_linebuffered ()
{
  struct _IO_FILE *fp;
  for (fp = (_IO_FILE *) _IO_list_all; fp; fp = fp->_chain)
    if ((fp->_flags & _IO_NO_WRITES) == 0 && fp->_flags & _IO_LINE_BUF)
      _IO_OVERFLOW (fp, EOF);
}

static void _IO_unbuffer_write __P ((void));

static void
_IO_unbuffer_write ()
{
  struct _IO_FILE *fp;
  for (fp = (_IO_FILE *) _IO_list_all; fp; fp = fp->_chain)
    if (! (fp->_flags & _IO_UNBUFFERED)
	&& (! (fp->_flags & _IO_NO_WRITES)
	    || (fp->_flags & _IO_IS_APPENDING))
	/* Iff stream is un-orientated, it wasn't used. */
	&& fp->_mode != 0)
      _IO_SETBUF (fp, NULL, 0);
}

int
_IO_cleanup ()
{
  int result = _IO_flush_all ();

  /* We currently don't have a reliable mechanism for making sure that
     C++ static destructors are executed in the correct order.
     So it is possible that other static destructors might want to
     write to cout - and they're supposed to be able to do so.

     The following will make the standard streambufs be unbuffered,
     which forces any output from late destructors to be written out. */
  _IO_unbuffer_write ();

  return result;
}


void
_IO_init_marker (marker, fp)
     struct _IO_marker *marker;
     _IO_FILE *fp;
{
  marker->_sbuf = fp;
  if (_IO_in_put_mode (fp))
    _IO_switch_to_get_mode (fp);
  if (_IO_in_backup (fp))
    marker->_pos = fp->_IO_read_ptr - fp->_IO_read_end;
  else
    marker->_pos = fp->_IO_read_ptr - fp->_IO_read_base;

  /* Should perhaps sort the chain? */
  marker->_next = fp->_markers;
  fp->_markers = marker;
}

void
_IO_remove_marker (marker)
     struct _IO_marker *marker;
{
  /* Unlink from sb's chain. */
  struct _IO_marker **ptr = &marker->_sbuf->_markers;
  for (; ; ptr = &(*ptr)->_next)
    {
      if (*ptr == NULL)
	break;
      else if (*ptr == marker)
	{
	  *ptr = marker->_next;
	  return;
	}
    }
#if 0
    if _sbuf has a backup area that is no longer needed, should we delete
    it now, or wait until the next underflow?
#endif
}

#define BAD_DELTA EOF

int
_IO_marker_difference (mark1, mark2)
     struct _IO_marker *mark1;
     struct _IO_marker *mark2;
{
  return mark1->_pos - mark2->_pos;
}

/* Return difference between MARK and current position of MARK's stream. */
int
_IO_marker_delta (mark)
     struct _IO_marker *mark;
{
  int cur_pos;
  if (mark->_sbuf == NULL)
    return BAD_DELTA;
  if (_IO_in_backup (mark->_sbuf))
    cur_pos = mark->_sbuf->_IO_read_ptr - mark->_sbuf->_IO_read_end;
  else
    cur_pos = mark->_sbuf->_IO_read_ptr - mark->_sbuf->_IO_read_base;
  return mark->_pos - cur_pos;
}

int
_IO_seekmark (fp, mark, delta)
     _IO_FILE *fp;
     struct _IO_marker *mark;
     int delta;
{
  if (mark->_sbuf != fp)
    return EOF;
 if (mark->_pos >= 0)
    {
      if (_IO_in_backup (fp))
	_IO_switch_to_main_get_area (fp);
      fp->_IO_read_ptr = fp->_IO_read_base + mark->_pos;
    }
  else
    {
      if (!_IO_in_backup (fp))
	_IO_switch_to_backup_area (fp);
      fp->_IO_read_ptr = fp->_IO_read_end + mark->_pos;
    }
  return 0;
}

void
_IO_unsave_markers (fp)
     _IO_FILE *fp;
{
  struct _IO_marker *mark = fp->_markers;
  if (mark)
    {
#ifdef TODO
      streampos offset = seekoff (0, ios::cur, ios::in);
      if (offset != EOF)
	{
	  offset += eGptr () - Gbase ();
	  for ( ; mark != NULL; mark = mark->_next)
	    mark->set_streampos (mark->_pos + offset);
	}
    else
      {
	for ( ; mark != NULL; mark = mark->_next)
	  mark->set_streampos (EOF);
      }
#endif
      fp->_markers = 0;
    }

  if (_IO_have_backup (fp))
    _IO_free_backup_area (fp);
}

#if 0
/* Seems not to be needed. --drepper */
int
_IO_nobackup_pbackfail (fp, c)
     _IO_FILE *fp;
     int c;
{
  if (fp->_IO_read_ptr > fp->_IO_read_base)
	fp->_IO_read_ptr--;
  if (c != EOF && *fp->_IO_read_ptr != c)
      *fp->_IO_read_ptr = c;
  return (unsigned char) c;
}
#endif

int
_IO_default_pbackfail (fp, c)
     _IO_FILE *fp;
     int c;
{
  if (fp->_IO_read_ptr > fp->_IO_read_base && !_IO_in_backup (fp)
      && (unsigned char) fp->_IO_read_ptr[-1] == c)
    --fp->_IO_read_ptr;
  else
    {
      /* Need to handle a filebuf in write mode (switch to read mode). FIXME!*/
      if (!_IO_in_backup (fp))
	{
	  /* We need to keep the invariant that the main get area
	     logically follows the backup area.  */
	  if (fp->_IO_read_ptr > fp->_IO_read_base && _IO_have_backup (fp))
	    {
	      if (save_for_backup (fp, fp->_IO_read_ptr))
		return EOF;
	    }
	  else if (!_IO_have_backup (fp))
	    {
	      /* No backup buffer: allocate one. */
	      /* Use nshort buffer, if unused? (probably not)  FIXME */
	      int backup_size = 128;
	      char *bbuf = (char *) malloc (backup_size);
	      if (bbuf == NULL)
		return EOF;
	      fp->_IO_save_base = bbuf;
	      fp->_IO_save_end = fp->_IO_save_base + backup_size;
	      fp->_IO_backup_base = fp->_IO_save_end;
	    }
	  fp->_IO_read_base = fp->_IO_read_ptr;
	  _IO_switch_to_backup_area (fp);
	}
      else if (fp->_IO_read_ptr <= fp->_IO_read_base)
	{
	  /* Increase size of existing backup buffer. */
	  _IO_size_t new_size;
	  _IO_size_t old_size = fp->_IO_read_end - fp->_IO_read_base;
	  char *new_buf;
	  new_size = 2 * old_size;
	  new_buf = (char *) malloc (new_size);
	  if (new_buf == NULL)
	    return EOF;
	  memcpy (new_buf + (new_size - old_size), fp->_IO_read_base,
		  old_size);
	  free (fp->_IO_read_base);
	  _IO_setg (fp, new_buf, new_buf + (new_size - old_size),
		    new_buf + new_size);
	  fp->_IO_backup_base = fp->_IO_read_ptr;
	}

      *--fp->_IO_read_ptr = c;
    }
  return (unsigned char) c;
}

_IO_off64_t
_IO_default_seek (fp, offset, dir)
     _IO_FILE *fp;
     _IO_off64_t offset;
     int dir;
{
  return _IO_pos_BAD;
}

int
_IO_default_stat (fp, st)
     _IO_FILE *fp;
     void* st;
{
  return EOF;
}

_IO_ssize_t
_IO_default_read (fp, data, n)
     _IO_FILE* fp;
     void *data;
     _IO_ssize_t n;
{
  return -1;
}

_IO_ssize_t
_IO_default_write (fp, data, n)
     _IO_FILE *fp;
     const void *data;
     _IO_ssize_t n;
{
  return 0;
}

int
_IO_default_showmanyc (fp)
     _IO_FILE *fp;
{
  return -1;
}

void
_IO_default_imbue (fp, locale)
     _IO_FILE *fp;
     void *locale;
{
}

_IO_ITER
_IO_iter_begin()
{
  return (_IO_ITER) _IO_list_all;
}

_IO_ITER
_IO_iter_end()
{
  return NULL;
}

_IO_ITER
_IO_iter_next(iter)
    _IO_ITER iter;
{
  return iter->_chain;
}

_IO_FILE *
_IO_iter_file(iter)
    _IO_ITER iter;
{
  return iter;
}

void
_IO_list_lock()
{
#ifdef _IO_MTSAFE_IO
  _IO_lock_lock (list_all_lock);
#endif
}

void
_IO_list_unlock()
{
#ifdef _IO_MTSAFE_IO
  _IO_lock_unlock (list_all_lock);
#endif
}

void
_IO_list_resetlock()
{
#ifdef _IO_MTSAFE_IO
  _IO_lock_init (list_all_lock);
#endif
}


#ifdef TODO
#if defined(linux)
#define IO_CLEANUP ;
#endif

#ifdef IO_CLEANUP
  IO_CLEANUP
#else
struct __io_defs {
    __io_defs() { }
    ~__io_defs() { _IO_cleanup (); }
};
__io_defs io_defs__;
#endif

#endif /* TODO */

#ifdef weak_alias
weak_alias (_IO_cleanup, _cleanup)
#endif

#ifdef text_set_element
text_set_element(__libc_atexit, _cleanup);
#endif
