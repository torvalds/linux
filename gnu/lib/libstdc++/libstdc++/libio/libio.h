/* Copyright (C) 1991,92,93,94,95,97,98,99,2000,2001 Free Software Foundation, Inc.
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

#ifndef _IO_STDIO_H
#define _IO_STDIO_H

#include <_G_config.h>
/* ALL of these should be defined in _G_config.h */
#define _IO_pos_t _G_fpos_t /* obsolete */
#define _IO_fpos_t _G_fpos_t
#define _IO_fpos64_t _G_fpos64_t
#define _IO_size_t _G_size_t
#define _IO_ssize_t _G_ssize_t
#define _IO_off_t _G_off_t
#define _IO_off64_t _G_off64_t
#define _IO_pid_t _G_pid_t
#define _IO_uid_t _G_uid_t
#define _IO_iconv_t _G_iconv_t
#define _IO_HAVE_SYS_WAIT _G_HAVE_SYS_WAIT
#define _IO_HAVE_ST_BLKSIZE _G_HAVE_ST_BLKSIZE
#define _IO_BUFSIZ _G_BUFSIZ
#define _IO_va_list _G_va_list
#define _IO_wint_t _G_wint_t

#ifdef _G_NEED_STDARG_H
/* This define avoids name pollution if we're using GNU stdarg.h */
# define __need___va_list
# include <stdarg.h>
# ifdef __GNUC_VA_LIST
#  undef _IO_va_list
#  define _IO_va_list __gnuc_va_list
# endif /* __GNUC_VA_LIST */
#endif

#ifndef __THROW
# ifdef __cplusplus
#  define __THROW throw ()
# else
#  define __THROW
# endif
#endif /* not __THROW */

#ifndef __P
# define __P(p) p __THROW
#endif /* not __P */

#ifndef __PMT
# define __PMT(p) p
#endif /* not __PMT */

/* For backward compatibility */
#ifndef _PARAMS
# define _PARAMS(protos) __P(protos)
#endif /*!_PARAMS*/

#ifndef __STDC__
# ifndef const
#  define const
# endif
#endif
#define _IO_UNIFIED_JUMPTABLES 1
#ifndef _G_HAVE_PRINTF_FP
# define _IO_USE_DTOA 1
#endif

#ifndef EOF
# define EOF (-1)
#endif
#ifndef NULL
# if defined __GNUG__ && \
    (__GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 8))
#  define NULL (__null)
# else
#  if !defined(__cplusplus)
#   define NULL ((void*)0)
#  else
#   define NULL (0)
#  endif
# endif
#endif

#define _IOS_INPUT	1
#define _IOS_OUTPUT	2
#define _IOS_ATEND	4
#define _IOS_APPEND	8
#define _IOS_TRUNC	16
#define _IOS_NOCREATE	32
#define _IOS_NOREPLACE	64
#define _IOS_BIN	128

/* Magic numbers and bits for the _flags field.
   The magic numbers use the high-order bits of _flags;
   the remaining bits are available for variable flags.
   Note: The magic numbers must all be negative if stdio
   emulation is desired. */

#define _IO_MAGIC 0xFBAD0000 /* Magic number */
#define _OLD_STDIO_MAGIC 0xFABC0000 /* Emulate old stdio. */
#define _IO_MAGIC_MASK 0xFFFF0000
#define _IO_USER_BUF 1 /* User owns buffer; don't delete it on close. */
#define _IO_UNBUFFERED 2
#define _IO_NO_READS 4 /* Reading not allowed */
#define _IO_NO_WRITES 8 /* Writing not allowd */
#define _IO_EOF_SEEN 0x10
#define _IO_ERR_SEEN 0x20
#define _IO_DELETE_DONT_CLOSE 0x40 /* Don't call close(_fileno) on cleanup. */
#define _IO_LINKED 0x80 /* Set if linked (using _chain) to streambuf::_list_all.*/
#define _IO_IN_BACKUP 0x100
#define _IO_LINE_BUF 0x200
#define _IO_TIED_PUT_GET 0x400 /* Set if put and get pointer logicly tied. */
#define _IO_CURRENTLY_PUTTING 0x800
#define _IO_IS_APPENDING 0x1000
#define _IO_IS_FILEBUF 0x2000
#define _IO_BAD_SEEN 0x4000
#define _IO_USER_LOCK 0x8000

