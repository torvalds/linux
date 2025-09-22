// File descriptor layer for filebuf -*- C++ -*-

// Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
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

/** @file ext/stdio_filebuf.h
 *  This file is a GNU extension to the Standard C++ Library.
 */

#ifndef _STDIO_FILEBUF_H
#define _STDIO_FILEBUF_H 1

#pragma GCC system_header

#include <fstream>

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  /**
   *  @brief Provides a layer of compatibility for C/POSIX.
   *
   *  This GNU extension provides extensions for working with standard C
   *  FILE*'s and POSIX file descriptors.  It must be instantiated by the
   *  user with the type of character used in the file stream, e.g.,
   *  stdio_filebuf<char>.
  */
  template<typename _CharT, typename _Traits = std::char_traits<_CharT> >
    class stdio_filebuf : public std::basic_filebuf<_CharT, _Traits>
    {
    public:
      // Types:
      typedef _CharT				        char_type;
      typedef _Traits				        traits_type;
      typedef typename traits_type::int_type		int_type;
      typedef typename traits_type::pos_type		pos_type;
      typedef typename traits_type::off_type		off_type;
      typedef std::size_t                               size_t;

    public:
      /**
       * deferred initialization
      */
      stdio_filebuf() : std::basic_filebuf<_CharT, _Traits>() {}

      /**
       *  @param  fd  An open file descriptor.
       *  @param  mode  Same meaning as in a standard filebuf.
       *  @param  size  Optimal or preferred size of internal buffer, in chars.
       *
       *  This constructor associates a file stream buffer with an open
       *  POSIX file descriptor. The file descriptor will be automatically
       *  closed when the stdio_filebuf is closed/destroyed.
      */
      stdio_filebuf(int __fd, std::ios_base::openmode __mode,
		    size_t __size = static_cast<size_t>(BUFSIZ));

      /**
       *  @param  f  An open @c FILE*.
       *  @param  mode  Same meaning as in a standard filebuf.
       *  @param  size  Optimal or preferred size of internal buffer, in chars.
       *                Defaults to system's @c BUFSIZ.
       *
       *  This constructor associates a file stream buffer with an open
       *  C @c FILE*.  The @c FILE* will not be automatically closed when the
       *  stdio_filebuf is closed/destroyed.
      */
      stdio_filebuf(std::__c_file* __f, std::ios_base::openmode __mode,
		    size_t __size = static_cast<size_t>(BUFSIZ));

      /**
       *  Closes the external data stream if the file descriptor constructor
       *  was used.
      */
      virtual
      ~stdio_filebuf();

      /**
       *  @return  The underlying file descriptor.
       *
       *  Once associated with an external data stream, this function can be
       *  used to access the underlying POSIX file descriptor.  Note that
       *  there is no way for the library to track what you do with the
       *  descriptor, so be careful.
      */
      int
      fd() { return this->_M_file.fd(); }

      /**
       *  @return  The underlying FILE*.
       *
       *  This function can be used to access the underlying "C" file pointer.
       *  Note that there is no way for the library to track what you do
       *  with the file, so be careful.
       */
      std::__c_file*
      file() { return this->_M_file.file(); }
    };

  template<typename _CharT, typename _Traits>
    stdio_filebuf<_CharT, _Traits>::~stdio_filebuf()
    { }

  template<typename _CharT, typename _Traits>
    stdio_filebuf<_CharT, _Traits>::
    stdio_filebuf(int __fd, std::ios_base::openmode __mode, size_t __size)
    {
      this->_M_file.sys_open(__fd, __mode);
      if (this->is_open())
	{
	  this->_M_mode = __mode;
	  this->_M_buf_size = __size;
	  this->_M_allocate_internal_buffer();
	  this->_M_reading = false;
	  this->_M_writing = false;
	  this->_M_set_buffer(-1);
	}
    }

  template<typename _CharT, typename _Traits>
    stdio_filebuf<_CharT, _Traits>::
    stdio_filebuf(std::__c_file* __f, std::ios_base::openmode __mode,
		  size_t __size)
    {
      this->_M_file.sys_open(__f, __mode);
      if (this->is_open())
	{
	  this->_M_mode = __mode;
	  this->_M_buf_size = __size;
	  this->_M_allocate_internal_buffer();
	  this->_M_reading = false;
	  this->_M_writing = false;
	  this->_M_set_buffer(-1);
	}
    }

_GLIBCXX_END_NAMESPACE

#endif
