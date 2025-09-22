// Wrapper of C-language FILE struct -*- C++ -*-

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

//
// ISO C++ 14882: 27.8  File-based streams
//

/** @file basic_file.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CPP_BASIC_FILE
#define _CPP_BASIC_FILE 1

#pragma GCC system_header

#include <bits/c++config.h>
#include <ios>
#include <libioP.h>

namespace std 
{
  // Ulrich is going to make some detailed comment here, explaining
  // all this unpleasantness, providing detailed performance analysis
  // as to why we have to do all this lame vtable hacking instead of a
  // sane, function-based approach. This verbiage will provide a clear
  // and detailed description of the whole object-layout,
  // vtable-swapping, sordid history of this hack.
  template<typename _CharT>
    struct __basic_file_base: public __c_file_type
    {
      virtual 
      ~__basic_file_base() { };

      virtual int 
      overflow(int __c = EOF) = 0;

      virtual int 
      underflow() = 0;

      virtual int 
      uflow() = 0;

      virtual int 
      pbackfail(int __c) = 0;

      virtual streamsize 
      xsputn(const _CharT* __s, streamsize __n) = 0;

      virtual streamsize 
      xsgetn(_CharT* __s, streamsize __n) = 0;

      virtual streamoff
      seekoff(streamoff __off, ios_base::seekdir __way,
	      ios_base::openmode __mode = ios_base::in | ios_base::out) = 0;

      virtual streamoff
      seekpos(streamoff __pos, 
	      ios_base::openmode __mode = ios_base::in | ios_base::out) = 0;

      virtual streambuf* 
      setbuf(_CharT* __b, int __len) = 0;

      virtual int 
      sync() = 0;

      virtual int 
      doallocate() = 0;

      virtual streamsize 
      sys_read(_CharT* __s, streamsize __n) = 0;

      virtual streamsize 
      sys_write(const _CharT* __s, streamsize __n) = 0;

      virtual streamoff
      sys_seek(streamoff __off, ios_base::seekdir __way) = 0;

      virtual int 
      sys_close() = 0;

      virtual int 
      sys_stat(void* __v) = 0;

      virtual int 
      showmanyc() = 0;

      virtual void 
      imbue(void* __v) = 0;
    };

  // Some of these member functions are based on libio/filebuf.cc.
  // Also note that the order and number of virtual functions has to precisely
  // match the order and number in the _IO_jump_t struct defined in libioP.h.
  template<typename _CharT>
    class __basic_file: public __basic_file_base<_CharT>
    {
# if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
      __c_wfile_type	_M_wfile;
# endif

    public:
      __basic_file(__c_lock* __lock = 0);
      
      void 
      _M_open_mode(ios_base::openmode __mode, int& __p_mode, int& __rw_mode, 
		   char* __c_mode);
      
      // Equivalent to the normal fopen function.
      __basic_file* 
      open(const char* __name, ios_base::openmode __mode, int __prot = 0664);

      // Used for opening the standard streams, cin, cout, cerr, clog,
      // and their wide-stream equivalents. Instead of calling open, it
      // just sets
      //  - for libio:  __c_file_type->_fileno and the respective _flags bits
      //  - for stdio:  _M_cfile = __file and some internal flags
      // and returns.
      __basic_file*
      sys_open(__c_file_type* __file, ios_base::openmode __mode);

      _CharT
      sys_getc();

      _CharT
      sys_ungetc(_CharT);

      __basic_file* 
      close(); 

      bool 
      is_open();

      int 
      fd();

      // NB: Must match FILE specific jump table starting here--this
      // means all virtual functions starting with the dtor must match,
      // slot by slot. For glibc-based dystems, this means the _IO_FILE
      // as the FILE struct and _IO_jump_t as the jump table.
      virtual 
      ~__basic_file(); // Takes the place of __finish.

      virtual int 
      overflow(int __c = EOF);

      virtual int 
      underflow();

      virtual int 
      uflow();

      virtual int 
      pbackfail(int __c);

      // A complex "write" function that sets all of __c_file_type's
      // pointers and associated data members correctly and manages its
      // relation to the external byte sequence.
      virtual streamsize 
      xsputn(const _CharT* __s, streamsize __n);

      // A complex "read" function that sets all of __c_file_type's
      // pointers and associated data members correctly and manages its
      // relation to the external byte sequence.
      virtual streamsize 
      xsgetn(_CharT* __s, streamsize __n);

      // A complex "seekoff" function that sets all of __c_file_type's
      // pointers and associated data members correctly and manages its
      // relation to the external byte sequence.
      virtual streamoff
      seekoff(streamoff __off, ios_base::seekdir __way,
	      ios_base::openmode __mode = ios_base::in | ios_base::out);

      // A complex "seekpos" function that sets all of __c_file_type's
      // pointers and associated data members correctly and manages its
      // relation to the external byte sequence.
      virtual streamoff
      seekpos(streamoff __pos, 
	      ios_base::openmode __mode = ios_base::in | ios_base::out);

      virtual streambuf* 
      setbuf(_CharT* __b, int __len);

      virtual int 
      sync();

      virtual int 
      doallocate();

      // A simple read function for the external byte sequence, that
      // does no mucking around with or setting of the pointers or flags
      // in __c_file_type.
      virtual streamsize 
      sys_read(_CharT* __s, streamsize __n);

      // A simple write function for the external byte sequence, that
      // does no mucking around with or setting of the pointers or flags
      // in __c_file_type.
      virtual streamsize 
      sys_write(const _CharT* __s, streamsize __n);

      // A simple seek function for the external byte sequence, that
      // does no mucking around with or setting of the pointers or flags
      // in __c_file_type.
      virtual streamoff
      sys_seek(streamoff __off, ios_base::seekdir __way);

      virtual int 
      sys_close();

      virtual int 
      sys_stat(void* __v);

      virtual int 
      showmanyc();

      virtual void 
      imbue(void* __v);
    };

  // __basic_file<char> specializations
  template<>
    __basic_file<char>::__basic_file(__c_lock* __lock);

  template<>
    int 
    __basic_file<char>::overflow(int __c);

  template<>
    int 
    __basic_file<char>::underflow();

  template<>
    int 
    __basic_file<char>::uflow();

  template<>
    int 
    __basic_file<char>::pbackfail(int __c);

  template<>
    streamsize 
    __basic_file<char>::xsputn(const char* __s, streamsize __n);

  template<>
    streamoff
    __basic_file<char>::seekoff(streamoff __off, ios_base::seekdir __way, 
				ios_base::openmode __mode);

  template<>
    streamoff
    __basic_file<char>::seekpos(streamoff __pos, ios_base::openmode __mode);

  template<>
    streambuf* 
    __basic_file<char>::setbuf(char* __b, int __len);

  template<>
    int 
    __basic_file<char>::sync();

  template<>
    int 
    __basic_file<char>::doallocate();

  // __basic_file<wchar_t> specializations
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<>
    __basic_file<wchar_t>::__basic_file(__c_lock* __lock);

  template<>
    int 
    __basic_file<wchar_t>::overflow(int __c);

  template<>
    int 
    __basic_file<wchar_t>::underflow();

  template<>
    int 
    __basic_file<wchar_t>::uflow();

  template<>
    int 
    __basic_file<wchar_t>::pbackfail(int __c);

  template<>
    streamsize 
    __basic_file<wchar_t>::xsputn(const wchar_t* __s, streamsize __n);

  template<>
    streamoff
    __basic_file<wchar_t>::seekoff(streamoff __off, ios_base::seekdir __way, 
				ios_base::openmode __mode);

  template<>
    streamoff
    __basic_file<wchar_t>::seekpos(streamoff __pos, ios_base::openmode __mode);

  template<>
    streambuf* 
    __basic_file<wchar_t>::setbuf(wchar_t* __b, int __len);

  template<>
    int 
    __basic_file<wchar_t>::sync();

  template<>
    int 
    __basic_file<wchar_t>::doallocate();
#endif

  template<typename _CharT>
    __basic_file<_CharT>::~__basic_file()
    { _IO_file_finish(this, 0); }
      
  template<typename _CharT>
    void 
    __basic_file<_CharT>::_M_open_mode(ios_base::openmode __mode, 
				       int& __p_mode, int& __rw_mode, 
				       char* /*__c_mode*/)
    {  
#ifdef O_BINARY
      bool __testb = __mode & ios_base::binary;
#endif
      bool __testi = __mode & ios_base::in;
      bool __testo = __mode & ios_base::out;
      bool __testt = __mode & ios_base::trunc;
      bool __testa = __mode & ios_base::app;
      
      if (!__testi && __testo && !__testt && !__testa)
	{
	  __p_mode = O_WRONLY | O_TRUNC | O_CREAT;
	  __rw_mode = _IO_NO_READS;
	}
      if (!__testi && __testo && !__testt && __testa)
	{
	  __p_mode = O_WRONLY | O_APPEND | O_CREAT;
	  __rw_mode = _IO_NO_READS | _IO_IS_APPENDING;
	}
      if (!__testi && __testo && __testt && !__testa)
	{
	  __p_mode = O_WRONLY | O_TRUNC | O_CREAT;
	  __rw_mode = _IO_NO_READS;
	}
      if (__testi && !__testo && !__testt && !__testa)
	{
	  __p_mode = O_RDONLY;
	  __rw_mode = _IO_NO_WRITES;
	}
      if (__testi && __testo && !__testt && !__testa)
	{
	  __p_mode = O_RDWR;
	  __rw_mode = 0;
	}
      if (__testi && __testo && __testt && !__testa)
	{
	  __p_mode = O_RDWR | O_TRUNC | O_CREAT;
	  __rw_mode = 0;
	}
#ifdef O_BINARY
      if (__testb)
	__p_mode |= O_BINARY;
#endif	   
    }
  
  template<typename _CharT>
    __basic_file<_CharT>*
    __basic_file<_CharT>::sys_open(__c_file_type* __f, 
				   ios_base::openmode __mode) 
    {
      __basic_file* __ret = NULL;
      int __fd = fileno(__f);
      int __p_mode = 0;
      int __rw_mode = _IO_NO_READS + _IO_NO_WRITES; 
      char __c_mode[4];
      
      _M_open_mode(__mode, __p_mode, __rw_mode, __c_mode);

      if (!_IO_file_is_open(this))
	{
	  _fileno = __fd;
	  _flags &= ~(_IO_NO_READS + _IO_NO_WRITES);
	  _flags |= _IO_DELETE_DONT_CLOSE;
	  _offset = _IO_pos_BAD;
	  int __mask = _IO_NO_READS + _IO_NO_WRITES + _IO_IS_APPENDING;
	  _IO_mask_flags(this, __rw_mode, __mask);
	}

      return __ret;
    }
  
  template<typename _CharT>
    __basic_file<_CharT>* 
    __basic_file<_CharT>::open(const char* __name, ios_base::openmode __mode, 
			       int __prot)
    {
      __basic_file* __ret = NULL;
      int __p_mode = 0;
      int __rw_mode = _IO_NO_READS + _IO_NO_WRITES; 
      char __c_mode[4];

      _M_open_mode(__mode, __p_mode, __rw_mode, __c_mode);
      if (!_IO_file_is_open(this))
	{
	  __c_file_type* __f;
	  __f = _IO_file_open(this, __name, __p_mode, __prot, __rw_mode, 0);
	  __ret = __f ? this: NULL;
	}
      return __ret;
    }
  
  template<typename _CharT>
    bool 
    __basic_file<_CharT>::is_open() { return _fileno >= 0; }
  
  template<typename _CharT>
    __basic_file<_CharT>* 
    __basic_file<_CharT>::close()
    { 
      return _IO_file_close_it(this) ? static_cast<__basic_file*>(NULL) : this;
    }
 
  template<typename _CharT>
    streamsize 
    __basic_file<_CharT>::xsgetn(_CharT* __s, streamsize __n)
    { return _IO_file_xsgetn(this, __s, __n); }

  // NB: Unused.
  template<typename _CharT>
    streamsize 
    __basic_file<_CharT>::sys_read(_CharT* __s, streamsize __n) 
    { return _IO_file_read(this, __s, __n); }

  // NB: Unused.    
  template<typename _CharT>
    streamsize 
    __basic_file<_CharT>::sys_write(const _CharT* __s, streamsize __n) 
    { return _IO_file_write(this, __s, __n); }

  // NB: Unused.
  template<typename _CharT>
    streamoff
    __basic_file<_CharT>::sys_seek(streamoff __pos, ios_base::seekdir __way)
    { return _IO_file_seek(this, __pos, __way); }
  
  // NB: Unused.
  template<typename _CharT>
    int 
    __basic_file<_CharT>::sys_close() 
    { return _IO_file_close(this); }

  // NB: Unused.
  template<typename _CharT>
    int 
    __basic_file<_CharT>::sys_stat(void* __v) 
    { return _IO_file_stat(this, __v); }

  // NB: Unused.
  template<typename _CharT>
    int 
    __basic_file<_CharT>::showmanyc() { return EOF; }

  // NB: Unused.
  template<typename _CharT>
    void 
    __basic_file<_CharT>::imbue(void* /*__v*/) { }
}  // namespace std

#endif	// _CPP_BASIC_FILE
