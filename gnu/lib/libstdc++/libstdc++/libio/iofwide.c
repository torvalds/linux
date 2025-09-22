/* Copyright (C) 1999, 2000 Free Software Foundation, Inc.
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

#include <libioP.h>
#ifdef _LIBC
# include <dlfcn.h>
# include <wchar.h>
# include <locale/localeinfo.h>
# include <wcsmbs/wcsmbsload.h>
# include <iconv/gconv_int.h>
#endif
#include <stdlib.h>
#include <string.h>

#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
# include <langinfo.h>
#endif

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
/* Prototypes of libio's codecvt functions.  */
static enum __codecvt_result do_out (struct _IO_codecvt *codecvt,
				     __c_mbstate_t *statep,
				     const wchar_t *from_start,
				     const wchar_t *from_end,
				     const wchar_t **from_stop, char *to_start,
				     char *to_end, char **to_stop);
static enum __codecvt_result do_unshift (struct _IO_codecvt *codecvt,
					 __c_mbstate_t *statep, char *to_start,
					 char *to_end, char **to_stop);
static enum __codecvt_result do_in (struct _IO_codecvt *codecvt,
				    __c_mbstate_t *statep,
				    const char *from_start,
				    const char *from_end,
				    const char **from_stop, wchar_t *to_start,
				    wchar_t *to_end, wchar_t **to_stop);
static int do_encoding (struct _IO_codecvt *codecvt);
static int do_length (struct _IO_codecvt *codecvt, __c_mbstate_t *statep,
		      const char *from_start,
		      const char *from_end, _IO_size_t max);
static int do_max_length (struct _IO_codecvt *codecvt);
static int do_always_noconv (struct _IO_codecvt *codecvt);


/* The functions used in `codecvt' for libio are always the same.  */
struct _IO_codecvt __libio_codecvt =
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


#ifdef _LIBC
static struct __gconv_trans_data libio_translit =
{
  .__trans_fct = __gconv_transliterate
};
#endif
#endif /* defined(GLIBCPP_USE_WCHAR_T) */

/* Return orientation of stream.  If mode is nonzero try to change
   the orientation first.  */
#undef _IO_fwide
int
_IO_fwide (fp, mode)
     _IO_FILE *fp;
     int mode;
{
  /* Normalize the value.  */
  mode = mode < 0 ? -1 : (mode == 0 ? 0 : 1);

  if (mode == 0 || fp->_mode != 0)
    /* The caller simply wants to know about the current orientation
       or the orientation already has been determined.  */
    return fp->_mode;

  /* Set the orientation appropriately.  */
  if (mode > 0)
    {
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
      struct _IO_codecvt *cc = fp->_codecvt;

      fp->_wide_data->_IO_read_ptr = fp->_wide_data->_IO_read_end;
      fp->_wide_data->_IO_write_ptr = fp->_wide_data->_IO_write_base;

#ifdef _LIBC
      /* Get the character conversion functions based on the currently
	 selected locale for LC_CTYPE.  */
      {
	struct gconv_fcts fcts;

	/* Clear the state.  We start all over again.  */
	memset (&fp->_wide_data->_IO_state, '\0', sizeof (__c_mbstate_t));
	memset (&fp->_wide_data->_IO_last_state, '\0', sizeof (__c_mbstate_t));

	__wcsmbs_clone_conv (&fcts);

	/* The functions are always the same.  */
	*cc = __libio_codecvt;

	cc->__cd_in.__cd.__nsteps = 1; /* Only one step allowed.  */
	cc->__cd_in.__cd.__steps = fcts.towc;

	cc->__cd_in.__cd.__data[0].__invocation_counter = 0;
	cc->__cd_in.__cd.__data[0].__internal_use = 1;
	cc->__cd_in.__cd.__data[0].__flags = __GCONV_IS_LAST;
	cc->__cd_in.__cd.__data[0].__statep = &fp->_wide_data->_IO_state;

	/* XXX For now no transliteration.  */
	cc->__cd_in.__cd.__data[0].__trans = NULL;

	cc->__cd_out.__cd.__nsteps = 1; /* Only one step allowed.  */
	cc->__cd_out.__cd.__steps = fcts.tomb;

	cc->__cd_out.__cd.__data[0].__invocation_counter = 0;
	cc->__cd_out.__cd.__data[0].__internal_use = 1;
	cc->__cd_out.__cd.__data[0].__flags = __GCONV_IS_LAST;
	cc->__cd_out.__cd.__data[0].__statep = &fp->_wide_data->_IO_state;

	/* And now the transliteration.  */
	cc->__cd_out.__cd.__data[0].__trans = &libio_translit;
      }
#else
# if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
      {
	/* Determine internal and external character sets.
	   XXX For now we make our life easy: we assume a fixed internal
	   encoding (as most sane systems have; hi HP/UX!).  If somebody
	   cares about systems which changing internal charsets they
	   should come up with a solution for the determination of the
	   currently used internal character set.  */
#if 0
	const char *internal_ccs = _G_INTERNAL_CCS;
	const char *external_ccs = nl_langinfo(CODESET);

	if (external_ccs == NULL)
	  external_ccs = "ISO-8859-1";

	cc->__cd_in = iconv_open (internal_ccs, external_ccs);
	if (cc->__cd_in != (iconv_t) -1)
	  cc->__cd_out = iconv_open (external_ccs, internal_ccs);
#endif
      }
# else
#  error "somehow determine this from LC_CTYPE"
# endif
#endif

      /* From now on use the wide character callback functions.  */
      ((struct _IO_FILE_plus *) fp)->vtable = fp->_wide_data->_wide_vtable;
#else /* !defined(_GLIBCPP_USE_WCHAR_T) */
      mode = fp->_mode;
#endif /* !defined(_GLIBCPP_USE_WCHAR_T) */
    }

  /* Set the mode now.  */
  fp->_mode = mode;

  return mode;
}

