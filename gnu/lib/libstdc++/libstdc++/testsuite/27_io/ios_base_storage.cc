// 2000-12-19 bkoz

// Copyright (C) 2000, 2002, 2003 Free Software Foundation
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

// 27.4.2.5 ios_base storage functions

// XXX This test will not work for some versions of irix6 because of
// XXX bug(s) in libc malloc for very large allocations.  However
// XXX -lmalloc seems to work.
// See http://gcc.gnu.org/ml/gcc/2002-05/msg01012.html
// { dg-options "-lmalloc" { target mips*-*-irix6* } }

#include <sstream>
#include <iostream>

#include <testsuite_hooks.h>

// http://gcc.gnu.org/ml/gcc-bugs/2000-12/msg00413.html
void test01() 
{
  bool test = true;
  
  using namespace std;

  long x1 = ios::xalloc();
  long x2 = ios::xalloc();
  long x3 = ios::xalloc();
  long x4 = ios::xalloc();

  ostringstream out("the element of crime, lars von trier");
  out.pword(++x4); // should not crash
}

// libstdc++/3129
void test02()
{
  bool test = true;
  int max = std::numeric_limits<int>::max() - 1;
  std::stringbuf        strbuf;
  std::ios              ios(&strbuf);

  ios.exceptions(std::ios::badbit);

  long l = 0;
  void* v = 0;

  // pword
  ios.pword(1) = v;
  VERIFY( ios.pword(1) == v );
  
  try 
    {
      v = ios.pword(max);
    }
  catch(std::ios_base::failure& obj)
    {
      // Ok.
      VERIFY( ios.bad() );
    }
  catch(...)
    {
      VERIFY( test = false );
    }
  VERIFY( v == 0 );

  VERIFY( ios.pword(1) == v );
  
  // max is different code path from max-1
  v = &test;
  try 
    {
      v = ios.pword(std::numeric_limits<int>::max());
    }
  catch(std::ios_base::failure& obj)
    {
      // Ok.
      VERIFY( ios.bad() );
    }
  catch(...)
    {
      VERIFY( test = false );
    }
  VERIFY( v == &test );

  // iword
  ios.iword(1) = 1;
  VERIFY( ios.iword(1) == 1 );
  
  try 
    {
      l = ios.iword(max);
    }
  catch(std::ios_base::failure& obj)
    {
      // Ok.
      VERIFY( ios.bad() );
    }
  catch(...)
    {
      VERIFY( test = false );
    }
  VERIFY( l == 0 );

  VERIFY( ios.iword(1) == 1 );

  // max is different code path from max-1
  l = 1;
  try 
    {
      l = ios.iword(std::numeric_limits<int>::max());
    }
  catch(std::ios_base::failure& obj)
    {
      // Ok.
      VERIFY( ios.bad() );
    }
  catch(...)
    {
      VERIFY( test = false );
    }
  VERIFY( l == 1 );

}

class derived : public std::ios_base
{
public:
  derived() {}
};

void test03()
{
  derived d;

  d.pword(0) = &d;
  d.iword(0) = 1;
}

int main(void)
{
  __gnu_cxx_test::set_memory_limits();
  test01();
  test02();
  test03();
  return 0;
}
