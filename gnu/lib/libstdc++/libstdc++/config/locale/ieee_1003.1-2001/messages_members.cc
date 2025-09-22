// std::messages implementation details, IEEE 1003.1-200x version -*- C++ -*-

// Copyright (C) 2001 Free Software Foundation, Inc.
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
// ISO C++ 14882: 22.2.7.1.2  messages virtual functions
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <locale>

namespace std
{
  // Specializations
  template<>
    string
    messages<char>::do_get(catalog __c, int __setid, int __msgid, 
			   const string& __dfault) const
    {
      nl_catd __nlc = reinterpret_cast<nl_catd>(__c);
      return string(catgets(__nlc, __setid, __msgid, __dfault.c_str())); 
    }

#ifdef _GLIBCPP_USE_WCHAR_T
  template<>
    wstring
    messages<wchar_t>::do_get(catalog, int, int, const wstring& __dfault) const
    {
      nl_catd __nlc = reinterpret_cast<nl_catd>(__c);
      char* __msg = catgets(__nlc, __setid, __msgid, 
			    _M_convert_to_char(__dfault));
      return _M_convert_from_char(__msg);
    }
#endif
}
