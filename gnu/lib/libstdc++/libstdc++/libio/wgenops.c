/* Copyright (C) 1993, 1995, 1997, 1998, 1999, 2000 Free Software Foundation, Inc.
   This file is part of the GNU IO Library.
   Written by Ulrich Drepper <drepper@cygnus.com>.
   Based on the single byte version by Per Bothner <bothner@cygnus.com>.

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
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
#ifdef __STDC__
#include <stdlib.h>
#endif
#include <string.h>
#include <wchar.h>


#ifndef _LIBC
# define __wmemcpy(dst, src, n) wmemcpy (dst, src, n)
#endif


static int save_for_wbackup __P ((_IO_FILE *fp, wchar_t *end_p))
#ifdef _LIBC
     internal_function
#endif
     ;

/* Return minimum _pos markers
   Assumes the current get area is the main get area. */
_IO_ssize_t _IO_least_wmarker __P ((_IO_FILE *fp, wchar_t *end_p));

_IO_ssize_t
_IO_least_wmarker (fp, end_p)
     _IO_FILE *fp;
     wchar_t *end_p;
{
  _IO_ssize_t least_so_far = end_p - fp->_wide_data->_IO_read_base;
  struct _IO_marker *mark;
  for (mark = fp->_markers; mark != NULL; mark = mark->_next)
    if (mark->_pos < least_so_far)
      least_so_far = mark->_pos;
  return least_so_far;
}

/* Switch current get area from backup buffer to (start of) main get area. */
void
_IO_switch_to_main_wget_area (fp)
     _IO_FILE *fp;
{
  wchar_t *tmp;
  fp->_flags &= ~_IO_IN_BACKUP;
  /* Swap _IO_read_end and _IO_save_end. */
  tmp = fp->_wide_data->_IO_read_end;
  fp->_wide_data->_IO_read_end = fp->_wide_data->_IO_save_end;
  fp->_wide_data->_IO_save_end= tmp;
  /* Swap _IO_read_base and _IO_save_base. */
  tmp = fp->_wide_data->_IO_read_base;
  fp->_wide_data->_IO_read_base = fp->_wide_data->_IO_save_base;
  fp->_wide_data->_IO_save_base = tmp;
  /* Set _IO_read_ptr. */
  fp->_wide_data->_IO_read_ptr = fp->_wide_data->_IO_read_base;
}


/* Switch current get area from main get area to (end of) backup area. */
void
_IO_switch_to_wbackup_area (fp)
     _IO_FILE *fp;
{
  wchar_t *tmp;
  fp->_flags |= _IO_IN_BACKUP;
  /* Swap _IO_read_end and _IO_save_end. */
  tmp = fp->_wide_data->_IO_read_end;
  fp->_wide_data->_IO_read_end = fp->_wide_data->_IO_save_end;
  fp->_wide_data->_IO_save_end = tmp;
  /* Swap _IO_read_base and _IO_save_base. */
  tmp = fp->_wide_data->_IO_read_base;
  fp->_wide_data->_IO_read_base = fp->_wide_data->_IO_save_base;
  fp->_wide_data->_IO_save_base = tmp;
  /* Set _IO_read_ptr.  */
  fp->_wide_data->_IO_read_ptr = fp->_wide_data->_IO_read_end;
}


void
_IO_wsetb (f, b, eb, a)
     _IO_FILE *f;
     wchar_t *b;
     wchar_t *eb;
     int a;
{
  if (f->_wide_data->_IO_buf_base && !(f->_flags & _IO_USER_BUF))
    FREE_BUF (f->_wide_data->_IO_buf_base, _IO_wblen (f));
  f->_wide_data->_IO_buf_base = b;
  f->_wide_data->_IO_buf_end = eb;
  if (a)
    f->_flags &= ~_IO_USER_BUF;
  else
    f->_flags |= _IO_USER_BUF;
}


