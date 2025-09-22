// 1999-06-03 bkoz

// Copyright (C) 1999, 2003 Free Software Foundation, Inc.
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

// 21.3.5.4 basic_string::insert

#include <string>
#include <stdexcept>
#include <testsuite_hooks.h>

int test01(void)
{
  bool test = true;
  typedef std::string::size_type csize_type;
  typedef std::string::iterator citerator;
  csize_type npos = std::string::npos;
  csize_type csz01, csz02;

  const std::string str01("rodeo beach, marin");
  const std::string str02("baker beach, san francisco");
  std::string str03;

  // string& insert(size_type p1, const string& str, size_type p2, size_type n)
  // requires:
  //   1) p1 <= size()
  //   2) p2 <= str.size()
  //   3) rlen = min(n, str.size() - p2)
  // throws:
  //   1) out_of_range if p1 > size() || p2 > str.size()
  //   2) length_error if size() >= npos - rlen
  // effects:
  // replaces *this with new string of length size() + rlen such that
  // nstr[0]  to nstr[p1] == thisstr[0] to thisstr[p1]
  // nstr[p1 + 1] to nstr[p1 + rlen] == str[p2] to str[p2 + rlen]
  // nstr[p1 + 1 + rlen] to nstr[...] == thisstr[p1 + 1] to thisstr[...]  
  str03 = str01; 
  csz01 = str03.size();
  csz02 = str02.size();
  try {
    str03.insert(csz01 + 1, str02, 0, 5);
    VERIFY( false );
  }		 
  catch(std::out_of_range& fail) {
    VERIFY( true );
  }
  catch(...) {
    VERIFY( false );
  }

  str03 = str01; 
  csz01 = str03.size();
  csz02 = str02.size();
  try {
    str03.insert(0, str02, csz02 + 1, 5);
    VERIFY( false );
  }		 
  catch(std::out_of_range& fail) {
    VERIFY( true );
  }
  catch(...) {
    VERIFY( false );
  }

  csz01 = str01.max_size();
  try {
    std::string str04(csz01, 'b'); 
    str03 = str04; 
    csz02 = str02.size();
    try {
      str03.insert(0, str02, 0, 5);
      VERIFY( false );
    }		 
    catch(std::length_error& fail) {
      VERIFY( true );
    }
    catch(...) {
      VERIFY( false );
    }
  }
  catch(std::bad_alloc& failure){
    VERIFY( true ); 
  }
  catch(std::exception& failure){
    VERIFY( false );
  }

  str03 = str01; 
  csz01 = str03.size();
  csz02 = str02.size();
  str03.insert(13, str02, 0, 12); 
  VERIFY( str03 == "rodeo beach, baker beach,marin" );

  str03 = str01; 
  csz01 = str03.size();
  csz02 = str02.size();
  str03.insert(0, str02, 0, 12); 
  VERIFY( str03 == "baker beach,rodeo beach, marin" );

  str03 = str01; 
  csz01 = str03.size();
  csz02 = str02.size();
  str03.insert(csz01, str02, 0, csz02); 
  VERIFY( str03 == "rodeo beach, marinbaker beach, san francisco" );

  // string& insert(size_type __p, const string& string);
  // insert(p1, str, 0, npos)
  str03 = str01; 
  csz01 = str03.size();
  csz02 = str02.size();
  str03.insert(csz01, str02); 
  VERIFY( str03 == "rodeo beach, marinbaker beach, san francisco" );

  str03 = str01; 
  csz01 = str03.size();
  csz02 = str02.size();
  str03.insert(0, str02); 
  VERIFY( str03 == "baker beach, san franciscorodeo beach, marin" );

  // string& insert(size_type __p, const char* s, size_type n);
  // insert(p1, string(s,n))
  str03 = str02; 
  csz01 = str03.size();
  str03.insert(0, "-break at the bridge", 20); 
  VERIFY( str03 == "-break at the bridgebaker beach, san francisco" );

  // string& insert(size_type __p, const char* s);
  // insert(p1, string(s))
  str03 = str02; 
  str03.insert(0, "-break at the bridge"); 
  VERIFY( str03 == "-break at the bridgebaker beach, san francisco" );

  // string& insert(size_type __p, size_type n, char c)
  // insert(p1, string(n,c))
  str03 = str02; 
  csz01 = str03.size();
  str03.insert(csz01, 5, 'z'); 
  VERIFY( str03 == "baker beach, san franciscozzzzz" );

  // iterator insert(iterator p, char c)
  // inserts a copy of c before the character referred to by p
  str03 = str02; 
  citerator cit01 = str03.begin();
  str03.insert(cit01, 'u'); 
  VERIFY( str03 == "ubaker beach, san francisco" );

  // iterator insert(iterator p, size_type n,  char c)
  // inserts n copies of c before the character referred to by p
  str03 = str02; 
  cit01 = str03.begin();
  str03.insert(cit01, 5, 'u'); 
  VERIFY( str03 == "uuuuubaker beach, san francisco" );

  // template<inputit>
  //   void 
  //   insert(iterator p, inputit first, inputit, last)
  // ISO-14882: defect #7 part 1 clarifies this member function to be:
  // insert(p - begin(), string(first,last))
  str03 = str02; 
  csz01 = str03.size();
  str03.insert(str03.begin(), str01.begin(), str01.end()); 
  VERIFY( str03 == "rodeo beach, marinbaker beach, san francisco" );

  str03 = str02; 
  csz01 = str03.size();
  str03.insert(str03.end(), str01.begin(), str01.end()); 
  VERIFY( str03 == "baker beach, san franciscorodeo beach, marin" );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return test;
}

