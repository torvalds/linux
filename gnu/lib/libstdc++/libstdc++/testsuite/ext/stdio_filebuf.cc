// 2003-02-13  Paolo Carlini  <pcarlini@unitus.it>

// Copyright (C) 2003 Free Software Foundation, Inc.
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

// stdio_filebuf.h

#include <ext/stdio_filebuf.h>
#include <testsuite_hooks.h>

// { dg-do compile }

// libstdc++/9320
namespace test 
{
  using namespace std;
  using __gnu_cxx_test::pod_char;
  typedef short type_t;
  template class __gnu_cxx::stdio_filebuf<type_t, char_traits<type_t> >;
  template class __gnu_cxx::stdio_filebuf<pod_char, char_traits<pod_char> >;
} // test
