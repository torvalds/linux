// 1999-07-22 bkoz

// Copyright (C) 1994, 1999, 2001 Free Software Foundation, Inc.
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

// 27.6.1.4 standard basic_istream manipulators

#include <istream>
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
  //  basic_istream<_CharT, _Traits>& ws(basic_istream<_Char, _Traits>& is)
  std::istringstream iss01(str01);
  std::istringstream iss02(str01);
  
  iss01 >> str04;
  VERIFY( str04.size() != str01.size() );
  VERIFY( str04 == "santa" );

  iss02 >> std::ws;
  iss02 >> str05;
  VERIFY( str05.size() != str01.size() );
  VERIFY( str05 == "santa" );
  VERIFY( str05 == str04 );

  iss01 >> str04;
  VERIFY( str04.size() != str01.size() );
  VERIFY( str04 == "barbara" );
  
  iss02 >> std::ws;
  iss02 >> str05;
  VERIFY( str05.size() != str01.size() );
  VERIFY( str05 == "barbara" );
  VERIFY( str05 == str04 );

  flag3 = std::ios_base::eofbit;
  flag4 = std::ios_base::badbit;
  flag5 = std::ios_base::failbit;
  VERIFY( !iss01.fail() );
  VERIFY( !iss02.fail() );
  VERIFY( !iss01.eof() );
  VERIFY( !iss02.eof() );

  iss01 >> std::ws;
  VERIFY( !iss01.fail() );
  VERIFY( iss01.eof() );

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}

int main()
{ 
  test01();
  return 0;
}
