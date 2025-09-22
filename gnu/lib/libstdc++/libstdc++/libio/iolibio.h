/* Copyright (C) 1999, 2000, 2004 Free Software Foundation, Inc.
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

#include "libio.h"

/* These emulate stdio functionality, but with a different name
   (_IO_ungetc instead of ungetc), and using _IO_FILE instead of FILE. */

#ifdef __cplusplus
extern "C" {
#endif

extern int _IO_fclose __P((_IO_FILE*));
extern int _IO_new_fclose __P((_IO_FILE*));
extern int _IO_old_fclose __P((_IO_FILE*));
extern _IO_FILE *_IO_fdopen __P((int, const char*));
extern _IO_FILE *_IO_old_fdopen __P((int, const char*));
extern _IO_FILE *_IO_new_fdopen __P((int, const char*));
extern int _IO_fflush __P((_IO_FILE*));
extern int _IO_fgetpos __P((_IO_FILE*, _IO_fpos_t*));
extern int _IO_fgetpos64 __P((_IO_FILE*, _IO_fpos64_t*));
extern char* _IO_fgets __P((char*, int, _IO_FILE*));
extern _IO_FILE *_IO_fopen __P((const char*, const char*));
extern _IO_FILE *_IO_old_fopen __P((const char*, const char*));
extern _IO_FILE *_IO_new_fopen __P((const char*, const char*));
extern _IO_FILE *_IO_fopen64 __P((const char*, const char*));
extern int _IO_fprintf __P((_IO_FILE*, const char*, ...));
extern int _IO_fputs __P((const char*, _IO_FILE*));
extern int _IO_fsetpos __P((_IO_FILE*, const _IO_fpos_t *));
extern int _IO_fsetpos64 __P((_IO_FILE*, const _IO_fpos64_t *));
extern long int _IO_ftell __P((_IO_FILE*));
extern _IO_size_t _IO_fread __P((void*, _IO_size_t, _IO_size_t, _IO_FILE*));
extern _IO_size_t _IO_fwrite __P((const void*,
				      _IO_size_t, _IO_size_t, _IO_FILE*));
extern char* _IO_gets __P((char*));
extern void _IO_perror __P((const char*));
extern int _IO_printf __P((const char*, ...));
extern int _IO_puts __P((const char*));
extern int _IO_scanf __P((const char*, ...));
extern void _IO_setbuffer __P((_IO_FILE *, char*, _IO_size_t));
extern int _IO_setvbuf __P((_IO_FILE*, char*, int, _IO_size_t));
extern int _IO_sscanf __P((const char*, const char*, ...));
extern int _IO_sprintf __P((char *, const char*, ...));
extern int _IO_ungetc __P((int, _IO_FILE*));
extern int _IO_vsscanf __P((const char *, const char *, _IO_va_list));
extern int _IO_vsprintf __P((char*, const char*, _IO_va_list));
extern int _IO_vswprintf __P((wchar_t*, _IO_size_t, const wchar_t*,
			      _IO_va_list));

struct obstack;
extern int _IO_obstack_vprintf __P ((struct obstack *, const char *,
				     _IO_va_list));
extern int _IO_obstack_printf __P ((struct obstack *, const char *, ...));
#ifndef _IO_pos_BAD
#define _IO_pos_BAD ((_IO_off64_t)(-1))
#endif
#define _IO_clearerr(FP) ((FP)->_flags &= ~(_IO_ERR_SEEN|_IO_EOF_SEEN))
#define _IO_fseek(__fp, __offset, __whence) \
  (_IO_seekoff(__fp, __offset, __whence, _IOS_INPUT|_IOS_OUTPUT) == _IO_pos_BAD ? EOF : 0)
#define _IO_rewind(FILE) (void)_IO_seekoff(FILE, 0, 0, _IOS_INPUT|_IOS_OUTPUT)
#define _IO_vprintf(FORMAT, ARGS) _IO_vfprintf(_IO_stdout, FORMAT, ARGS)
#define _IO_freopen(FILENAME, MODE, FP) \
  (_IO_file_close_it(FP), _IO_file_fopen(FP, FILENAME, MODE, 0))
#define _IO_old_freopen(FILENAME, MODE, FP) \
  (_IO_old_file_close_it (FP), _IO_old_file_fopen(FP, FILENAME, MODE))
#define _IO_freopen64(FILENAME, MODE, FP) \
  (_IO_file_close_it(FP), _IO_file_fopen(FP, FILENAME, MODE, 1))
#define _IO_fileno(FP) ((FP)->_fileno)
extern _IO_FILE* _IO_popen __P((const char*, const char*));
extern _IO_FILE* _IO_new_popen __P((const char*, const char*));
extern _IO_FILE* _IO_old_popen __P((const char*, const char*));
extern int __new_pclose __P((_IO_FILE *));
extern int __old_pclose __P((_IO_FILE *));
#define _IO_pclose _IO_fclose
#define _IO_setbuf(_FP, _BUF) _IO_setbuffer(_FP, _BUF, _IO_BUFSIZ)
#define _IO_setlinebuf(_FP) _IO_setvbuf(_FP, NULL, 1, 0)

_IO_FILE *__new_freopen __P ((const char *, const char *, _IO_FILE *));
_IO_FILE *__old_freopen __P ((const char *, const char *, _IO_FILE *));

#ifdef __cplusplus
}
#endif