wint_t
_IO_wdefault_pbackfail (fp, c)
     _IO_FILE *fp;
     wint_t c;
{
  if (fp->_wide_data->_IO_read_ptr > fp->_wide_data->_IO_read_base
      && !_IO_in_backup (fp)
      && (wint_t) fp->_IO_read_ptr[-1] == c)
    --fp->_IO_read_ptr;
  else
    {
      /* Need to handle a filebuf in write mode (switch to read mode). FIXME!*/
      if (!_IO_in_backup (fp))
	{
	  /* We need to keep the invariant that the main get area
	     logically follows the backup area.  */
	  if (fp->_wide_data->_IO_read_ptr > fp->_wide_data->_IO_read_base
	      && _IO_have_wbackup (fp))
	    {
	      if (save_for_wbackup (fp, fp->_wide_data->_IO_read_ptr))
		return WEOF;
	    }
	  else if (!_IO_have_wbackup (fp))
	    {
	      /* No backup buffer: allocate one. */
	      /* Use nshort buffer, if unused? (probably not)  FIXME */
	      int backup_size = 128;
	      wchar_t *bbuf = (wchar_t *) malloc (backup_size
						  * sizeof (wchar_t));
	      if (bbuf == NULL)
		return WEOF;
	      fp->_wide_data->_IO_save_base = bbuf;
	      fp->_wide_data->_IO_save_end = (fp->_wide_data->_IO_save_base
					      + backup_size);
	      fp->_wide_data->_IO_backup_base = fp->_wide_data->_IO_save_end;
	    }
	  fp->_wide_data->_IO_read_base = fp->_wide_data->_IO_read_ptr;
	  _IO_switch_to_wbackup_area (fp);
	}
      else if (fp->_wide_data->_IO_read_ptr <= fp->_wide_data->_IO_read_base)
	{
	  /* Increase size of existing backup buffer. */
	  _IO_size_t new_size;
	  _IO_size_t old_size = (fp->_wide_data->_IO_read_end
				 - fp->_wide_data->_IO_read_base);
	  wchar_t *new_buf;
	  new_size = 2 * old_size;
	  new_buf = (wchar_t *) malloc (new_size * sizeof (wchar_t));
	  if (new_buf == NULL)
	    return WEOF;
	  __wmemcpy (new_buf + (new_size - old_size),
		     fp->_wide_data->_IO_read_base, old_size);
	  free (fp->_wide_data->_IO_read_base);
	  _IO_wsetg (fp, new_buf, new_buf + (new_size - old_size),
		     new_buf + new_size);
	  fp->_wide_data->_IO_backup_base = fp->_wide_data->_IO_read_ptr;
	}

      *--fp->_wide_data->_IO_read_ptr = c;
    }
  return c;
}


void
_IO_wdefault_finish (fp, dummy)
     _IO_FILE *fp;
     int dummy;
{
  struct _IO_marker *mark;
  if (fp->_wide_data->_IO_buf_base && !(fp->_flags & _IO_USER_BUF))
    {
      FREE_BUF (fp->_wide_data->_IO_buf_base,
		_IO_wblen (fp) * sizeof (wchar_t));
      fp->_wide_data->_IO_buf_base = fp->_wide_data->_IO_buf_end = NULL;
    }

  for (mark = fp->_markers; mark != NULL; mark = mark->_next)
    mark->_sbuf = NULL;

  if (fp->_IO_save_base)
    {
      free (fp->_wide_data->_IO_save_base);
      fp->_IO_save_base = NULL;
    }

#ifdef _IO_MTSAFE_IO
  _IO_lock_fini (*fp->_lock);
#endif

  _IO_un_link ((struct _IO_FILE_plus *) fp);
}


wint_t
_IO_wdefault_uflow (fp)
     _IO_FILE *fp;
{
  wint_t wch;
  wch = _IO_UNDERFLOW (fp);
  if (wch == WEOF)
    return WEOF;
  return *fp->_wide_data->_IO_read_ptr++;
}


wint_t
__woverflow (f, wch)
     _IO_FILE *f;
     wint_t wch;
{
  if (f->_mode == 0)
    _IO_fwide (f, 1);
  return _IO_OVERFLOW (f, wch);
}