/* These are "formatting flags" matching the iostream fmtflags enum values. */
#define _IO_SKIPWS 01
#define _IO_LEFT 02
#define _IO_RIGHT 04
#define _IO_INTERNAL 010
#define _IO_DEC 020
#define _IO_OCT 040
#define _IO_HEX 0100
#define _IO_SHOWBASE 0200
#define _IO_SHOWPOINT 0400
#define _IO_UPPERCASE 01000
#define _IO_SHOWPOS 02000
#define _IO_SCIENTIFIC 04000
#define _IO_FIXED 010000
#define _IO_UNITBUF 020000
#define _IO_STDIO 040000
#define _IO_DONT_CLOSE 0100000
#define _IO_BOOLALPHA 0200000


struct _IO_jump_t;  struct _IO_FILE;

/* Handle lock.  */
#ifdef _IO_MTSAFE_IO
# if defined __GLIBC__ && __GLIBC__ >= 2
#  if __GLIBC_MINOR__ == 0
#   include <stdio-lock.h>
#  else
#   include <bits/stdio-lock.h>
#  endif
# else
/*# include <comthread.h>*/
# endif
#else
typedef void _IO_lock_t;
#endif


/* A streammarker remembers a position in a buffer. */

struct _IO_marker {
  struct _IO_marker *_next;
  struct _IO_FILE *_sbuf;
  /* If _pos >= 0
 it points to _buf->Gbase()+_pos. FIXME comment */
  /* if _pos < 0, it points to _buf->eBptr()+_pos. FIXME comment */
  int _pos;
#if 0
    void set_streampos(streampos sp) { _spos = sp; }
    void set_offset(int offset) { _pos = offset; _spos = (streampos)(-2); }
  public:
    streammarker(streambuf *sb);
    ~streammarker();
    int saving() { return  _spos == -2; }
    int delta(streammarker&);
    int delta();
#endif
};

/* This is the structure from the libstdc++ codecvt class.  */
enum __codecvt_result
{
  __codecvt_ok,
  __codecvt_partial,
  __codecvt_error,
  __codecvt_noconv
};

#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
/* The order of the elements in the following struct must match the order
   of the virtual functions in the libstdc++ codecvt class.  */
struct _IO_codecvt
{
  void (*__codecvt_destr) (struct _IO_codecvt *);
  enum __codecvt_result (*__codecvt_do_out) (struct _IO_codecvt *,
					     __c_mbstate_t *,
					     const wchar_t *,
					     const wchar_t *,
					     const wchar_t **, char *,
					     char *, char **);
  enum __codecvt_result (*__codecvt_do_unshift) (struct _IO_codecvt *,
						 __c_mbstate_t *, char *,
						 char *, char **);
  enum __codecvt_result (*__codecvt_do_in) (struct _IO_codecvt *,
					    __c_mbstate_t *,
					    const char *, const char *,
					    const char **, wchar_t *,
					    wchar_t *, wchar_t **);
  int (*__codecvt_do_encoding) (struct _IO_codecvt *);
  int (*__codecvt_do_always_noconv) (struct _IO_codecvt *);
  int (*__codecvt_do_length) (struct _IO_codecvt *, __c_mbstate_t *,
			      const char *, const char *, _IO_size_t);
  int (*__codecvt_do_max_length) (struct _IO_codecvt *);

  _IO_iconv_t __cd_in;
  _IO_iconv_t __cd_out;
};

