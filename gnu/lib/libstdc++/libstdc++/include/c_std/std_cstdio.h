// -*- C++ -*- forwarding header.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003
// Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

//
// ISO C++ 14882: 27.8.2  C Library files
//

/** @file cstdio
 *  This is a Standard C++ Library file.  You should @c #include this file
 *  in your programs, rather than any of the "*.h" implementation files.
 *
 *  This is the C++ version of the Standard C Library header @c stdio.h,
 *  and its contents are (mostly) the same as that header, but are all
 *  contained in the namespace @c std.
 */

#ifndef _CPP_CSTDIO
#define _CPP_CSTDIO 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <cstddef>

#include <stdio.h>

// Get rid of those macros defined in <stdio.h> in lieu of real functions.
#undef clearerr
#undef fclose
#undef feof
#undef ferror
#undef fflush
#undef fgetc
#undef fgetpos
#undef fgets
#undef fopen
#undef fprintf
#undef fputc
#undef fputs
#undef fread
#undef freopen
#undef fscanf
#undef fseek
#undef fsetpos
#undef ftell
#undef fwrite
#undef getc
#undef getchar
#undef gets
#undef perror
#undef printf
#undef putc
#undef putchar
#undef puts
#undef remove
#undef rename
#undef rewind
#undef scanf
#undef setbuf
#undef setvbuf
#undef sprintf
#undef sscanf
#undef tmpfile
#undef tmpnam
#undef ungetc
#undef vfprintf
#undef vprintf
#undef vsprintf

namespace std 
{
  using ::FILE;
  using ::fpos_t;

  using ::clearerr;
  using ::fclose;
  using ::feof;
  using ::ferror;
  using ::fflush;
  using ::fgetc;
  using ::fgetpos;
  using ::fgets;
  using ::fopen;
  using ::fprintf;
  using ::fputc;
  using ::fputs;
  using ::fread;
  using ::freopen;
  using ::fscanf;
  using ::fseek;
  using ::fsetpos;
  using ::ftell;
  using ::fwrite;
  using ::getc;
  using ::getchar;
  using ::perror;
  using ::printf;
  using ::putc;
  using ::putchar;
  using ::puts;
  using ::remove;
  using ::rename;
  using ::rewind;
  using ::scanf;
  using ::setbuf;
  using ::setvbuf;
  using ::sprintf;
  using ::sscanf;
  using ::tmpfile;
  using ::tmpnam;
  using ::ungetc;
  using ::vfprintf;
  using ::vprintf;
  using ::vsprintf;
}

#if defined(_GLIBCPP_USE_C99) || defined(_GLIBCPP_USE_C99_SNPRINTF)

#undef snprintf
#undef vfscanf
#undef vscanf
#undef vsnprintf
#undef vsscanf

namespace __gnu_cxx
{
#if defined(_GLIBCPP_USE_C99_CHECK) || defined(_GLIBCPP_USE_C99_DYNAMIC)
  extern "C" int
    (snprintf)(char * restrict, size_t, const char * restrict, ...);
  extern "C" int
   (vfscanf)(FILE * restrict, const char * restrict, __gnuc_va_list);
  extern "C" int (vscanf)(const char * restrict, __gnuc_va_list);
  extern "C" int
    (vsnprintf)(char * restrict, size_t, const char * restrict, __gnuc_va_list);
  extern "C" int
    (vsscanf)(const char * restrict, const char * restrict, __gnuc_va_list);
#endif
#if !defined(_GLIBCPP_USE_C99_DYNAMIC)
  using ::snprintf;
  using ::vfscanf;
  using ::vscanf;
  using ::vsnprintf;
  using ::vsscanf;
#endif
}

namespace std
{
  using __gnu_cxx::snprintf;
  using __gnu_cxx::vfscanf;
  using __gnu_cxx::vscanf;
  using __gnu_cxx::vsnprintf;
  using __gnu_cxx::vsscanf;
}
#endif 

#endif
