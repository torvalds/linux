// { dg-options "-O0" }
// 2000-11-20
// Benjamin Kosnik bkoz@redhat.com

// Copyright (C) 2000, 2003 Free Software Foundation, Inc.
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

#include <complex>
#include <testsuite_hooks.h>

void test01()
{
 using namespace std;
 bool test = true;
 typedef complex<double> complex_type;
 const double cd1 = -11.451;
 const double cd2 = -442.1533;

 complex_type a(cd1, cd2);
 double d;
 d = a.real();
 VERIFY( d == cd1);

 d = a.imag();
 VERIFY(d == cd2);

 complex_type c(cd1, cd2);
 double d6 = abs(c);
 VERIFY( d6 >= 0);

 double d7 = arg(c);
 double d8 = atan2(c.imag(), c.real());
 VERIFY( d7 == d8);

 double d9 = norm(c);
 double d10 = d6 * d6;
 VERIFY(d9 - d10 == 0);

 complex_type e = conj(c);
 
 complex_type f = polar(c.imag(), 0.0);
 VERIFY(f.real() != 0);
}


int main()
{
  test01();
  return 0;
}