/* Extra data for wide character streams.  */
struct _IO_wide_data
{
  wchar_t *_IO_read_ptr;	/* Current read pointer */
  wchar_t *_IO_read_end;	/* End of get area. */
  wchar_t *_IO_read_base;	/* Start of putback+get area. */
  wchar_t *_IO_write_base;	/* Start of put area. */
  wchar_t *_IO_write_ptr;	/* Current put pointer. */
  wchar_t *_IO_write_end;	/* End of put area. */
  wchar_t *_IO_buf_base;	/* Start of reserve area. */
  wchar_t *_IO_buf_end;		/* End of reserve area. */
  /* The following fields are used to support backing up and undo. */
  wchar_t *_IO_save_base;	/* Pointer to start of non-current get area. */
  wchar_t *_IO_backup_base;	/* Pointer to first valid character of
				   backup area */
  wchar_t *_IO_save_end;	/* Pointer to end of non-current get area. */

#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  __c_mbstate_t _IO_state;
  __c_mbstate_t _IO_last_state;
#endif
  struct _IO_codecvt _codecvt;

  wchar_t _shortbuf[1];

#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  struct _IO_jump_t *_wide_vtable;
#endif
};
#else /* !(defined _LIBC || defined _GLIBCPP_USE_WCHAR_T) */
/* Because _IO_no_init unconditionally takes a `_IO_wide_data*' as its
   last parameter we must still define this type.  We intentionally
   leave it incomplete to prevent any use of this type when we are not
   supporting wide characters.  */
struct _IO_wide_data;
#endif /* !(defined _LIBC || defined _GLIBCPP_USE_WCHAR_T) */

struct _IO_FILE {
  int _flags;		/* High-order word is _IO_MAGIC; rest is flags. */
#define _IO_file_flags _flags

  /* The following pointers correspond to the C++ streambuf protocol. */
  /* Note:  Tk uses the _IO_read_ptr and _IO_read_end fields directly. */
  char* _IO_read_ptr;	/* Current read pointer */
  char* _IO_read_end;	/* End of get area. */
  char* _IO_read_base;	/* Start of putback+get area. */
  char* _IO_write_base;	/* Start of put area. */
  char* _IO_write_ptr;	/* Current put pointer. */
  char* _IO_write_end;	/* End of put area. */
  char* _IO_buf_base;	/* Start of reserve area. */
  char* _IO_buf_end;	/* End of reserve area. */
  /* The following fields are used to support backing up and undo. */
  char *_IO_save_base; /* Pointer to start of non-current get area. */
  char *_IO_backup_base;  /* Pointer to first valid character of backup area */
  char *_IO_save_end; /* Pointer to end of non-current get area. */

  struct _IO_marker *_markers;

  struct _IO_FILE *_chain;

  int _fileno;
  int _blksize;
  _IO_off_t _old_offset; /* This used to be _offset but it's too small.  */

#define __HAVE_COLUMN /* temporary */
  /* 1+column number of pbase(); 0 is unknown. */
  unsigned short _cur_column;
  signed char _vtable_offset;
  char _shortbuf[1];

  /*  char* _save_gptr;  char* _save_egptr; */

  _IO_lock_t *_lock;
#ifdef _IO_USE_OLD_IO_FILE
};

struct _IO_FILE_complete
{
  struct _IO_FILE _file;
#endif
#if defined _G_IO_IO_FILE_VERSION && _G_IO_IO_FILE_VERSION == 0x20001
  _IO_off64_t _offset;
# if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  /* Wide character stream stuff.  */
  struct _IO_codecvt *_codecvt;
  struct _IO_wide_data *_wide_data;
# else
  void *__pad1;
  void *__pad2;
# endif
  int _mode;
  /* Make sure we don't get into trouble again.  */
  char _unused2[15 * sizeof (int) - 2 * sizeof (void *)];
#endif
};

#ifndef __cplusplus
typedef struct _IO_FILE _IO_FILE;
#endif

struct _IO_FILE_plus;

extern struct _IO_FILE_plus _IO_2_1_stdin_;
extern struct _IO_FILE_plus _IO_2_1_stdout_;
extern struct _IO_FILE_plus _IO_2_1_stderr_;
#ifndef _LIBC
#define _IO_stdin ((_IO_FILE*)(&_IO_2_1_stdin_))
#define _IO_stdout ((_IO_FILE*)(&_IO_2_1_stdout_))
#define _IO_stderr ((_IO_FILE*)(&_IO_2_1_stderr_))
#else
extern _IO_FILE *_IO_stdin;
extern _IO_FILE *_IO_stdout;
extern _IO_FILE *_IO_stderr;
#endif


