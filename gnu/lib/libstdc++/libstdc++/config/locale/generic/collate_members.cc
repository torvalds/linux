// std::collate implementation details, generic version -*- C++ -*-

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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
// ISO C++ 14882: 22.2.4.1.2  collate virtual functions
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <locale>

namespace std
{
  // These are basically extensions to char_traits, and perhaps should
  // be put there instead of here.
  template<>
    int 
    collate<char>::_M_compare(const char* __one, const char* __two) const
    { 
      int __cmp = strcoll(__one, __two);
      return (__cmp >> (8 * sizeof (int) - 2)) | (__cmp != 0);
    }
  
  template<>
    size_t
    collate<char>::_M_transform(char* __to, const char* __from, 
				size_t __n) const
    { return strxfrm(__to, __from, __n); }

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<>
    int 
    collate<wchar_t>::_M_compare(const wchar_t* __one, 
				 const wchar_t* __two) const
    {
      int __cmp = wcscoll(__one, __two);
      return (__cmp >> (8 * sizeof (int) - 2)) | (__cmp != 0);
    }
  
  template<>
    size_t
    collate<wchar_t>::_M_transform(wchar_t* __to, const wchar_t* __from,
				   size_t __n) const
    { return wcsxfrm(__to, __from, __n); }
#endif
}
