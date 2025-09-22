// 1999-06-08 bkoz

// Copyright (C) 1999, 2000, 2002 Free Software Foundation, Inc.
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

// 23.3.5.1 bitset constructors

#include <string>
#include <bitset>
#include <stdexcept>
#include <testsuite_hooks.h>

bool test01(void)
{
  bool test = true;

  // bitset()
  const size_t n1 = 5;
  std::bitset<n1> bit01;
  for (int i = 0; i < n1; ++i)
    VERIFY( !bit01.test(i) );

  // bitset(unsigned long)
  const size_t n2 = 32;
  unsigned long ul1 = 2;
  std::bitset<n2> bit02(ul1);
  VERIFY( !bit02.test(0) );
  VERIFY( bit02.test(1) );
  VERIFY( !bit02.test(2) );

  // template<_CharT, _Traits, _Alloc>
  // explicit bitset(const basic_string<_C,_T,_A>&, size_type pos, size_type n)
  std::string str01("stuff smith sessions");
  const size_t n3 = 128;
  try {
    std::bitset<n3> bit03(str01, 5);
  }
  catch(std::invalid_argument& fail) {
    VERIFY( true );
  }
  catch(...) {
    VERIFY( false );
  }

  std::string str02("010101000011");
  int sz = str02.size();
  try {
    std::bitset<n3> bit03(str02, 0);
    std::string str03;
    for (int i = 0; i < sz; ++i)
      str03 += (bit03.test(i) ? '1' : '0');
    std::reverse(str03.begin(), str03.end());
    VERIFY( str03 == str02 );
  }
  catch(std::invalid_argument& fail) {
    VERIFY( false );
  }
  catch(...) {
    VERIFY( false );
  }
  

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return test;
}

// boundary condition:  a zero-sized set
// libstdc++/6282
bool test02(void)
{
  using std::char_traits;  using std::allocator;
  bool test = true;

  std::bitset<0>  z1;
  VERIFY( z1.any() == false );

  std::bitset<0>  z2(12345);
  VERIFY( z2.any() == false );

  std::bitset<0>  z3(std::string("10101010101"));
  VERIFY( z3.any() == false );

  try {
    z1.set(0);
    VERIFY( false );
  }
  catch(std::out_of_range& fail) {
    VERIFY( true );
  }
  catch(...) {
    VERIFY( false );
  }

  VERIFY( z1.to_ulong() == 0 );
  VERIFY( ( z1.to_string<char,char_traits<char>,allocator<char> >().empty() ) );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return test;
}

int main()
{
  test01();
  test02();

  return 0;
}
