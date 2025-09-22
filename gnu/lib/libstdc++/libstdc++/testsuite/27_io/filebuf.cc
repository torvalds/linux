// 1999-01-17 bkoz test functionality of basic_filebuf for char_type == char

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003
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

// 27.8.1.1 - Template class basic_filebuf 
// NB: This file is for testing basic_filebuf with NO OTHER INCLUDES.

#include <fstream>
#include <testsuite_hooks.h>

// { dg-do compile }

// libstdc++/7216
void test01()
{
  // Check for required typedefs
  typedef std::filebuf test_type;
  typedef test_type::char_type char_type;
  typedef test_type::traits_type traits_type;
  typedef test_type::int_type int_type;
  typedef test_type::pos_type pos_type;
  typedef test_type::off_type off_type;
}

// test05
// libstdc++/1886
// should be able to instantiate basic_filebuf for non-standard types.
namespace test 
{
  using namespace std;
  using __gnu_cxx_test::pod_char;
  typedef short type_t;
  template class basic_filebuf<type_t, char_traits<type_t> >;
  template class basic_filebuf<pod_char, char_traits<pod_char> >;
} // test


// test07
// libstdc++/2020
// should be able to use custom char_type
class gnu_char_type
{
  unsigned long character;
public:
  // operator ==
  bool
  operator==(const gnu_char_type& __lhs) 
  { return character == __lhs.character; }

  // operator <
  bool
  operator<(const gnu_char_type& __lhs) 
  { return character < __lhs.character; }

  // default ctor
  gnu_char_type() { }

  // to_char_type
  gnu_char_type(const unsigned long& __l) : character(__l) { } 

  // to_int_type
  operator unsigned long() const { return character; }
};

void test07()
{
  bool test = true;
  typedef std::basic_filebuf<gnu_char_type> gnu_filebuf;
  
  try
    { gnu_filebuf obj; }
  catch(std::exception& obj)
    { 
      test = false; 
      VERIFY( test );
    }
}

#if !__GXX_WEAK__
// Explicitly instantiate for systems with no COMDAT or weak support.
template 
  std::basic_streambuf<gnu_char_type>::int_type
  std::basic_streambuf<gnu_char_type>::_S_pback_size;
#endif

int main() 
{
  test01();
  test07();
  return 0;
}



// more surf!!!









