// 1999-06-09 bkoz

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

// 21.3.6.1 basic_string find

#include <string>
#include <stdexcept>
#include <testsuite_hooks.h>

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

  // size_type find(const string&, size_type pos = 0) const;
  csz01 = str01.find(str01);
  VERIFY( csz01 == 0 );
  csz01 = str01.find(str01, 4);
  VERIFY( csz01 == npos );
  csz01 = str01.find(str02, 0);
  VERIFY( csz01 == 0 );
  csz01 = str01.find(str02, 3);
  VERIFY( csz01 == npos );
  csz01 = str01.find(str03, 0);
  VERIFY( csz01 == 8 );
  csz01 = str01.find(str03, 3);
  VERIFY( csz01 == 8 );
  csz01 = str01.find(str03, 12);
  VERIFY( csz01 == npos );

  // An empty string consists of no characters
  // therefore it should be found at every point in a string,
  // except beyond the end
  csz01 = str01.find(str04, 0);
  VERIFY( csz01 == 0 );
  csz01 = str01.find(str04, 5);
  VERIFY( csz01 == 5 );
  csz01 = str01.find(str04, str01.size());
  VERIFY( csz01 == str01.size() ); 
  csz01 = str01.find(str04, str01.size()+1);
  VERIFY( csz01 == npos ); 
  
  // size_type find(const char* s, size_type pos, size_type n) const;
  csz01 = str01.find(str_lit01, 0, 3);
  VERIFY( csz01 == 0 );
  csz01 = str01.find(str_lit01, 3, 0);
  VERIFY( csz01 == 3 );

  // size_type find(const char* s, size_type pos = 0) const;
  csz01 = str01.find(str_lit01);
  VERIFY( csz01 == 0 );
  csz01 = str01.find(str_lit01, 3);
  VERIFY( csz01 == npos );

  // size_type find(char c, size_type pos = 0) const;
  csz01 = str01.find('z');
  csz02 = str01.size() - 1;
  VERIFY( csz01 == csz02 );
  csz01 = str01.find('/');
  VERIFY( csz01 == npos );
   
  // size_type find_first_of(const string&, size_type pos = 0) const;
  std::string str05("xena rulez");
  csz01 = str01.find_first_of(str01);
  VERIFY( csz01 == 0 );
  csz01 = str01.find_first_of(str01, 4);
  VERIFY( csz01 == 4 );
  csz01 = str01.find_first_of(str02, 0);
  VERIFY( csz01 == 0 );
  csz01 = str01.find_first_of(str02, 3);
  VERIFY( csz01 == 3 );
  csz01 = str01.find_first_of(str03, 0);
  VERIFY( csz01 == 8 );
  csz01 = str01.find_first_of(str03, 3);
  VERIFY( csz01 == 8 );
  csz01 = str01.find_first_of(str03, 12);
  VERIFY( csz01 == 16 );
  csz01 = str01.find_first_of(str05, 0);
  VERIFY( csz01 == 1 );
  csz01 = str01.find_first_of(str05, 4);
  VERIFY( csz01 == 4 );

  // An empty string consists of no characters
  // therefore it should be found at every point in a string,
  // except beyond the end
  // However, str1.find_first_of(str2,pos) finds the first character in 
  // str1 (starting at pos) that exists in str2, which is none for empty str2
  csz01 = str01.find_first_of(str04, 0);
  VERIFY( csz01 == npos );
  csz01 = str01.find_first_of(str04, 5);
  VERIFY( csz01 == npos );
  
  // size_type find_first_of(const char* s, size_type pos, size_type n) const;
  csz01 = str01.find_first_of(str_lit01, 0, 3);
  VERIFY( csz01 == 0 );
  csz01 = str01.find_first_of(str_lit01, 3, 0);
  VERIFY( csz01 == npos );

  // size_type find_first_of(const char* s, size_type pos = 0) const;
  csz01 = str01.find_first_of(str_lit01);
  VERIFY( csz01 == 0 );
  csz01 = str01.find_first_of(str_lit01, 3);
  VERIFY( csz01 == 3 );

  // size_type find_first_of(char c, size_type pos = 0) const;
  csz01 = str01.find_first_of('z');
  csz02 = str01.size() - 1;
  VERIFY( csz01 == csz02 );

  // size_type find_last_of(const string& str, size_type pos = 0) const;
  // size_type find_last_of(const char* s, size_type pos, size_type n) const;
  // size_type find_last_of(const char* s, size_type pos = 0) const;
  // size_type find_last_of(char c, size_type pos = 0) const;

#if 1
// from tstring.cc, from jason merrill, et. al.
  std::string x;
  std::string::size_type pos;
  pos = x.find_last_not_of('X');
  VERIFY( pos == npos );
  pos = x.find_last_not_of("XYZ");
  VERIFY( pos == npos );

  std::string y("a");
  pos = y.find_last_not_of('X');
  VERIFY( pos == 0 );
  pos = y.find_last_not_of('a');
  VERIFY( pos == npos );
  pos = y.find_last_not_of("XYZ");
  VERIFY( pos == 0 );
  pos = y.find_last_not_of("a");
  VERIFY( pos == npos );

  std::string z("ab");
  pos = z.find_last_not_of('X');
  VERIFY( pos == 1 );
  pos = z.find_last_not_of("XYZ");
  VERIFY( pos == 1 );
  pos = z.find_last_not_of('b');
  VERIFY( pos == 0 );
  pos = z.find_last_not_of("Xb");
  VERIFY( pos == 0 );
  pos = z.find_last_not_of("Xa");
  VERIFY( pos == 1 );
  pos = z.find_last_of("ab");
  VERIFY( pos == 1 );
  pos = z.find_last_of("Xa");
  VERIFY( pos == 0 );
  pos = z.find_last_of("Xb");
  VERIFY( pos == 1 );
  pos = z.find_last_of("XYZ");
  VERIFY( pos == std::string::npos );
  pos = z.find_last_of('a');
  VERIFY( pos == 0 );
  pos = z.find_last_of('b');
  VERIFY( pos == 1 );
  pos = z.find_last_of('X');
  VERIFY( pos == std::string::npos );
#endif

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
