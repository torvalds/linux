// Wrapper of C-language FILE struct -*- C++ -*-

// Copyright (C) 2000, 2001 Free Software Foundation, Inc.
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
// ISO C++ 14882: 27.8  File-based streams
//

#include <bits/basic_file.h>

namespace std 
{
  // __basic_file<char> definitions
  __basic_file<char>::__basic_file(__c_lock* __lock)
  {
#ifdef _IO_MTSAFE_IO
    _lock = __lock;
#endif
    // Don't set the orientation of the stream when initializing.
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
    _IO_no_init(this, 0, 0, &_M_wfile, 0);
#else /* !defined(_GLIBCPP_USE_WCHAR_T) */
    _IO_no_init(this, 0, 0, NULL, 0);
#endif /* !defined(_GLIBCPP_USE_WCHAR_T) */
    _IO_JUMPS((_IO_FILE_plus *) this) = &_IO_file_jumps;
    _IO_file_init((_IO_FILE_plus*)this);
  }

  // NB: Unused.
  int 
  __basic_file<char>::overflow(int __c) 
  { return _IO_file_overflow(this, __c); }

  // NB: Unused.
  int 
  __basic_file<char>::underflow()  
  { return _IO_file_underflow(this); }

  // NB: Unused.
  int 
  __basic_file<char>::uflow()  
  { return _IO_default_uflow(this); }

  // NB: Unused.
  int 
  __basic_file<char>::pbackfail(int __c) 
  { return _IO_default_pbackfail(this, __c); }
 
  streamsize 
  __basic_file<char>::xsputn(const char* __s, streamsize __n)
  { return _IO_file_xsputn(this, __s, __n); }

  streamoff
  __basic_file<char>::seekoff(streamoff __off, ios_base::seekdir __way, 
			      ios_base::openmode __mode)
  { return _IO_file_seekoff(this, __off, __way, __mode); }

  streamoff
  __basic_file<char>::seekpos(streamoff __pos, ios_base::openmode __mode)
  { return _IO_file_seekoff(this, __pos, ios_base::beg, __mode); }

 // NB: Unused.
  streambuf* 
  __basic_file<char>::setbuf(char* __b, int __len)
  { return (streambuf*) _IO_file_setbuf(this,__b, __len); }

 int 
  __basic_file<char>::sync()
  { return _IO_file_sync(this); }

  // NB: Unused.
  int 
  __basic_file<char>::doallocate() 
  { return _IO_file_doallocate(this); }

  // __basic_file<wchar_t> definitions
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  __basic_file<wchar_t>::__basic_file(__c_lock* __lock)
  {
#ifdef _IO_MTSAFE_IO
    _lock = __lock;
#endif
    // Don't set the orientation of the stream when initializing.
    _IO_no_init(this, 0, 0, &_M_wfile, &_IO_wfile_jumps);
    _IO_JUMPS((_IO_FILE_plus *) this) = &_IO_wfile_jumps;
    _IO_file_init((_IO_FILE_plus*)this);

    // In addition, need to allocate the buffer...
    _IO_wdoallocbuf(this);
    // Setup initial positions for this buffer...
    //    if (!(_flags & _IO_NO_READS))
    _IO_wsetg(this, _wide_data->_IO_buf_base, _wide_data->_IO_buf_base,
	      _wide_data->_IO_buf_base);
    //    if (!(_flags & _IO_NO_WRITES))
    _IO_wsetp(this, _wide_data->_IO_buf_base, _wide_data->_IO_buf_base);
    
    // Setup codecvt bits...
    _codecvt = &__c_libio_codecvt;
    
    // Do the same for narrow bits...
    if (_IO_write_base == NULL)
      {
	_IO_doallocbuf(this);
	//      if (!(_flags & _IO_NO_READS))
	_IO_setg(this, _IO_buf_base, _IO_buf_base, _IO_buf_base);
	//    if (!(_flags & _IO_NO_WRITES))
	_IO_setp(this, _IO_buf_base, _IO_buf_base);
      }
  }

 int 
  __basic_file<wchar_t>::overflow(int __c) 
  { return _IO_wfile_overflow(this, __c); }

  int 
  __basic_file<wchar_t>::underflow()  
  { return _IO_wfile_underflow(this); }

  // NB: Unused.
  int 
  __basic_file<wchar_t>::uflow()  
  { return _IO_wdefault_uflow(this); }

  // NB: Unused.
  int 
  __basic_file<wchar_t>::pbackfail(int __c) 
  { return _IO_wdefault_pbackfail(this, __c); }

  streamsize 
  __basic_file<wchar_t>::xsputn(const wchar_t* __s, streamsize __n)
  { return _IO_wfile_xsputn(this, __s, __n); }
  
  streamoff
  __basic_file<wchar_t>::seekoff(streamoff __off, ios_base::seekdir __way, 
				 ios_base::openmode __mode)
  { return _IO_wfile_seekoff(this, __off, __way, __mode); }

  streamoff
  __basic_file<wchar_t>::seekpos(streamoff __pos, ios_base::openmode __mode)
  { return _IO_wfile_seekoff(this, __pos, ios_base::beg, __mode); }

   streambuf* 
  __basic_file<wchar_t>::setbuf(wchar_t* __b, int __len)
  { return (streambuf*) _IO_wfile_setbuf(this,__b, __len); }

   int 
  __basic_file<wchar_t>::sync()
  { return _IO_wfile_sync(this); }

  int 
  __basic_file<wchar_t>::doallocate() 
  { return _IO_wfile_doallocate(this); }
#endif

  // Need to instantiate base class here for type-info bits, etc
  template struct __basic_file_base<char>;
  template class __basic_file<char>;
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template struct __basic_file_base<wchar_t>;
  template class __basic_file<wchar_t>;
#endif
}  // namespace std







