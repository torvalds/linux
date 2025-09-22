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
 
// minimal sanity check
bool test01() {

  bool test = true;

  // Integral Types:
  bool 			b1  = false;
  bool 			b2  = false;
  short 		s1  = 0;
  int	 		i1  = 0;
  long	 		l1  = 0;
  unsigned short 	us1 = 0;
  unsigned int 		ui1 = 0;
  unsigned long 	ul1 = 0;

  // Floating-point Types:
  float 		f1  = 0;
  double 		d1  = 0;
  long double 		ld1 = 0;

  // process alphanumeric versions of bool values
  std::ios_base::fmtflags fmt = is_02.flags();
  bool testfmt = fmt & std::ios_base::boolalpha;
  is_02.setf(std::ios_base::boolalpha);
  fmt = is_02.flags();
  testfmt = fmt & std::ios_base::boolalpha;
  is_02 >> b1;
  VERIFY( b1 == 1 );
  is_02 >> b1;
  VERIFY( b1 == 0 );

  // process numeric versions of of bool values
  is_02.unsetf(std::ios_base::boolalpha);
  fmt = is_02.flags();
  testfmt = fmt & std::ios_base::boolalpha;
  is_02 >> b1;
  VERIFY( b1 == 0 );
  is_02 >> b1;
  VERIFY( b1 == 1 );

  // is_03 == "-19999999 777777 -234234 233 -234 33 1 66300.25 .315 1.5"
  is_03 >> l1;
  VERIFY( l1 == -19999999 );
  is_03 >> ul1;
  VERIFY( ul1 == 777777 );
  is_03 >> i1;
  VERIFY( i1 == -234234 );
  is_03 >> ui1;
  VERIFY( ui1 == 233 );
  is_03 >> s1;
  VERIFY( s1 == -234 );
  is_03 >> us1;
  VERIFY( us1 == 33 );
  is_03 >> b1;
  VERIFY( b1 == 1 );
  is_03 >> ld1;
  VERIFY( ld1 == 66300.25 );
  is_03 >> d1;
  VERIFY( d1 == .315 );
  is_03 >> f1;
  VERIFY( f1 == 1.5 );

  is_04 >> std::hex >> i1;
  std::printf ("%d %d %d\n", i1, i1 == 0x123, test);
  VERIFY( i1 == 0x123 );
  std::printf ("%d %d %d\n", i1, i1 == 0x123, test);

  // test void pointers
  int i = 55;
  void* po = &i;
  void* pi;

  ss_01 << po;
  ss_01 >> pi;
  std::printf ("%x %x\n", pi, po);
  VERIFY( po == pi );
  
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