#ifdef weak_alias
weak_alias (_IO_fwide, fwide)
#endif

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)

static enum __codecvt_result
do_out (struct _IO_codecvt *codecvt, __c_mbstate_t *statep,
	const wchar_t *from_start, const wchar_t *from_end,
	const wchar_t **from_stop, char *to_start, char *to_end,
	char **to_stop)
{
  enum __codecvt_result result;

#ifdef _LIBC
  struct __gconv_step *gs = codecvt->__cd_out.__cd.__steps;
  int status;
  size_t dummy;
  const unsigned char *from_start_copy = (unsigned char *) from_start;

  codecvt->__cd_out.__cd.__data[0].__outbuf = to_start;
  codecvt->__cd_out.__cd.__data[0].__outbufend = to_end;
  codecvt->__cd_out.__cd.__data[0].__statep = statep;

  status = DL_CALL_FCT (gs->__fct,
			(gs, codecvt->__cd_out.__cd.__data, &from_start_copy,
			 (const unsigned char *) from_end, NULL,
			 &dummy, 0, 0));

  *from_stop = (wchar_t *) from_start_copy;
  *to_stop = codecvt->__cd_out.__cd.__data[0].__outbuf;

  switch (status)
    {
    case __GCONV_OK:
    case __GCONV_EMPTY_INPUT:
      result = __codecvt_ok;
      break;

    case __GCONV_FULL_OUTPUT:
    case __GCONV_INCOMPLETE_INPUT:
      result = __codecvt_partial;
      break;

    default:
      result = __codecvt_error;
      break;
    }
#else
# if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  size_t res;
  const char *from_start_copy = (const char *) from_start;
  size_t from_len = from_end - from_start;
  char *to_start_copy = to_start;
  size_t to_len = to_end - to_start;
  res = iconv (codecvt->__cd_out, &from_start_copy, &from_len,
	       &to_start_copy, &to_len);

  if (res == 0 || from_len == 0)
    result = __codecvt_ok;
  else if (to_len < codecvt->__codecvt_do_max_length (codecvt))
    result = __codecvt_partial;
  else
    result = __codecvt_error;

# else
  /* Decide what to do.  */
  result = __codecvt_error;
# endif
#endif

  return result;
}


static enum __codecvt_result
do_unshift (struct _IO_codecvt *codecvt, __c_mbstate_t *statep,
	    char *to_start, char *to_end, char **to_stop)
{
  enum __codecvt_result result;

#ifdef _LIBC
  struct __gconv_step *gs = codecvt->__cd_out.__cd.__steps;
  int status;
  size_t dummy;

  codecvt->__cd_out.__cd.__data[0].__outbuf = to_start;
  codecvt->__cd_out.__cd.__data[0].__outbufend = to_end;
  codecvt->__cd_out.__cd.__data[0].__statep = statep;

  status = DL_CALL_FCT (gs->__fct,
			(gs, codecvt->__cd_out.__cd.__data, NULL, NULL,
			 NULL, &dummy, 1, 0));

  *to_stop = codecvt->__cd_out.__cd.__data[0].__outbuf;

  switch (status)
    {
    case __GCONV_OK:
    case __GCONV_EMPTY_INPUT:
      result = __codecvt_ok;
      break;

    case __GCONV_FULL_OUTPUT:
    case __GCONV_INCOMPLETE_INPUT:
      result = __codecvt_partial;
      break;

    default:
      result = __codecvt_error;
      break;
    }
#else
# if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  size_t res;
  char *to_start_copy = (char *) to_start;
  size_t to_len = to_end - to_start;

  res = iconv (codecvt->__cd_out, NULL, NULL, &to_start_copy, &to_len);

  if (res == 0)
    result = __codecvt_ok;
  else if (to_len < codecvt->__codecvt_do_max_length (codecvt))
    result = __codecvt_partial;
  else
    result = __codecvt_error;
# else
  /* Decide what to do.  */
  result = __codecvt_error;
# endif
#endif

  return result;
}


