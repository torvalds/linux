// 2000-06-22 -=dbv=-  (shamelessy copied from bkoz' find.cc)

// Copyright (C) 2000 Free Software Foundation, Inc.
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

#include <string>
#include <stdexcept>
#include <testsuite_hooks.h>

// 21.3.6.2 basic_string rfind
bool test01(void)
{
  bool test = true;
  typedef std::string::size_type csize_type;
  typedef std::string::const_reference cref;
  typedef std::string::reference ref;
  csize_type npos = std::string::npos;
  csize_type csz01, csz02;

  const char str_lit01[] = "mave";
  const std::string str01("mavericks, santa cruz");
  std::string str02(str_lit01);
  std::string str03("s, s");
  std::string str04;

  // size_type rfind(const string&, size_type pos = 0) const;
  csz01 = str01.rfind(str01);
  VERIFY( csz01 == 0 );
  csz01 = str01.rfind(str01, 4);
  VERIFY( csz01 == 0 );
  csz01 = str01.rfind(str02,3);
  VERIFY( csz01 == 0 );
  csz01 = str01.rfind(str02);
  VERIFY( csz01 == 0 );
  csz01 = str01.rfind(str03);
  VERIFY( csz01 == 8 );
  csz01 = str01.rfind(str03, 3);
  VERIFY( csz01 == npos );
  csz01 = str01.rfind(str03, 12);
  VERIFY( csz01 == 8 );

  // An empty string consists of no characters
  // therefore it should be found at every point in a string,
  // except beyond the end
  csz01 = str01.rfind(str04, 0);
  VERIFY( csz01 == 0 );
  csz01 = str01.rfind(str04, 5);
  VERIFY( csz01 == 5 );
  csz01 = str01.rfind(str04, str01.size());
  VERIFY( csz01 == str01.size() );
  csz01 = str01.rfind(str04, str01.size()+1);
  VERIFY( csz01 == str01.size() );

  // size_type rfind(const char* s, size_type pos, size_type n) const;
  csz01 = str01.rfind(str_lit01, 0, 3);
  VERIFY( csz01 == 0 );
  csz01 = str01.rfind(str_lit01, 3, 0);
  VERIFY( csz01 == 3 );

  // size_type rfind(const char* s, size_type pos = 0) const;
  csz01 = str01.rfind(str_lit01);
  VERIFY( csz01 == 0 );
  csz01 = str01.rfind(str_lit01, 3);
  VERIFY( csz01 == 0 );

  // size_type rfind(char c, size_type pos = 0) const;
  csz01 = str01.rfind('z');
  csz02 = str01.size() - 1;
  VERIFY( csz01 == csz02 );
  csz01 = str01.rfind('/');
  VERIFY( csz01 == npos );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return test;
}

// 21.3.6.4 basic_string::find_last_of
bool test02()
{
  bool test = true;

  // test find_last_of

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return test;
}

// 21.3.6.6 basic_string::find_last_not_of
bool test03()
{
  bool test = true;

  // test find_last_not_of

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return test;
}
int main()
{
  test01();
  test02();
  test03();
  return 0;
}
