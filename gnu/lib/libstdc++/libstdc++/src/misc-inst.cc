// Explicit instantiation file.

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002
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
// ISO C++ 14882:
//

#include <string>
#include <istream>
#include <ostream>
#include <algorithm>
#include <vector>
#include <bits/atomicity.h>

namespace std
{
  // string related to iostreams
  template 
    basic_istream<char>& 
    operator>>(basic_istream<char>&, string&);
  template 
    basic_ostream<char>& 
    operator<<(basic_ostream<char>&, const string&);
  template 
    basic_istream<char>& 
    getline(basic_istream<char>&, string&, char);
  template 
    basic_istream<char>& 
    getline(basic_istream<char>&, string&);
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template 
    basic_istream<wchar_t>& 
    operator>>(basic_istream<wchar_t>&, wstring&);
  template 
    basic_ostream<wchar_t>& 
    operator<<(basic_ostream<wchar_t>&, const wstring&);
  template 
    basic_istream<wchar_t>& 
    getline(basic_istream<wchar_t>&, wstring&, wchar_t);
  template 
    basic_istream<wchar_t>& 
    getline(basic_istream<wchar_t>&, wstring&);
#endif
#ifdef _GLIBCPP_INST_ATOMICITY_LOCK
  template volatile int __Atomicity_lock<0>::_S_atomicity_lock;
#endif

#if 1
  // XXX
  // 2002-05-24 These are no longer needed and should eventually be deleted.
  template 
    string* 
    __uninitialized_fill_n_aux<string*, size_t, string>
    (string*, size_t, string const &, __false_type);

  template 
    string* 
    __uninitialized_copy_aux<vector<string>::const_iterator, string *>
    (vector<string>::const_iterator, vector<string>::const_iterator, 
     string*, __false_type);
#endif
} // namespace std

#ifdef _GLIBCPP_NEED_GENERIC_MUTEX
namespace __gnu_cxx
{
#ifdef __GTHREAD_MUTEX_INIT
  __gthread_mutex_t _Atomic_add_mutex = __GTHREAD_MUTEX_INIT;
#else
  // generic atomicity.h without static initialization
  __gthread_mutex_t _Atomic_add_mutex;
  __gthread_once_t _Atomic_add_mutex_once = __GTHREAD_ONCE_INIT;
  void __gthread_atomic_add_mutex_once()
  {
    __GTHREAD_MUTEX_INIT_FUNCTION (&_Atomic_add_mutex);
  }
#endif
} // namespace __gnu_cxx
#endif // _GLIBCPP_NEED_GLOBAL_MUTEX

