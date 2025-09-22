// 2002-07-25 Benjamin Kosnik <bkoz@redhat.com>

// Copyright (C) 2002, 2003 Free Software Foundation, Inc.
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

// 27.7.4 - Template class basic_stringstream
// NB: This file is for testing basic_stringstream with NO OTHER INCLUDES.

#include <sstream>
#include <testsuite_hooks.h>

// { dg-do compile }

// libstdc++/7216
void test01()
{
  // Check for required typedefs
  typedef std::stringstream test_type;
  typedef test_type::char_type char_type;
  typedef test_type::traits_type traits_type;
  typedef test_type::int_type int_type;
  typedef test_type::pos_type pos_type;
  typedef test_type::off_type off_type;
}

namespace test 
{
  using namespace std;
  using __gnu_cxx_test::pod_char;
  typedef short type_t;
  template class basic_stringstream<type_t, char_traits<type_t> >;
  template class basic_stringstream<pod_char, char_traits<pod_char> >;
} // test

// libstdc++/9826
void test02()
{
  using namespace std;
  using __gnu_cxx_test::pod_char;

  basic_stringstream<pod_char, char_traits<pod_char> > sstr;
  // 1
  basic_string<pod_char, char_traits<pod_char> > str;
  sstr >> str;
  // 2
  pod_char*  chr;
  sstr >> chr;
  // 3
  sstr >> ws;
}

int main() 
{
  test01();
  test02();
  return 0;
}
