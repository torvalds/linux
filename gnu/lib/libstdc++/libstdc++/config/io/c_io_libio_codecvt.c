/* Copyright (C) 2000 Free Software Foundation, Inc.
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

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.
/* Slightly modified from glibc/libio/iofwide.c */

#include <libio.h>

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)

/* Prototypes of libio's codecvt functions.  */
static enum __codecvt_result 
do_out(struct _IO_codecvt *codecvt, __c_mbstate_t *statep,
       const wchar_t *from_start, const wchar_t *from_end,
       const wchar_t **from_stop, char *to_start, char *to_end, 
       char **to_stop);

static enum __codecvt_result 
do_unshift(struct _IO_codecvt *codecvt, __c_mbstate_t *statep, char *to_start, 
	   char *to_end, char **to_stop);

static enum __codecvt_result 
do_in(struct _IO_codecvt *codecvt, __c_mbstate_t *statep, 
      const char *from_start, const char *from_end, const char **from_stop, 
      wchar_t *to_start, wchar_t *to_end, wchar_t **to_stop);

static int 
do_encoding(struct _IO_codecvt *codecvt);

static int 
do_length(struct _IO_codecvt *codecvt, __c_mbstate_t *statep, 
	  const char *from_start, const char *from_end, _IO_size_t max);

static int 
do_max_length(struct _IO_codecvt *codecvt);

static int 
do_always_noconv(struct _IO_codecvt *codecvt);


/* The functions used in `codecvt' for libio are always the same.  */
struct _IO_codecvt __c_libio_codecvt =
{
  .__codecvt_destr = NULL,		/* Destructor, never used.  */
  .__codecvt_do_out = do_out,
  .__codecvt_do_unshift = do_unshift,
  .__codecvt_do_in = do_in,
  .__codecvt_do_encoding = do_encoding,
  .__codecvt_do_always_noconv = do_always_noconv,
  .__codecvt_do_length = do_length,
  .__codecvt_do_max_length = do_max_length
};

static enum __codecvt_result
do_out(struct _IO_codecvt *codecvt, __c_mbstate_t *statep,
       const wchar_t *from_start, const wchar_t *from_end,
       const wchar_t **from_stop, char *to_start, char *to_end,
       char **to_stop)
{
  enum __codecvt_result res = __codecvt_ok;

  while (from_start < from_end)
    {
      if (to_start >= to_end)
	{
	  res = __codecvt_partial;
	  break;
	}
      *to_start++ = (char) *from_start++;
    }

  *from_stop = from_start;
  *to_stop = to_start;

  return res;
}


static enum __codecvt_result
do_unshift(struct _IO_codecvt *codecvt, __c_mbstate_t *statep,
	   char *to_start, char *to_end, char **to_stop)
{
  *to_stop = to_start;
  return __codecvt_ok;
}


static enum __codecvt_result
do_in(struct _IO_codecvt *codecvt, __c_mbstate_t *statep,
      const char *from_start, const char *from_end, const char **from_stop,
      wchar_t *to_start, wchar_t *to_end, wchar_t **to_stop)
{
  enum __codecvt_result res = __codecvt_ok;

  while (from_start < from_end)
    {
      if (to_start >= to_end)
	{
	  res = __codecvt_partial;
	  break;
	}
      *to_start++ = (wchar_t) *from_start++;
    }

  *from_stop = from_start;
  *to_stop = to_start;

  return res;
}


static int
do_encoding(struct _IO_codecvt *codecvt)
{ return 1; }


static int
do_always_noconv(struct _IO_codecvt *codecvt)
{ return 0; }


static int
do_length(struct _IO_codecvt *codecvt, __c_mbstate_t *statep,
	  const char *from_start, const char *from_end, _IO_size_t max)
{ return from_end - from_start; }


static int
do_max_length(struct _IO_codecvt *codecvt)
{ return 1; }

#endif /* _GLIBCPP_USE_WCHAR_T */
