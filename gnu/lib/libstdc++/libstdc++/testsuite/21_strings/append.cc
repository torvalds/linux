// 1999-07-08 bkoz

// Copyright (C) 1999 Free Software Foundation, Inc.
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

// 21.3.5.3 basic_string::assign

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

  const char str_lit01[] = "point bolivar, texas";
  const std::string str01(str_lit01);
  const std::string str02("corpus, ");
  const std::string str03;
  std::string str05;


  // string& append(const string&)
  str05 = str02;
  str05.append(str05); 
  VERIFY( str05 == "corpus, corpus, " );
  str05.append(str01);
  VERIFY( str05 == "corpus, corpus, point bolivar, texas" );
  str05.append(str03);
  VERIFY( str05 == "corpus, corpus, point bolivar, texas" );
  std::string str06;
  str06.append(str05);
  VERIFY( str06 == str05 );


  // string& append(const string&, size_type pos, size_type n)
  str05.erase();
  str06.erase();
  csz01 = str03.size();
  try {
    str06.append(str03, csz01 + 1, 0);
    VERIFY( false ); 
  }
  catch(std::out_of_range& fail) {
    VERIFY( true );
  }
  catch(...) {
    VERIFY( false );
  }

  csz01 = str01.size();
  try {
    str06.append(str01, csz01 + 1, 0);
    VERIFY( false ); 
  }
  catch(std::out_of_range& fail) {
    VERIFY( true );
  }
  catch(...) {
    VERIFY( false );
  }

  str05 = str02;
  str05.append(str01, 0, std::string::npos);
  VERIFY( str05 == "corpus, point bolivar, texas" );
  VERIFY( str05 != str02 );

  str06 = str02;
  str06.append(str01, 15, std::string::npos);
  VERIFY( str06 == "corpus, texas" );
  VERIFY( str02 != str06 );


  // string& append(const char* s)
  str05.erase();
  str06.erase();
  str05.append("");
  VERIFY( str05 == str03 );

  str05.append(str_lit01);
  VERIFY( str05 == str01 );

  str06 = str02;
  str06.append("corpus, ");
  VERIFY( str06 == "corpus, corpus, " );


  // string& append(const char* s, size_type n)
  str05.erase();
  str06.erase();
  str05.append("", 0);
  VERIFY( str05.size() == 0 );
  VERIFY( str05 == str03 );
  
  str05.append(str_lit01, sizeof(str_lit01) - 1);
  VERIFY( str05 == str01 );

  str06 = str02;
  str06.append("corpus, ", 6);
  VERIFY( str06 == "corpus, corpus" );

  str06 = str02;
  str06.append("corpus, ", 12);
  VERIFY( str06 != "corpus, corpus, " );


  // string& append(size_type n, char c)
  str05.erase();
  str06.erase();
  str05.append(0, 'a');
  VERIFY( str05 == str03 );
  str06.append(8, '.');
  VERIFY( str06 == "........" );


  // template<typename InputIter>
  //  string& append(InputIter first, InputIter last)
  str05.erase();
  str06.erase();
  str05.append(str03.begin(), str03.end());
  VERIFY( str05 == str03 );

  str06 = str02;
  str06.append(str01.begin(), str01.begin() + str01.find('r')); 
  VERIFY( str06 == "corpus, point boliva" );
  VERIFY( str06 != str01 );
  VERIFY( str06 != str02 );

  str05 = str01;
  str05.append(str05.begin(), str05.begin() + str05.find('r')); 
  VERIFY( str05 ==  "point bolivar, texaspoint boliva" );
  VERIFY( str05 != str01 );

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