wint_t
__wuflow (fp)
     _IO_FILE *fp;
{
  if (fp->_mode < 0 || (fp->_mode == 0 && _IO_fwide (fp, 1) != 1))
    return WEOF;

  if (fp->_mode == 0)
    _IO_fwide (fp, 1);
  if (_IO_in_put_mode (fp))
    if (_IO_switch_to_wget_mode (fp) == EOF)
      return WEOF;
  if (fp->_wide_data->_IO_read_ptr < fp->_wide_data->_IO_read_end)
    return *fp->_wide_data->_IO_read_ptr++;
  if (_IO_in_backup (fp))
    {
      _IO_switch_to_main_wget_area (fp);
      if (fp->_wide_data->_IO_read_ptr < fp->_wide_data->_IO_read_end)
	return *fp->_wide_data->_IO_read_ptr++;
    }
  if (_IO_have_markers (fp))
    {
      if (save_for_wbackup (fp, fp->_wide_data->_IO_read_end))
	return WEOF;
    }
  else if (_IO_have_wbackup (fp))
    _IO_free_wbackup_area (fp);
  return _IO_UFLOW (fp);
}


wint_t
__wunderflow (fp)
     _IO_FILE *fp;
{
  if (fp->_mode < 0 || (fp->_mode == 0 && _IO_fwide (fp, 1) != 1))
    return WEOF;

  if (fp->_mode == 0)
    _IO_fwide (fp, 1);
  if (_IO_in_put_mode (fp))
    if (_IO_switch_to_wget_mode (fp) == EOF)
      return WEOF;
  if (fp->_wide_data->_IO_read_ptr < fp->_wide_data->_IO_read_end)
    return *fp->_wide_data->_IO_read_ptr;
  if (_IO_in_backup (fp))
    {
      _IO_switch_to_main_wget_area (fp);
      if (fp->_wide_data->_IO_read_ptr < fp->_wide_data->_IO_read_end)
	return *fp->_wide_data->_IO_read_ptr;
    }
  if (_IO_have_markers (fp))
    {
      if (save_for_wbackup (fp, fp->_wide_data->_IO_read_end))
	return WEOF;
    }
  else if (_IO_have_backup (fp))
    _IO_free_wbackup_area (fp);
  return _IO_UNDERFLOW (fp);
}


_IO_size_t
_IO_wdefault_xsputn (f, data, n)
     _IO_FILE *f;
     const void *data;
     _IO_size_t n;
{
  const wchar_t *s = (const wchar_t *) data;
  _IO_size_t more = n;
  if (more <= 0)
    return 0;
  for (;;)
    {
      /* Space available. */
      _IO_ssize_t count = (f->_wide_data->_IO_write_end
			   - f->_wide_data->_IO_write_ptr);
      if (count > 0)
	{
	  if ((_IO_size_t) count > more)
	    count = more;
	  if (count > 20)
	    {
#ifdef _LIBC
	      f->_wide_data->_IO_write_ptr =
		__wmempcpy (f->_wide_data->_IO_write_ptr, s, count);
#else
	      memcpy (f->_wide_data->_IO_write_ptr, s, count);
	      f->_wide_data->_IO_write_ptr += count;
#endif
	      s += count;
            }
	  else if (count <= 0)
	    count = 0;
	  else
	    {
	      wchar_t *p = f->_wide_data->_IO_write_ptr;
	      _IO_ssize_t i;
	      for (i = count; --i >= 0; )
		*p++ = *s++;
	      f->_wide_data->_IO_write_ptr = p;
            }
	  more -= count;
        }
      if (more == 0 || __woverflow (f, *s++) == WEOF)
	break;
      more--;
    }
  return n - more;
}


_IO_size_t
_IO_wdefault_xsgetn (fp, data, n)
     _IO_FILE *fp;
     void *data;
     _IO_size_t n;
{
  _IO_size_t more = n;
  wchar_t *s = (wchar_t*) data;
  for (;;)
    {
      /* Data available. */
      _IO_ssize_t count = (fp->_wide_data->_IO_read_end
			   - fp->_wide_data->_IO_read_ptr);
      if (count > 0)
	{
	  if ((_IO_size_t) count > more)
	    count = more;
	  if (count > 20)
	    {
#ifdef _LIBC
	      s = __wmempcpy (s, fp->_wide_data->_IO_read_ptr, count);
#else
	      memcpy (s, fp->_wide_data->_IO_read_ptr, count);
	      s += count;
#endif
	      fp->_wide_data->_IO_read_ptr += count;
	    }
	  else if (count <= 0)
	    count = 0;
	  else
	    {
	      wchar_t *p = fp->_wide_data->_IO_read_ptr;
	      int i = (int) count;
	      while (--i >= 0)
		*s++ = *p++;
	      fp->_wide_data->_IO_read_ptr = p;
            }
            more -= count;
        }
      if (more == 0 || __wunderflow (fp) == WEOF)
	break;
    }
  return n - more;
}


