// 1999-07-28 bkoz

// Copyright (C) 1999, 2001 Free Software Foundation
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

// 27.6.1.2.3 basic_istream::operator>>
// @require@ %-*.tst %-*.txt
// @diff@ %-*.tst %-*.txt

#include <istream>
#include <sstream>
#include <fstream>
#include <testsuite_hooks.h>


// stringbufs.
bool test01() {

  typedef std::ios::traits_type ctraits_type;

  bool test = true;
  const std::string str_01;
  const std::string str_02("art taylor kickin it on DAKAR");
  std::string strtmp;

  std::stringbuf isbuf_00(std::ios_base::in);
  std::stringbuf isbuf_01(std::ios_base::in | std::ios_base::out);
  std::stringbuf isbuf_02(str_01, std::ios_base::in);
  std::stringbuf isbuf_03(str_01, std::ios_base::in | std::ios_base::out);
  std::stringbuf isbuf_04(str_02, std::ios_base::in);
  std::stringbuf isbuf_05(str_02, std::ios_base::in | std::ios_base::out);

  std::istream is_00(NULL);
  std::istream is_01(&isbuf_01);
  std::istream is_02(&isbuf_02);
  std::istream is_03(&isbuf_03);
  std::istream is_04(&isbuf_04);
  std::istream is_05(&isbuf_05);
  std::ios_base::iostate state1, state2, statefail, stateeof;
  statefail = std::ios_base::failbit;
  stateeof = std::ios_base::eofbit;


  // template<_CharT, _Traits>
  //  basic_istream& operator>>(basic_streambuf*)

  // null istream to empty in_buf
  state1 = is_00.rdstate();
  is_00 >> &isbuf_00;   
  state2 = is_00.rdstate();
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & statefail) );
  VERIFY( isbuf_00.str() == str_01 ); 

  // null istream to empty in_out_buf
  is_00.clear(std::ios_base::goodbit);
  state1 = is_00.rdstate();
  is_00 >> &isbuf_01;   
  state2 = is_00.rdstate();
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & statefail) );
  VERIFY( isbuf_01.str() == str_01 ); 

  // null istream to full in_buf
  is_00.clear(std::ios_base::goodbit);
  state1 = is_00.rdstate();
  is_00 >> &isbuf_04;   
  state2 = is_00.rdstate();
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & statefail) );
  VERIFY( isbuf_04.str() == str_02 ); 

  // null istream to full in_out_buf
  is_00.clear(std::ios_base::goodbit);
  state1 = is_00.rdstate();
  is_00 >> &isbuf_05;   
  state2 = is_00.rdstate();
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & statefail) );
  VERIFY( isbuf_05.str() == str_02 ); 

  // empty but non-null istream to full in_buf
  state1 = is_02.rdstate();
  is_02 >> &isbuf_04;   
  state2 = is_02.rdstate();
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & statefail) );
  VERIFY( isbuf_04.str() == str_02 ); // as only an "in" buffer
  VERIFY( isbuf_04.sgetc() == 'a' );

  // empty but non-null istream to full in_out_buf
  is_02.clear(std::ios_base::goodbit);
  state1 = is_02.rdstate();
  is_02 >> &isbuf_05;   
  state2 = is_02.rdstate();
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & statefail) );
  VERIFY( isbuf_05.str() == str_02 ); // as only an "in" buffer
  VERIFY( isbuf_05.sgetc() == 'a' );

  // full istream to empty in_buf (need out_buf, you know?)
  state1 = is_04.rdstate();
  is_04 >> &isbuf_02;   
  state2 = is_04.rdstate();
  VERIFY( state1 != state2 );
  VERIFY( static_cast<bool>(state2 & statefail) );
  VERIFY( isbuf_02.str() == str_01 ); // as only an "in" buffer
  VERIFY( isbuf_02.sgetc() == ctraits_type::eof() );
  VERIFY( is_04.peek() == ctraits_type::eof() ); // as failed

  // full istream to empty in_out_buf
  is_04.clear(std::ios_base::goodbit);
  state1 = is_04.rdstate();
  is_04 >> &isbuf_03;   
  state2 = is_04.rdstate();
  VERIFY( state1 == state2 );
  VERIFY( !static_cast<bool>(state2 & statefail) );
  VERIFY( state2 != stateeof );
  strtmp = isbuf_03.str();
  VERIFY( strtmp == str_02 ); // as only an "in" buffer
  VERIFY( isbuf_03.sgetc() == 'a' );
  VERIFY( is_04.peek() == ctraits_type::eof() );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
 
  return test;
}

// filebufs.
bool test02() {

  bool test = true;
  typedef std::ios::traits_type ctraits_type;
  const char name_01[] = "istream_extractor_other-1.txt"; //read 
  const char name_02[] = "istream_extractor_other-2.txt"; //write

  std::filebuf fbin, fbout;
  fbin.open(name_01, std::ios_base::in);
  fbout.open(name_02, std::ios_base::out | std::ios_base::trunc);
  VERIFY( fbin.is_open() );
  VERIFY( fbout.is_open() );

  if (test)
    {
      std::istream is(&fbin);
      is.unsetf(std::ios_base::skipws);
      is >> &fbout;
    }

  fbout.close();
  fbin.close();
  VERIFY( !fbin.is_open() );
  VERIFY( !fbout.is_open() );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
 
  return test;
}

void test03() 
{
  using namespace std;  
  bool test = true;

  // template<_CharT, _Traits>
  //  basic_istream& operator>>(ios_base& (*pf) (ios_base&))
  {
    int i = 0;
    std::istringstream iss(" 43");
    iss >> std::noskipws >> i;
    std::ios::iostate i3 = iss.rdstate();
    VERIFY ( !iss ); //should set failbit
  }

  // template<_CharT, _Traits>
  //  basic_istream& operator>>(basic_ios& (*pf) (basic_ios&))

  // template<_CharT, _Traits>
  //  basic_istream& operator>>(basic_istream& (*pf) (basic_istream&))
}


int main()
{
  test01();
  test02();
  test03();
  return 0;
}
