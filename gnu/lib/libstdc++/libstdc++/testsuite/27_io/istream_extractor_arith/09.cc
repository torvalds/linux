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
 
bool test09()
{
   bool test = true;

   std::string st("2.456e3-+0.567e-2");
   std::stringbuf sb(st);
   std::istream is(&sb);
   double f1 = 0, f2 = 0;
   char c;
   (is>>std::ws) >> f1;
   (is>>std::ws) >> c;
   (is>>std::ws) >> f2;
   test = f1 == 2456;
   VERIFY( f2 == 0.00567 );
   VERIFY( c == '-' );
#ifdef DEBUG_ASSERT
  assert(test);
#endif
 
  return test;
}

int main()
{
  test09();
  return 0;
}
