// 1999-07-22 bkoz

// Copyright (C) 1994, 1999, 2000 Free Software Foundation, Inc.
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

// 27.6.2.7 standard basic_ostream manipulators

#include <ostream>
#include <sstream>
#include <stdexcept>
#include <testsuite_hooks.h>

bool test01(void)
{
  bool test = true;

  const char str_lit01[] = "  venice ";
  const std::string str01(" santa barbara ");
  std::string str02(str_lit01);
  std::string str04;
  std::string str05;
  std::ios_base::iostate flag1, flag2, flag3, flag4, flag5;

  // template<_CharT, _Traits>
  //  basic_ostream<_CharT, _Traits>& endl(basic_ostream<_Char, _Traits>& os)
  std::ostringstream oss01(str01);
  std::ostringstream oss02;
  std::ostringstream::int_type i01, i02;
  typedef std::ostringstream::traits_type traits_type;

  oss01 << std::endl;
  str04 = oss01.str();
  VERIFY( str04.size() == str01.size() );

  oss02 << std::endl;
  str05 = oss02.str();
  VERIFY( str05.size() == 1 );

  // template<_CharT, _Traits>
  //  basic_ostream<_CharT, _Traits>& ends(basic_ostream<_Char, _Traits>& os)
  oss01 << std::ends;
  str04 = oss01.str();
  VERIFY( str04.size() == str01.size() );
  VERIFY( str04[1] == char() );

  oss02 << std::ends;
  str05 = oss02.str();
  VERIFY( str05.size() == 2 );
  VERIFY( str05[1] == char() );

  // template<_CharT, _Traits>
  //  basic_ostream<_CharT, _Traits>& flush(basic_ostream<_Char, _Traits>& os)
  oss01.flush();
  str04 = oss01.str();
  VERIFY( str04.size() == str01.size() );

  oss02.flush();
  str05 = oss02.str();
  VERIFY( str05.size() == 2 );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return test;
}


// based vaguely on this:
// http://gcc.gnu.org/ml/libstdc++/2000-q2/msg00109.html
bool test02()
{
  using namespace std;
  typedef ostringstream::int_type int_type;

  bool test = true;
  ostringstream osst_01;
  const string str_00("herbie_hancock");
  int_type len1 = str_00.size();
  osst_01 << str_00;
  VERIFY( osst_01.str().size() == len1 );

  osst_01 << ends;

  const string str_01("speak like a child");
  int_type len2 = str_01.size();
  osst_01 << str_01;
  int_type len3 = osst_01.str().size();
  VERIFY( len1 < len3 );
  VERIFY( len3 == len1 + len2 + 1 );

  osst_01 << ends;

  const string str_02("+ inventions and dimensions");
  int_type len4 = str_02.size();
  osst_01 << str_02;
  int_type len5 = osst_01.str().size();
  VERIFY( len3 < len5 );
  VERIFY( len5 == len3 + len4 + 1 );

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