void
_IO_wdoallocbuf (fp)
     _IO_FILE *fp;
{
  if (fp->_wide_data->_IO_buf_base)
    return;
  if (!(fp->_flags & _IO_UNBUFFERED))
    if (_IO_DOALLOCATE (fp) != WEOF)
      return;
  _IO_wsetb (fp, fp->_wide_data->_shortbuf, fp->_wide_data->_shortbuf + 1, 0);
}


_IO_FILE *
_IO_wdefault_setbuf (fp, p, len)
     _IO_FILE *fp;
     wchar_t *p;
     _IO_ssize_t len;
{
  if (_IO_SYNC (fp) == EOF)
    return NULL;
  if (p == NULL || len == 0)
    {
      fp->_flags |= _IO_UNBUFFERED;
      _IO_wsetb (fp, fp->_wide_data->_shortbuf, fp->_wide_data->_shortbuf + 1,
		 0);
    }
  else
    {
      fp->_flags &= ~_IO_UNBUFFERED;
      _IO_wsetb (fp, p, p + len, 0);
    }
  fp->_wide_data->_IO_write_base = fp->_wide_data->_IO_write_ptr
    = fp->_wide_data->_IO_write_end = 0;
  fp->_wide_data->_IO_read_base = fp->_wide_data->_IO_read_ptr
    = fp->_wide_data->_IO_read_end = 0;
  return fp;
}


int
_IO_wdefault_doallocate (fp)
     _IO_FILE *fp;
{
  wchar_t *buf;

  ALLOC_WBUF (buf, _IO_BUFSIZ, EOF);
  _IO_wsetb (fp, buf, buf + _IO_BUFSIZ, 1);
  return 1;
}


int
_IO_switch_to_wget_mode (fp)
     _IO_FILE *fp;
{
  if (fp->_wide_data->_IO_write_ptr > fp->_wide_data->_IO_write_base)
    if (_IO_OVERFLOW (fp, WEOF) == WEOF)
      return EOF;
  if (_IO_in_backup (fp))
    fp->_wide_data->_IO_read_base = fp->_wide_data->_IO_backup_base;
  else
    {
      fp->_wide_data->_IO_read_base = fp->_wide_data->_IO_buf_base;
      if (fp->_wide_data->_IO_write_ptr > fp->_wide_data->_IO_read_end)
	fp->_wide_data->_IO_read_end = fp->_wide_data->_IO_write_ptr;
    }
  fp->_wide_data->_IO_read_ptr = fp->_wide_data->_IO_write_ptr;

  fp->_wide_data->_IO_write_base = fp->_wide_data->_IO_write_ptr
    = fp->_wide_data->_IO_write_end = fp->_wide_data->_IO_read_ptr;

  fp->_flags &= ~_IO_CURRENTLY_PUTTING;
  return 0;
}

void
_IO_free_wbackup_area (fp)
     _IO_FILE *fp;
{
  if (_IO_in_backup (fp))
    _IO_switch_to_main_wget_area (fp);  /* Just in case. */
  free (fp->_wide_data->_IO_save_base);
  fp->_wide_data->_IO_save_base = NULL;
  fp->_wide_data->_IO_save_end = NULL;
  fp->_wide_data->_IO_backup_base = NULL;
}

