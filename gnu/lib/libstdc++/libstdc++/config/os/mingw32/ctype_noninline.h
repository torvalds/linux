// Locale support -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2002 Free Software Foundation, Inc.
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
// ISO C++ 14882: 22.1  Locales
//
  
// Information as gleaned from /mingw32/include/ctype.h.

  // This should be in mingw's ctype.h but isn't in older versions
  // Static classic C-locale table.  _ctype[0] is EOF
  extern "C"  unsigned short  __declspec(dllimport) _ctype[];

  const ctype_base::mask*
  ctype<char>::classic_table() throw()
  { return _ctype + 1; }  

  ctype<char>::ctype(__c_locale, const mask* __table, bool __del, 
		     size_t __refs) 
  : __ctype_abstract_base<char>(__refs), _M_del(__table != 0 && __del), 
  _M_toupper(NULL), _M_tolower(NULL),
  _M_table(__table ? __table : classic_table())  
  { }

  ctype<char>::ctype(const mask* __table, bool __del, size_t __refs) 
  : __ctype_abstract_base<char>(__refs), _M_del(__table != 0 && __del), 
  _M_toupper(NULL), _M_tolower(NULL),
  _M_table(__table ? __table : classic_table()) 
  { }

  char
  ctype<char>::do_toupper(char __c) const
  { return (this->is(ctype_base::lower, __c) ? (__c - 'a' + 'A') : __c); }

  const char*
  ctype<char>::do_toupper(char* __low, const char* __high) const
  {
    while (__low < __high)
      {
	*__low = this->do_toupper(*__low);
	++__low;
      }
    return __high;
  }

  char
  ctype<char>::do_tolower(char __c) const
  { return (this->is(ctype_base::upper, __c) ? (__c - 'A' + 'a') : __c); }

  const char* 
  ctype<char>::do_tolower(char* __low, const char* __high) const
  {
    while (__low < __high)
      {
	*__low = this->do_tolower(*__low);
	++__low;
      }
    return __high;
  }




