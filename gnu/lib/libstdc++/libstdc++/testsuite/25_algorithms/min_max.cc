// 2000-03-29 sss/bkoz

// Copyright (C) 2000 Free Software Foundation, Inc.
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

#include <algorithm>
#include <testsuite_hooks.h>

void test01()
{
  bool test = true;
  const int& x = std::max(1, 2);
  const int& y = std::max(3, 4);
  VERIFY( x == 2 );
  VERIFY( y == 4 );

  const int& z = std::min(1, 2);
  const int& w = std::min(3, 4);
  VERIFY( z == 1 );
  VERIFY( w == 3 );

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}

int main()
{
  test01();
  return 0;
}