static enum __codecvt_result
do_in (struct _IO_codecvt *codecvt, __c_mbstate_t *statep,
       const char *from_start, const char *from_end, const char **from_stop,
       wchar_t *to_start, wchar_t *to_end, wchar_t **to_stop)
{
  enum __codecvt_result result;

#ifdef _LIBC
  struct __gconv_step *gs = codecvt->__cd_in.__cd.__steps;
  int status;
  size_t dummy;
  const unsigned char *from_start_copy = (unsigned char *) from_start;

  codecvt->__cd_in.__cd.__data[0].__outbuf = (char *) to_start;
  codecvt->__cd_in.__cd.__data[0].__outbufend = (char *) to_end;
  codecvt->__cd_in.__cd.__data[0].__statep = statep;

  status = DL_CALL_FCT (gs->__fct,
			(gs, codecvt->__cd_in.__cd.__data, &from_start_copy,
			 from_end, NULL, &dummy, 0, 0));

  *from_stop = from_start_copy;
  *to_stop = (wchar_t *) codecvt->__cd_in.__cd.__data[0].__outbuf;

  switch (status)
    {
    case __GCONV_OK:
    case __GCONV_EMPTY_INPUT:
      result = __codecvt_ok;
      break;

    case __GCONV_FULL_OUTPUT:
    case __GCONV_INCOMPLETE_INPUT:
      result = __codecvt_partial;
      break;

    default:
      result = __codecvt_error;
      break;
    }
#else
# if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  size_t res;
  const char *from_start_copy = (const char *) from_start;
  size_t from_len = from_end - from_start;
  char *to_start_copy = (char *) from_start;
  size_t to_len = to_end - to_start;

  res = iconv (codecvt->__cd_in, &from_start_copy, &from_len,
	       &to_start_copy, &to_len);

  if (res == 0)
    result = __codecvt_ok;
  else if (to_len == 0)
    result = __codecvt_partial;
  else if (from_len < codecvt->__codecvt_do_max_length (codecvt))
    result = __codecvt_partial;
  else
    result = __codecvt_error;
# else
  /* Decide what to do.  */
  result = __codecvt_error;
# endif
#endif

  return result;
}


static int
do_encoding (struct _IO_codecvt *codecvt)
{
#ifdef _LIBC
  /* See whether the encoding is stateful.  */
  if (codecvt->__cd_in.__cd.__steps[0].__stateful)
    return -1;
  /* Fortunately not.  Now determine the input bytes for the conversion
     necessary for each wide character.  */
  if (codecvt->__cd_in.__cd.__steps[0].__min_needed_from
      != codecvt->__cd_in.__cd.__steps[0].__max_needed_from)
    /* Not a constant value.  */
    return 0;

  return codecvt->__cd_in.__cd.__steps[0].__min_needed_from;
#else
  /* Worst case scenario.  */
  return -1;
#endif
}


static int
do_always_noconv (struct _IO_codecvt *codecvt)
{
  return 0;
}


static int
do_length (struct _IO_codecvt *codecvt, __c_mbstate_t *statep,
	   const char *from_start, const char *from_end, _IO_size_t max)
{
  int result;
#ifdef _LIBC
  const unsigned char *cp = (const unsigned char *) from_start;
  wchar_t to_buf[max];
  struct __gconv_step *gs = codecvt->__cd_in.__cd.__steps;
  int status;
  size_t dummy;

  codecvt->__cd_in.__cd.__data[0].__outbuf = (char *) to_buf;
  codecvt->__cd_in.__cd.__data[0].__outbufend = (char *) &to_buf[max];
  codecvt->__cd_in.__cd.__data[0].__statep = statep;

  status = DL_CALL_FCT (gs->__fct,
			(gs, codecvt->__cd_in.__cd.__data, &cp, from_end,
			 NULL, &dummy, 0, 0));

  result = cp - (const unsigned char *) from_start;
#else
# if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  const char *from_start_copy = (const char *) from_start;
  size_t from_len = from_end - from_start;
  wchar_t to_buf[max];
  size_t res;
  char *to_start = (char *) to_buf;

  res = iconv (codecvt->__cd_in, &from_start_copy, &from_len,
	       &to_start, &max);

  result = from_start_copy - (char *) from_start;
# else
  /* Decide what to do.  */
  result = 0;
# endif
#endif

  return result;
}


static int
do_max_length (struct _IO_codecvt *codecvt)
{
#ifdef _LIBC
  return codecvt->__cd_in.__cd.__steps[0].__max_needed_from;
#else
  return MB_CUR_MAX;
#endif
}

#endif /* defined(_GLIBCPP_USE_WCHAR_T) */