/* Functions to do I/O and file management for a stream.  */

/* Read NBYTES bytes from COOKIE into a buffer pointed to by BUF.
   Return number of bytes read.  */
typedef __ssize_t __io_read_fn (void *__cookie, char *__buf, size_t __nbytes);

/* Write N bytes pointed to by BUF to COOKIE.  Write all N bytes
   unless there is an error.  Return number of bytes written, or -1 if
   there is an error without writing anything.  If the file has been
   opened for append (__mode.__append set), then set the file pointer
   to the end of the file and then do the write; if not, just write at
   the current file pointer.  */
typedef __ssize_t __io_write_fn (void *__cookie, __const char *__buf,
				 size_t __n);

/* Move COOKIE's file position to *POS bytes from the
   beginning of the file (if W is SEEK_SET),
   the current position (if W is SEEK_CUR),
   or the end of the file (if W is SEEK_END).
   Set *POS to the new file position.
   Returns zero if successful, nonzero if not.  */
typedef int __io_seek_fn (void *__cookie, _IO_off64_t *__pos, int __w);

/* Close COOKIE.  */
typedef int __io_close_fn (void *__cookie);


#ifdef _GNU_SOURCE
/* User-visible names for the above.  */
typedef __io_read_fn cookie_read_function_t;
typedef __io_write_fn cookie_write_function_t;
typedef __io_seek_fn cookie_seek_function_t;
typedef __io_close_fn cookie_close_function_t;

/* The structure with the cookie function pointers.  */
typedef struct
{
  __io_read_fn *read;		/* Read bytes.  */
  __io_write_fn *write;		/* Write bytes.  */
  __io_seek_fn *seek;		/* Seek/tell file position.  */
  __io_close_fn *close;		/* Close file.  */
} _IO_cookie_io_functions_t;
typedef _IO_cookie_io_functions_t cookie_io_functions_t;

struct _IO_cookie_file;

/* Initialize one of those.  */
extern void _IO_cookie_init (struct _IO_cookie_file *__cfile, int __read_write,
			     void *__cookie, _IO_cookie_io_functions_t __fns);
#endif


