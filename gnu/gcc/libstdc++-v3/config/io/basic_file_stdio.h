// Wrapper of C-language FILE struct -*- C++ -*-

// Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005
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
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
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
// ISO C++ 14882: 27.8  File-based streams
//

/** @file basic_file.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _BASIC_FILE_STDIO_H
#define _BASIC_FILE_STDIO_H 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <ios>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // Generic declaration.
  template<typename _CharT>
    class __basic_file; 

  // Specialization.
  template<>
    class __basic_file<char>
    {
      // Underlying data source/sink.
      __c_file* 	_M_cfile;

      // True iff we opened _M_cfile, and thus must close it ourselves.
      bool 		_M_cfile_created;

    public:
      __basic_file(__c_lock* __lock = 0);

      __basic_file* 
      open(const char* __name, ios_base::openmode __mode, int __prot = 0664);

      __basic_file*
      sys_open(__c_file* __file, ios_base::openmode);

      __basic_file*
      sys_open(int __fd, ios_base::openmode __mode);

      __basic_file* 
      close(); 

      bool 
      is_open() const;

      int 
      fd();

      __c_file*
      file();

      ~__basic_file();

      streamsize 
      xsputn(const char* __s, streamsize __n);

      streamsize 
      xsputn_2(const char* __s1, streamsize __n1,
	       const char* __s2, streamsize __n2);

      streamsize 
      xsgetn(char* __s, streamsize __n);

      streamoff
      seekoff(streamoff __off, ios_base::seekdir __way);

      int 
      sync();

      streamsize
      showmanyc();
    };

_GLIBCXX_END_NAMESPACE

#endif	
