// underlying io library  -*- C++ -*-

// Copyright (C) 2000, 2001, 2002 Free Software Foundation, Inc.
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

// c_io_stdio.h - Defines for using "C" stdio.h

#ifndef _CPP_IO_STDIO_H
#define _CPP_IO_STDIO_H 1

#include <cstdio>
#include <cstddef>
#include <bits/gthr.h>

namespace std 
{
// for fpos.h
  typedef long  	streamoff;
  typedef ptrdiff_t	streamsize; // Signed integral type
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  typedef ptrdiff_t	wstreamsize;
#endif
  typedef fpos_t  	__c_streampos;

  typedef __gthread_mutex_t __c_lock;

// for basic_file.h
  typedef FILE __c_file;

// for ios_base.h
  struct __ios_flags
  {
    typedef short __int_type;

    static const __int_type _S_boolalpha =	0x0001;
    static const __int_type _S_dec =		0x0002;
    static const __int_type _S_fixed = 		0x0004;
    static const __int_type _S_hex =		0x0008;
    static const __int_type _S_internal = 	0x0010;
    static const __int_type _S_left =   	0x0020;
    static const __int_type _S_oct =		0x0040;
    static const __int_type _S_right =		0x0080;
    static const __int_type _S_scientific =	0x0100;
    static const __int_type _S_showbase =       0x0200;
    static const __int_type _S_showpoint =	0x0400;
    static const __int_type _S_showpos =	0x0800;
    static const __int_type _S_skipws =		0x1000;
    static const __int_type _S_unitbuf =	0x2000;
    static const __int_type _S_uppercase =	0x4000;
    static const __int_type _S_adjustfield =	0x0020 | 0x0080 | 0x0010;
    static const __int_type _S_basefield =	0x0002 | 0x0040 | 0x0008;
    static const __int_type _S_floatfield =	0x0100 | 0x0004;

    // 27.4.2.1.3  Type ios_base::iostate
    static const __int_type _S_badbit =		0x01;
    static const __int_type _S_eofbit =		0x02;
    static const __int_type _S_failbit =       	0x04;

    // 27.4.2.1.4  Type openmode
    static const __int_type _S_app =		0x01;
    static const __int_type _S_ate =		0x02;
    static const __int_type _S_bin =		0x04;
    static const __int_type _S_in =		0x08;
    static const __int_type _S_out =		0x10;
    static const __int_type _S_trunc =		0x20;
  };
}

#endif // _CPP_IO_STDIO_H