#ifdef __cplusplus
extern "C" {
#endif

extern int __underflow (_IO_FILE *) __THROW;
extern int __uflow (_IO_FILE *) __THROW;
extern int __overflow (_IO_FILE *, int) __THROW;
extern _IO_wint_t __wunderflow (_IO_FILE *) __THROW;
extern _IO_wint_t __wuflow (_IO_FILE *) __THROW;
extern _IO_wint_t __woverflow (_IO_FILE *, _IO_wint_t) __THROW;

#define _IO_getc_unlocked(_fp) \
       ((_fp)->_IO_read_ptr >= (_fp)->_IO_read_end ? __uflow (_fp) \
	: *(unsigned char *) (_fp)->_IO_read_ptr++)
#define _IO_peekc_unlocked(_fp) \
       ((_fp)->_IO_read_ptr >= (_fp)->_IO_read_end \
	  && __underflow (_fp) == EOF ? EOF \
	: *(unsigned char *) (_fp)->_IO_read_ptr)
#define _IO_putc_unlocked(_ch, _fp) \
   (((_fp)->_IO_write_ptr >= (_fp)->_IO_write_end) \
    ? __overflow (_fp, (unsigned char) (_ch)) \
    : (unsigned char) (*(_fp)->_IO_write_ptr++ = (_ch)))

#define _IO_getwc_unlocked(_fp) \
  ((_fp)->_wide_data->_IO_read_ptr >= (_fp)->_wide_data->_IO_read_end \
   ? __wuflow (_fp) : (_IO_wint_t) *(_fp)->_wide_data->_IO_read_ptr++)
#define _IO_putwc_unlocked(_wch, _fp) \
  ((_fp)->_wide_data->_IO_write_ptr >= (_fp)->_wide_data->_IO_write_end \
   ? __woverflow (_fp, _wch) \
   : (_IO_wint_t) (*(_fp)->_wide_data->_IO_write_ptr++ = (_wch)))

#define _IO_feof_unlocked(__fp) (((__fp)->_flags & _IO_EOF_SEEN) != 0)
#define _IO_ferror_unlocked(__fp) (((__fp)->_flags & _IO_ERR_SEEN) != 0)

extern int _IO_getc (_IO_FILE *__fp) __THROW;
extern int _IO_putc (int __c, _IO_FILE *__fp) __THROW;
extern int _IO_feof (_IO_FILE *__fp) __THROW;
extern int _IO_ferror (_IO_FILE *__fp) __THROW;

extern int _IO_peekc_locked (_IO_FILE *__fp) __THROW;

/* This one is for Emacs. */
#define _IO_PENDING_OUTPUT_COUNT(_fp)	\
	((_fp)->_IO_write_ptr - (_fp)->_IO_write_base)

extern void _IO_flockfile (_IO_FILE *) __THROW;
extern void _IO_funlockfile (_IO_FILE *) __THROW;
extern int _IO_ftrylockfile (_IO_FILE *) __THROW;

#ifdef _IO_MTSAFE_IO
# define _IO_peekc(_fp) _IO_peekc_locked (_fp)
#else
# define _IO_peekc(_fp) _IO_peekc_unlocked (_fp)
# define _IO_flockfile(_fp) /**/
# define _IO_funlockfile(_fp) /**/
# define _IO_ftrylockfile(_fp) /**/
# define _IO_cleanup_region_start(_fct, _fp) /**/
# define _IO_cleanup_region_end(_Doit) /**/
#endif /* !_IO_MTSAFE_IO */

extern int _IO_vfscanf (_IO_FILE * __restrict, const char * __restrict,
			_IO_va_list, int *__restrict) __THROW;
extern int _IO_vfprintf (_IO_FILE *__restrict, const char *__restrict,
			 _IO_va_list) __THROW;
extern _IO_ssize_t _IO_padn (_IO_FILE *, int, _IO_ssize_t) __THROW;
extern _IO_size_t _IO_sgetn (_IO_FILE *, void *, _IO_size_t) __THROW;

extern _IO_off64_t _IO_seekoff (_IO_FILE *, _IO_off64_t, int, int) __THROW;
extern _IO_off64_t _IO_seekpos (_IO_FILE *, _IO_off64_t, int) __THROW;

extern void _IO_free_backup_area (_IO_FILE *) __THROW;

#if defined _LIBC || defined _GLIBCPP_USE_WCHAR_T || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
extern _IO_wint_t _IO_getwc (_IO_FILE *__fp) __THROW;
extern _IO_wint_t _IO_putwc (wchar_t __wc, _IO_FILE *__fp) __THROW;
extern int _IO_fwide (_IO_FILE *__fp, int __mode) __THROW;
# if __GNUC__ >= 2
/* A special optimized version of the function above.  It optimizes the
   case of initializing an unoriented byte stream.  */
#  define _IO_fwide(__fp, __mode) \
  ({ int __result = (__mode);						      \
     if (__result < 0)							      \
       {								      \
	 if ((__fp)->_mode == 0)					      \
	   /* We know that all we have to do is to set the flag.  */	      \
	   (__fp)->_mode = -1;						      \
	 __result = (__fp)->_mode;					      \
       }								      \
     else								      \
       __result = _IO_fwide (__fp, __result);				      \
     __result; })
# endif

extern int _IO_vfwscanf (_IO_FILE * __restrict, const wchar_t * __restrict,
			 _IO_va_list, int *__restrict) __THROW;
extern int _IO_vfwprintf (_IO_FILE *__restrict, const wchar_t *__restrict,
			  _IO_va_list) __THROW;
extern _IO_ssize_t _IO_wpadn (_IO_FILE *, wint_t, _IO_ssize_t) __THROW;
extern void _IO_free_wbackup_area (_IO_FILE *) __THROW;
#endif

#ifdef __cplusplus
}
#endif

#endif /* _IO_STDIO_H */
