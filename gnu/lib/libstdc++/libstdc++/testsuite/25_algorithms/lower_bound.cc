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

// http://gcc.gnu.org/ml/libstdc++/2000-06/msg00316.html
struct foo { };

bool operator== (const foo&, const foo&) { return true; };
bool operator< (const foo&, const foo&) { return true; };

void bar(foo* a, foo* b, foo& x)
{
  foo* c = std::lower_bound(a, b, x);
}

void test01()
{
  bool test = true;

#ifdef DEBUG_ASSERT
  assert(test);
#endif
}

int main()
{
  return 0;
}
