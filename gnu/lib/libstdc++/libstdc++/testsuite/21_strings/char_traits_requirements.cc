// 1999-06-03 bkoz

// Copyright (C) 1999, 2000, 2001 Free Software Foundation, Inc.
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

// 21.1.1 Characher traits requirements

#include <string>
#include <testsuite_hooks.h>

int test01(void)
{
  bool test = true;
  const std::string str_01("zuma beach");
  const std::string str_02("montara and ocean beach");

  // 21.1.1 character traits requirements

  // Key for decoding what function signatures really mean:
  // X 		== char_traits<_CharT>
  // [c,d] 	== _CharT
  // [p,q] 	== const _CharT*
  // s 		== _CharT*
  // [n,i,j] 	== size_t
  // f 		== X::int_type
  // pos 	== X::pos_type
  // state 	== X::state_type

  // void X::assign(char c, char d)
  // assigns c = d;
  char c1 = 'z';
  char c2 = 'u';
  VERIFY( c1 != c2 );
  std::char_traits<char>::assign(c1,c2);
  VERIFY( c1 == 'u' );

  // char* X::move(char* s, const char* p, size_t n)
  // for each i in [0,n) performs X::assign(s[i], p[i]). Copies
  // correctly even where p is in [s, s + n), and yields s.
  char array1[] = {'z', 'u', 'm', 'a', ' ', 'b', 'e', 'a', 'c', 'h',  0};
  const char str_lit1[] = "montara and ocean beach";
  int len = sizeof(str_lit1) + sizeof(array1) - 1; // two terminating chars
  char array2[len];

  VERIFY( str_lit1[0] == 'm' );
  c1 = array2[0];
  c2 = str_lit1[0];
  char c3 = array2[1];
  char c4 = str_lit1[1];
  std::char_traits<char>::move(array2, str_lit1, 0);
  VERIFY( array2[0] == c1 );
  VERIFY( str_lit1[0] == c2 );
  std::char_traits<char>::move(array2, str_lit1, 1);
  VERIFY( array2[0] == c2 );
  VERIFY( str_lit1[0] == c2 );
  VERIFY( array2[1] == c3 );
  VERIFY( str_lit1[1] == c4 );
  std::char_traits<char>::move(array2, str_lit1, 2);
  VERIFY( array2[0] == c2 );
  VERIFY( str_lit1[0] == c2 );
  VERIFY( array2[1] == c4 );
  VERIFY( str_lit1[1] == c4 );

  char* pc1 = array1 + 1;
  c1 = pc1[0];
  c2 = array1[0];
  VERIFY( c1 != c2 );
  char* pc2 = std::char_traits<char>::move(array1, pc1, 0);
  c3 = pc1[0];
  c4 = array1[0];
  VERIFY( c1 == c3 );
  VERIFY( c2 == c4 );
  VERIFY( pc2 == array1 );

  c1 = pc1[0];
  c2 = array1[0];
  char* pc3 = pc1;
  pc2 = std::char_traits<char>::move(array1, pc1, 10);
  c3 = pc1[0];
  c4 = array1[0];
  VERIFY( c1 != c3 ); // underlying char array changed.
  VERIFY( c4 != c3 );
  VERIFY( pc2 == array1 );
  VERIFY( pc3 == pc1 ); // but pointers o-tay
  c1 = *(str_01.data());
  c2 = array1[0];
  VERIFY( c1 != c2 );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
  return test;
}

#if defined(_GLIBCPP_USE_WCHAR_T)
int test02(void)
{
  bool test = true;
  const std::wstring str_01(L"zuma beach");
  const std::wstring str_02(L"montara and ocean beach");
 
  // 21.1.1 character traits requirements

  // Key for decoding what function signatures really mean:
  // X                == char_traits<_CharT>
  // [c,d]    == _CharT
  // [p,q]    == const _CharT*
  // s                == _CharT*
  // [n,i,j]  == size_t
  // f                == X::int_type
  // pos      == X::pos_type
  // state    == X::state_type

  // void X::assign(wchar_t c, wchar_t d)
  // assigns c = d;
  wchar_t c1 = L'z';
  wchar_t c2 = L'u';
  VERIFY( c1 != c2 );
  std::char_traits<wchar_t>::assign(c1,c2);
  VERIFY( c1 == L'u' );

  // char* X::move(char* s, const char* p, size_t n)
  // for each i in [0,n) performs X::assign(s[i], p[i]). Copies
  // correctly even where p is in [s, s + n), and yields s.   
  wchar_t array1[] = {L'z', L'u', L'm', L'a', L' ', L'b', L'e', L'a', L'c', L'h',  0};
  const wchar_t str_lit1[] = L"montara and ocean beach";
  int len = sizeof(str_lit1) + sizeof(array1) - 1; // two terminating chars
  wchar_t array2[len];

  VERIFY( str_lit1[0] == 'm' );
  c1 = array2[0];
  c2 = str_lit1[0];
  wchar_t c3 = array2[1];
  wchar_t c4 = str_lit1[1];
  std::char_traits<wchar_t>::move(array2, str_lit1, 0);
  VERIFY( array2[0] == c1 );
  VERIFY( str_lit1[0] == c2 );
  std::char_traits<wchar_t>::move(array2, str_lit1, 1);
  VERIFY( array2[0] == c2 );
  VERIFY( str_lit1[0] == c2 );
  VERIFY( array2[1] == c3 );
  VERIFY( str_lit1[1] == c4 );
  std::char_traits<wchar_t>::move(array2, str_lit1, 2);
  VERIFY( array2[0] == c2 );
  VERIFY( str_lit1[0] == c2 );
  VERIFY( array2[1] == c4 );
  VERIFY( str_lit1[1] == c4 );
 
  wchar_t* pc1 = array1 + 1;
  c1 = pc1[0];
  c2 = array1[0];
  VERIFY( c1 != c2 );
  wchar_t* pc2 = std::char_traits<wchar_t>::move(array1, pc1, 0);
  c3 = pc1[0];
  c4 = array1[0];
  VERIFY( c1 == c3 );
  VERIFY( c2 == c4 );
  VERIFY( pc2 == array1 );

  c1 = pc1[0];
  c2 = array1[0];
  wchar_t* pc3 = pc1;
  pc2 = std::char_traits<wchar_t>::move(array1, pc1, 10);
  c3 = pc1[0];
  c4 = array1[0];
  VERIFY( c1 != c3 ); // underlying wchar_t array changed.
  VERIFY( c4 != c3 );
  VERIFY( pc2 == array1 );
  VERIFY( pc3 == pc1 ); // but pointers o-tay
  c1 = *(str_01.data());
  c2 = array1[0];
  VERIFY( c1 != c2 );
 
#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}
#endif  //_GLIBCPP_USE_WCHAR_T

int main()
{ 
  test01();
#if defined(_GLIBCPP_USE_WCHAR_T)
  test02();
#endif
  return 0;
}
