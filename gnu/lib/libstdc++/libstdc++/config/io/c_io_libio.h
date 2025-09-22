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

// c_io_libio.h - Defines for using the GNU libio

#ifndef _CPP_IO_LIBIO_H
#define _CPP_IO_LIBIO_H 1

#include <libio.h>

namespace std 
{
// from fpos.h
  typedef _IO_ssize_t 	streamsize; // Signed integral type
  typedef _IO_ssize_t 	wstreamsize;

#if defined(_G_IO_IO_FILE_VERSION) && _G_IO_IO_FILE_VERSION == 0x20001
  typedef _IO_off64_t 	streamoff;
  typedef _IO_fpos64_t 	__c_streampos;
#else
  typedef _IO_off_t 	streamoff;
  typedef _IO_fpos_t 	__c_streampos;
#endif

#ifdef _GLIBCPP_USE_THREADS
  typedef _IO_lock_t   __c_lock;
#else
  typedef int          __c_lock;
#endif

// from basic_file.h
  typedef _IO_FILE 	__c_file_type;
  typedef _IO_wide_data __c_wfile_type;
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  extern "C" _IO_codecvt __c_libio_codecvt;
#endif 

// from ios_base.h
  struct __ios_flags
  {
    typedef short __int_type;

    static const __int_type _S_boolalpha =	_IO_BAD_SEEN;
    static const __int_type _S_dec =		_IO_DEC;
    static const __int_type _S_fixed = 		_IO_FIXED;
    static const __int_type _S_hex =		_IO_HEX;
    static const __int_type _S_internal = 	_IO_INTERNAL;
    static const __int_type _S_left =          	_IO_LEFT;
    static const __int_type _S_oct =		_IO_OCT;
    static const __int_type _S_right =		_IO_RIGHT;
    static const __int_type _S_scientific =	_IO_SCIENTIFIC;
    static const __int_type _S_showbase =      	_IO_SHOWBASE;
    static const __int_type _S_showpoint =	_IO_SHOWPOINT;
    static const __int_type _S_showpos =       	_IO_SHOWPOS;
    static const __int_type _S_skipws =		_IO_SKIPWS;
    static const __int_type _S_unitbuf =       	_IO_UNITBUF;
    static const __int_type _S_uppercase =	_IO_UPPERCASE;
    static const __int_type _S_adjustfield =	_IO_LEFT | _IO_RIGHT
    						| _IO_INTERNAL;
    static const __int_type _S_basefield =	_IO_DEC | _IO_OCT | _IO_HEX;
    static const __int_type _S_floatfield =	_IO_SCIENTIFIC | _IO_FIXED;

    // 27.4.2.1.3  Type ios_base::iostate
    static const __int_type _S_badbit =		_IO_BAD_SEEN;
    static const __int_type _S_eofbit =		_IO_EOF_SEEN;
    static const __int_type _S_failbit =       	_IO_ERR_SEEN;

    // 27.4.2.1.4  Type openmode
    static const __int_type _S_app =		_IOS_APPEND;
    static const __int_type _S_ate =		_IOS_ATEND;
    static const __int_type _S_bin =		_IOS_BIN;
    static const __int_type _S_in =		_IOS_INPUT;
    static const __int_type _S_out =		_IOS_OUTPUT;
    static const __int_type _S_trunc =		_IOS_TRUNC;
  };
}

#endif // _CPP_IO_LIBIO_H