#if 0
int
_IO_switch_to_wput_mode (fp)
     _IO_FILE *fp;
{
  fp->_wide_data->_IO_write_base = fp->_wide_data->_IO_read_ptr;
  fp->_wide_data->_IO_write_ptr = fp->_wide_data->_IO_read_ptr;
  /* Following is wrong if line- or un-buffered? */
  fp->_wide_data->_IO_write_end = (fp->_flags & _IO_IN_BACKUP
				   ? fp->_wide_data->_IO_read_end
				   : fp->_wide_data->_IO_buf_end);

  fp->_wide_data->_IO_read_ptr = fp->_wide_data->_IO_read_end;
  fp->_wide_data->_IO_read_base = fp->_wide_data->_IO_read_end;

  fp->_flags |= _IO_CURRENTLY_PUTTING;
  return 0;
}
#endif


static int
#ifdef _LIBC
internal_function
#endif
save_for_wbackup (fp, end_p)
     _IO_FILE *fp;
     wchar_t *end_p;
{
  /* Append [_IO_read_base..end_p] to backup area. */
  _IO_ssize_t least_mark = _IO_least_wmarker (fp, end_p);
  /* needed_size is how much space we need in the backup area. */
  _IO_size_t needed_size = ((end_p - fp->_wide_data->_IO_read_base)
			    - least_mark);
  /* FIXME: Dubious arithmetic if pointers are NULL */
  _IO_size_t current_Bsize = (fp->_wide_data->_IO_save_end
			      - fp->_wide_data->_IO_save_base);
  _IO_size_t avail; /* Extra space available for future expansion. */
  _IO_ssize_t delta;
  struct _IO_marker *mark;
  if (needed_size > current_Bsize)
    {
      wchar_t *new_buffer;
      avail = 100;
      new_buffer = (wchar_t *) malloc ((avail + needed_size)
				       * sizeof (wchar_t));
      if (new_buffer == NULL)
	return EOF;		/* FIXME */
      if (least_mark < 0)
	{
#ifdef _LIBC
	  __wmempcpy (__wmempcpy (new_buffer + avail,
				  fp->_wide_data->_IO_save_end + least_mark,
				  -least_mark),
		      fp->_wide_data->_IO_read_base,
		      end_p - fp->_wide_data->_IO_read_base);
#else
	  memcpy (new_buffer + avail,
		  fp->_wide_data->_IO_save_end + least_mark,
		  -least_mark * sizeof (wchar_t));
	  memcpy (new_buffer + avail - least_mark,
		  fp->_wide_data->_IO_read_base,
		  (end_p - fp->_wide_data->_IO_read_base) * sizeof (wchar_t));
#endif
	}
      else
	{
#ifdef _LIBC
	  __wmemcpy (new_buffer + avail,
		     fp->_wide_data->_IO_read_base + least_mark,
		     needed_size);
#else
	  memcpy (new_buffer + avail,
		  fp->_wide_data->_IO_read_base + least_mark,
		  needed_size * sizeof (wchar_t));
#endif
	}
      if (fp->_wide_data->_IO_save_base)
	free (fp->_wide_data->_IO_save_base);
      fp->_wide_data->_IO_save_base = new_buffer;
      fp->_wide_data->_IO_save_end = new_buffer + avail + needed_size;
    }
  else
    {
      avail = current_Bsize - needed_size;
      if (least_mark < 0)
	{
#ifdef _LIBC
	  __wmemmove (fp->_wide_data->_IO_save_base + avail,
		      fp->_wide_data->_IO_save_end + least_mark,
		      -least_mark);
	  __wmemcpy (fp->_wide_data->_IO_save_base + avail - least_mark,
		     fp->_wide_data->_IO_read_base,
		     end_p - fp->_wide_data->_IO_read_base);
#else
	  memmove (fp->_wide_data->_IO_save_base + avail,
		   fp->_wide_data->_IO_save_end + least_mark,
		   -least_mark * sizeof (wchar_t));
	  memcpy (fp->_wide_data->_IO_save_base + avail - least_mark,
		  fp->_wide_data->_IO_read_base,
		  (end_p - fp->_wide_data->_IO_read_base) * sizeof (wchar_t));
#endif
	}
      else if (needed_size > 0)
#ifdef _LIBC
	__wmemcpy (fp->_wide_data->_IO_save_base + avail,
		   fp->_wide_data->_IO_read_base + least_mark,
		   needed_size);
#else
	memcpy (fp->_wide_data->_IO_save_base + avail,
		fp->_wide_data->_IO_read_base + least_mark,
		needed_size * sizeof (wchar_t));
#endif
    }
  fp->_wide_data->_IO_backup_base = fp->_wide_data->_IO_save_base + avail;
  /* Adjust all the streammarkers. */
  delta = end_p - fp->_wide_data->_IO_read_base;
  for (mark = fp->_markers; mark != NULL; mark = mark->_next)
    mark->_pos -= delta;
  return 0;
}

