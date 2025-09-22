// 1999-07-26 bkoz

// Copyright (C) 1999 Free Software Foundation
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

// 27.6.1.2.3 character extractors

#include <istream>
#include <sstream>
#include <testsuite_hooks.h>

bool test01() {

  bool test = true;
  std::string str_01;
  const std::string str_02("coltrane playing 'softly as a morning sunrise'");
  const std::string str_03("coltrane");

  std::stringbuf isbuf_01(std::ios_base::in);
  std::stringbuf isbuf_02(str_02, std::ios_base::in);
  std::istream is_01(NULL);
  std::istream is_02(&isbuf_02);

  std::ios_base::iostate state1, state2, statefail;
  statefail = std::ios_base::failbit;

  // template<_CharT, _Traits>
  //  basic_istream& operator>>(istream&, _CharT*)
  int n = 20;
  char array1[n];
  typedef std::ios::traits_type ctraits_type;
  ctraits_type::int_type i1, i2;

  state1 = is_01.rdstate();
  i1 = ctraits_type::length(array1);
  is_01 >> array1;   // should snake 0 characters, not alter stream state
  i2 = ctraits_type::length(array1);
  state2 = is_01.rdstate();
  VERIFY( i1 == i2 );
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & statefail) );

  state1 = is_02.rdstate();
  is_02 >> array1;   // should snake "coltrane"
  state2 = is_02.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( !static_cast<bool>(state2 & statefail) );
  VERIFY( array1[str_03.size() - 1] == 'e' );
  array1[str_03.size()] = '\0';
  VERIFY( !str_03.compare(0, str_03.size(), array1) );
  std::istream::int_type int1 = is_02.peek(); // should be ' '
  VERIFY( int1 == ' ' );

  state1 = is_02.rdstate();
  is_02 >> array1;   // should snake "playing" as sentry "eats" ws
  state2 = is_02.rdstate();
  int1 = is_02.peek(); // should be ' '
  VERIFY( int1 == ' ' );
  VERIFY( state1 == state2 );
  VERIFY( !static_cast<bool>(state2 & statefail) );


  // template<_CharT, _Traits>
  //  basic_istream& operator>>(istream&, unsigned char*)
  unsigned char array2[n];
  state1 = is_02.rdstate();
  is_02 >> array2;   // should snake 'softly
  state2 = is_02.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( !static_cast<bool>(state2 & statefail) );
  VERIFY( array2[0] == '\'' );
  VERIFY( array2[1] == 's' );
  VERIFY( array2[6] == 'y' );
  int1 = is_02.peek(); // should be ' '
  VERIFY( int1 == ' ' );


   // template<_CharT, _Traits>
  //  basic_istream& operator>>(istream&, signed char*)
  signed char array3[n];
  state1 = is_02.rdstate();
  is_02 >> array3;   // should snake "as"
  state2 = is_02.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( !static_cast<bool>(state2 & statefail) );
  VERIFY( array3[0] == 'a' );
  VERIFY( array3[1] == 's' );
  int1 = is_02.peek(); // should be ' '
  VERIFY( int1 == ' ' );
 

  // testing with width() control enabled.
  is_02.width(8);
  state1 = is_02.rdstate();
  is_02 >> array1;   // should snake a
  state2 = is_02.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( !ctraits_type::compare(array1, "a", 2) );

  is_02.width(1);
  state1 = is_02.rdstate();
  is_02 >> array1;   // should snake nothing, set failbit
  state2 = is_02.rdstate();
  VERIFY( state1 != state2 );
  VERIFY( state2 == statefail );
  VERIFY( array1[0] == '\0' );

  is_02.width(8);
  is_02.clear();
  state1 = is_02.rdstate();
  VERIFY( !state1 );
  is_02 >> array1;   // should snake "morning"
  state2 = is_02.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( !ctraits_type::compare(array1, "morning", 8) );

  // testing for correct exception setting
  const std::string str_04("   impulse!!");
  std::stringbuf isbuf_03(str_04, std::ios_base::in);
  std::stringbuf isbuf_04(str_04, std::ios_base::in);
  std::istream is_03(&isbuf_03);
  std::istream is_04(&isbuf_04);

  is_03 >> array1;
  VERIFY( !ctraits_type::compare(array1,"impulse!!", 10) );
  VERIFY( is_03.rdstate() == std::ios_base::eofbit );

  is_04.width(9);
  is_04 >> array1;
  VERIFY( ! std::ios::traits_type::compare(array1,"impulse!", 9) );
  VERIFY( !is_04.rdstate() ); 

#ifdef DEBUG_ASSERT
  assert(test);
#endif
 
  return test;
}

bool test02() {

  typedef std::ios::traits_type ctraits_type;

  bool test = true;
  std::string str_01;
  const std::string str_02("or coltrane playing tunji with jimmy garrison");
  const std::string str_03("coltrane");

  std::stringbuf isbuf_01(std::ios_base::in);
  std::stringbuf isbuf_02(str_02, std::ios_base::in);
  std::istream is_01(NULL);
  std::istream is_02(&isbuf_02);
  std::ios_base::iostate state1, state2, statefail;
  statefail = std::ios_base::failbit;

  // template<_CharT, _Traits>
  //  basic_istream& operator>>(istream&, _CharT&)
  char c1 = 'c', c2 = 'c';
  state1 = is_01.rdstate();
  is_01 >> c1;   
  state2 = is_01.rdstate();
  VERIFY( state1 != state2 );
  VERIFY( c1 == c2 );
  VERIFY( static_cast<bool>(state2 & statefail) );

  state1 = is_02.rdstate();
  is_02 >> c1;   
  state2 = is_02.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( c1 == 'o' );
  is_02 >> c1;   
  is_02 >> c1;   
  VERIFY( c1 == 'c' );
  VERIFY( !static_cast<bool>(state2 & statefail) );

  // template<_CharT, _Traits>
  //  basic_istream& operator>>(istream&, unsigned char&)
  unsigned char uc1 = 'c';
  state1 = is_02.rdstate();
  is_02 >> uc1;   
  state2 = is_02.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( uc1 == 'o' );
  is_02 >> uc1;   
  is_02 >> uc1;   
  VERIFY( uc1 == 't' );

  // template<_CharT, _Traits>
  //  basic_istream& operator>>(istream&, signed char&)
  signed char sc1 = 'c';
  state1 = is_02.rdstate();
  is_02 >> sc1;   
  state2 = is_02.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( sc1 == 'r' );
  is_02 >> sc1;   
  is_02 >> sc1;   
  VERIFY( sc1 == 'n' );

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
