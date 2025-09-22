// File descriptor layer for filebuf -*- C++ -*-

// Copyright (C) 2002 Free Software Foundation, Inc.
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

/** @file ext/stdio_filebuf.h
 *  This file is a GNU extension to the Standard C++ Library.
 */

#ifndef _EXT_STDIO_FILEBUF
#define _EXT_STDIO_FILEBUF

#pragma GCC system_header
#include <fstream>

namespace __gnu_cxx
{
  /**
   *  @class stdio_filebuf ext/stdio_filebuf.h <ext/stdio_filebuf.h>
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
      typedef _CharT                     	        char_type;
      typedef _Traits                    	        traits_type;
      typedef typename traits_type::int_type 		int_type;
      typedef typename traits_type::pos_type 		pos_type;
      typedef typename traits_type::off_type 		off_type;
      typedef std::size_t                               size_t;
      
    protected:
      // Stack-based buffer for unbuffered input.
      char_type			_M_unbuf[4];
      
    public:
      /**
       *  @param  fd  An open file descriptor.
       *  @param  mode  Same meaning as in a standard filebuf.
       *  @param  del  Whether to close the file on destruction.
       *  @param  size  Optimal or preferred size of internal buffer, in bytes.
       *
       *  This constructor associates a file stream buffer with an open
       *  POSIX file descriptor.  Iff @a del is true, then the associated
       *  file will be closed when the stdio_filebuf is closed/destroyed.
      */
      stdio_filebuf(int __fd, std::ios_base::openmode __mode, bool __del, 
		    size_t __size);

      /**
       *  @param  f  An open @c FILE*.
       *  @param  mode  Same meaning as in a standard filebuf.
       *  @param  size  Optimal or preferred size of internal buffer, in bytes.
       *                Defaults to system's @c BUFSIZ.
       *
       *  This constructor associates a file stream buffer with an open
       *  C @c FILE*.  The @c FILE* will not be automatically closed when the
       *  stdio_filebuf is closed/destroyed.
      */
      stdio_filebuf(std::__c_file* __f, std::ios_base::openmode __mode, 
		    size_t __size = static_cast<size_t>(BUFSIZ));

      /**
       *  Possibly closes the external data stream, in the case of the file
       *  descriptor constructor and @c del @c == @c true.
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
      fd()
      { return _M_file.fd(); }
    };

  template<typename _CharT, typename _Traits>
    stdio_filebuf<_CharT, _Traits>::~stdio_filebuf()
    { }

  template<typename _CharT, typename _Traits>
    stdio_filebuf<_CharT, _Traits>::
    stdio_filebuf(int __fd, std::ios_base::openmode __mode, bool __del, 
		  size_t __size)
    {
      _M_file.sys_open(__fd, __mode, __del);
      if (this->is_open())
	{
	  _M_mode = __mode;
	  if (__size > 0 && __size < 4)
	    {
	      // Specify not to use an allocated buffer.
	      _M_buf = _M_unbuf;
	      _M_buf_size = __size;
	      _M_buf_size_opt = 0;
	    }
	  else
	    {
	      _M_buf_size_opt = __size;
	      _M_allocate_internal_buffer();
	    }
	  _M_set_indeterminate();
	}
    }

  template<typename _CharT, typename _Traits>
    stdio_filebuf<_CharT, _Traits>::
    stdio_filebuf(std::__c_file* __f, std::ios_base::openmode __mode, 
		  size_t __size)
    {
      _M_file.sys_open(__f, __mode);
      if (this->is_open())
	{
	  _M_mode = __mode;
	  if (__size > 0 && __size < 4)
	    {
	      // Specify not to use an allocated buffer.
	      _M_buf = _M_unbuf;
	      _M_buf_size = __size;
	      _M_buf_size_opt = 0;
	    }
	  else
	    {
	      _M_buf_size_opt = __size;
	      _M_allocate_internal_buffer();
	    }
	  _M_set_indeterminate();
	}
    }
} // namespace __gnu_cxx

#endif /* _EXT_STDIO_FILEBUF */