// Once more
//   string& insert(size_type __p, const char* s, size_type n);
//   string& insert(size_type __p, const char* s);
// but now s points inside the _Rep
int test02(void)
{
  bool test = true;

  std::string str01;
  const char* title = "Everything was beautiful, and nothing hurt";
  // Increasing size: str01 is reallocated every time.
  str01 = title;
  str01.insert(0, str01.c_str() + str01.size() - 4, 4);
  VERIFY( str01 == "hurtEverything was beautiful, and nothing hurt" );
  str01 = title;
  str01.insert(0, str01.c_str(), 5);
  VERIFY( str01 == "EveryEverything was beautiful, and nothing hurt" );
  str01 = title;
  str01.insert(10, str01.c_str() + 4, 6);
  VERIFY( str01 == "Everythingything was beautiful, and nothing hurt" );
  str01 = title;
  str01.insert(15, str01.c_str(), 10);
  VERIFY( str01 == "Everything was Everythingbeautiful, and nothing hurt" );
  str01 = title;
  str01.insert(15, str01.c_str() + 11, 13);
  VERIFY( str01 == "Everything was was beautifulbeautiful, and nothing hurt" );
  str01 = title;
  str01.insert(0, str01.c_str());
  VERIFY( str01 == "Everything was beautiful, and nothing hurt"
	  "Everything was beautiful, and nothing hurt");
  // Again: no reallocations.
  str01 = title;
  str01.insert(0, str01.c_str() + str01.size() - 4, 4);
  VERIFY( str01 == "hurtEverything was beautiful, and nothing hurt" );
  str01 = title;
  str01.insert(0, str01.c_str(), 5);
  VERIFY( str01 == "EveryEverything was beautiful, and nothing hurt" );
  str01 = title;
  str01.insert(10, str01.c_str() + 4, 6);
  VERIFY( str01 == "Everythingything was beautiful, and nothing hurt" );
  str01 = title;
  str01.insert(15, str01.c_str(), 10);
  VERIFY( str01 == "Everything was Everythingbeautiful, and nothing hurt" );
  str01 = title;
  str01.insert(15, str01.c_str() + 11, 13);
  VERIFY( str01 == "Everything was was beautifulbeautiful, and nothing hurt" );
  str01 = title;
  str01.insert(0, str01.c_str());
  VERIFY( str01 == "Everything was beautiful, and nothing hurt"
	  "Everything was beautiful, and nothing hurt");

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return test;
}

int main()
{ 
  __gnu_cxx_test::set_memory_limits();
  test01();
  test02();
  return 0;
}
