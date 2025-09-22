// 2001-06-03 pme

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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

// 23.3.5.2 bitset members

#include <bitset>
#include <stdexcept>
#include <sstream>
#include <testsuite_hooks.h>

void 
test01(void)
{
  bool test = true;
  const size_t n1 = 5;

  // the other 22 member functions should be in here too...
  try {
    std::bitset<n1> five_bits;
    bool unused = five_bits.test(n1);   // should throw
    VERIFY( false );
  }
  catch(std::out_of_range& fail) {
    VERIFY( true );
  }
  catch(...) {
    VERIFY( false );
  }
  VERIFY( test );
}

// libstdc++/6124
void test02()
{
  std::bitset<1> bs;
  bs.count();
}

void test03()
{
  std::bitset<5>      b;
  std::stringstream   ss ("101");

  ss.exceptions(std::ios_base::eofbit);

  try
  {
    ss >> b;
  }
  catch (std::exception&) {}

  VERIFY( b.to_ulong() == 5 );
}

int main()
{
  test01();
  test02();
  test03();
  return 0;
}