wint_t
_IO_sputbackwc (fp, c)
     _IO_FILE *fp;
     wint_t c;
{
  wint_t result;

  if (fp->_wide_data->_IO_read_ptr > fp->_wide_data->_IO_read_base
      && (wchar_t)fp->_wide_data->_IO_read_ptr[-1] == (wchar_t) c)
    {
      fp->_wide_data->_IO_read_ptr--;
      result = c;
    }
  else
    result = _IO_PBACKFAIL (fp, c);

  if (result != EOF)
    fp->_flags &= ~_IO_EOF_SEEN;

  return result;
}

wint_t
_IO_sungetwc (fp)
     _IO_FILE *fp;
{
  int result;

  if (fp->_wide_data->_IO_read_ptr > fp->_wide_data->_IO_read_base)
    {
      fp->_wide_data->_IO_read_ptr--;
      result = *fp->_wide_data->_IO_read_ptr;
    }
  else
    result = _IO_PBACKFAIL (fp, EOF);

  if (result != WEOF)
    fp->_flags &= ~_IO_EOF_SEEN;

  return result;
}


unsigned
_IO_adjust_wcolumn (start, line, count)
     unsigned start;
     const wchar_t *line;
     int count;
{
  const wchar_t *ptr = line + count;
  while (ptr > line)
    if (*--ptr == L'\n')
      return line + count - ptr - 1;
  return start + count;
}

void
_IO_init_wmarker (marker, fp)
     struct _IO_marker *marker;
     _IO_FILE *fp;
{
  marker->_sbuf = fp;
  if (_IO_in_put_mode (fp))
    _IO_switch_to_wget_mode (fp);
  if (_IO_in_backup (fp))
    marker->_pos = fp->_wide_data->_IO_read_ptr - fp->_wide_data->_IO_read_end;
  else
    marker->_pos = (fp->_wide_data->_IO_read_ptr
		    - fp->_wide_data->_IO_read_base);

  /* Should perhaps sort the chain? */
  marker->_next = fp->_markers;
  fp->_markers = marker;
}

#define BAD_DELTA EOF

/* Return difference between MARK and current position of MARK's stream. */
int
_IO_wmarker_delta (mark)
     struct _IO_marker *mark;
{
  int cur_pos;
  if (mark->_sbuf == NULL)
    return BAD_DELTA;
  if (_IO_in_backup (mark->_sbuf))
    cur_pos = (mark->_sbuf->_wide_data->_IO_read_ptr
	       - mark->_sbuf->_wide_data->_IO_read_end);
  else
    cur_pos = (mark->_sbuf->_wide_data->_IO_read_ptr
	       - mark->_sbuf->_wide_data->_IO_read_base);
  return mark->_pos - cur_pos;
}

int
_IO_seekwmark (fp, mark, delta)
     _IO_FILE *fp;
     struct _IO_marker *mark;
     int delta;
{
  if (mark->_sbuf != fp)
    return EOF;
 if (mark->_pos >= 0)
    {
      if (_IO_in_backup (fp))
	_IO_switch_to_main_wget_area (fp);
      fp->_wide_data->_IO_read_ptr = (fp->_wide_data->_IO_read_base
				      + mark->_pos);
    }
  else
    {
      if (!_IO_in_backup (fp))
	_IO_switch_to_wbackup_area (fp);
      fp->_wide_data->_IO_read_ptr = fp->_wide_data->_IO_read_end + mark->_pos;
    }
  return 0;
}

void
_IO_unsave_wmarkers (fp)
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
    _IO_free_wbackup_area (fp);
}

#endif /* _GLIBCPP_USE_WCHAR_T */
