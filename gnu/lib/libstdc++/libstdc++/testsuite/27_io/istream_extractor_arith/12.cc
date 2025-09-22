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

// XXX This test fails on sparc-solaris2 because of a bug in libc
// XXX sscanf for very long input.  See:
// XXX http://gcc.gnu.org/ml/gcc/2002-12/msg01422.html
// { dg-do run { xfail sparc*-*-solaris2* } }

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
 
// libstdc++/3720
// excess input should not cause a core dump
template<typename T>
bool test12_aux(bool integer_type)
{
  bool test = true;
  
  int digits_overflow;
  if (integer_type)
    // This many digits will overflow integer types in base 10.
    digits_overflow = std::numeric_limits<T>::digits10 + 2;
  else
    // This might do it, unsure.
    digits_overflow = std::numeric_limits<T>::max_exponent10 + 1;
  
  std::string st;
  std::string part = "1234567890123456789012345678901234567890";
  for (int i = 0; i < digits_overflow / part.size() + 1; ++i)
    st += part;
  std::stringbuf sb(st);
  std::istream is(&sb);
  T t;
  is >> t;
  VERIFY(is.fail());
  return test;
}

bool test12()
{
  bool test = true;
  VERIFY(test12_aux<short>(true));
  VERIFY(test12_aux<int>(true));
  VERIFY(test12_aux<long>(true));
  VERIFY(test12_aux<float>(false));
  VERIFY(test12_aux<double>(false));
  VERIFY(test12_aux<long double>(false));
  return test;
}

int main()
{
  test12();
  return 0;
}
