// 1999-04-12 bkoz

// Copyright (C) 1999, 2000, 2002, 2003 Free Software Foundation, Inc.
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

// 27.6.1.2.2 arithmetic extractors

#include <cstdio> // for printf
#include <istream>
#include <ostream>
#include <sstream>
#include <locale>
#include <testsuite_hooks.h>

std::string str_01;
std::string str_02("true false 0 1 110001");
std::string str_03("-19999999 777777 -234234 233 -234 33 1 66300.25 .315 1.5");
std::string str_04("0123");

std::stringbuf isbuf_01(std::ios_base::in);
std::stringbuf isbuf_02(str_02, std::ios_base::in);
std::stringbuf isbuf_03(str_03, std::ios_base::in);
std::stringbuf isbuf_04(str_04, std::ios_base::in);

std::istream is_01(NULL);
std::istream is_02(&isbuf_02);
std::istream is_03(&isbuf_03);
std::istream is_04(&isbuf_04);
std::stringstream ss_01(str_01);
 
namespace std {
  class test_numpunct1 : public numpunct<char>
  {
  protected:
    string
    do_grouping() const 
    { return string(1, '\003'); }
  };
} // namespace std

void test07()
{
  // manufactured locale, grouping is turned on
  bool test = true;
  unsigned int h4 = 0, h3 = 0, h2 = 0;
  float f1 = 0.0;
  const std::string s1("205,199 23,445.25 1,024,365 123,22,24");
  std::istringstream is(s1);
  is.imbue(std::locale(std::locale(), new std::test_numpunct1));  

  // Basic operation.
  is >> h4; 
  VERIFY( h4 == 205199 );
  VERIFY( is.good() );

  is.clear();
  is >> f1; 
  VERIFY( f1 == 23445.25 );
  VERIFY( is.good() );

  is.clear();
  is >> h3; 
  VERIFY( h3 == 1024365 );
  VERIFY( is.good() );

  is.clear();
  is >> h2; 
  VERIFY( h2 == 0 );
  VERIFY( static_cast<bool>(is.rdstate() & std::ios_base::failbit) );
  VERIFY( static_cast<bool>(is.rdstate() & std::ios_base::eofbit) );

  // Stress tests for explicit errors in grouping corner cases.  The
  // validity of these tests and results have been hammered out in
  // private email between bkoz and ncm between Jan 25 and Jan 27, 2000.
  // Thanks nate -- benjamin
  const std::string s2(",111 4,,4 0.25,345 5..25 156,, 1,000000 1000000 1234,567");
  h3 = h4 = h2 = 0;
  f1 = 0.0;
  const char c_control = '?';
  char c = c_control;
  is.clear();
  is.str(s2);

  is >> h4; 
  VERIFY( h4 == 0 );
  VERIFY( static_cast<bool>(is.rdstate() & std::ios_base::failbit) );
  is.clear();
  is >> c;
  VERIFY( c == ',' );
  VERIFY( is.good() );

  is.ignore(3);
  is >> f1; 
  VERIFY( f1 == 0.0 );
  VERIFY( static_cast<bool>(is.rdstate() & std::ios_base::failbit) );
  is.clear();
  is >> c;
  VERIFY( c == ',' );
  is >> c;
  VERIFY( c == '4' );
  VERIFY( is.good() );

  is >> f1; 
  VERIFY( f1 == 0.25 );
  VERIFY( is.good() );
  is >> c;
  VERIFY( c == ',' );
  is >> h2;
  VERIFY( h2 == 345 );
  VERIFY( is.good() );
  f1 = 0.0;
  h2 = 0;

  is >> f1; 
  VERIFY( f1 == 5.0 );
  VERIFY( is.good() );
  is >> f1; 
  VERIFY( f1 == .25 );
  VERIFY( is.good() );

  is >> h3; 
  VERIFY( h3 == 0 );
  VERIFY( static_cast<bool>(is.rdstate() & std::ios_base::failbit) );
  is.clear();
  is >> c;
  VERIFY( c == ',' ); // second one
  VERIFY( is.good() );

  is >> h2; 
  VERIFY( h2 == 0 );
  VERIFY( static_cast<bool>(is.rdstate() & std::ios_base::failbit) );
  is.clear();

  is >> h2; 
  VERIFY( h2 == 1000000 );
  VERIFY( is.good() );
  h2 = 0;

  is >> h2; 
  VERIFY( h2 == 0 );
  VERIFY( static_cast<bool>(is.rdstate() & std::ios_base::failbit) );
  VERIFY( static_cast<bool>(is.rdstate() & std::ios_base::eofbit) );
  is.clear();

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}

int main()
{
  test07();
  return 0;
}
