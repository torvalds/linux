// Copyright (C) 2001 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without Pred the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// 23.2.2.2 list capacity [lib.list.capacity]

#include <list>
#include <testsuite_hooks.h>

bool test = true;

// This test verifies the following.
//
// 23.2.2       bool empty() const
// 23.2.2       size_type size() const
// 23.2.2       iterator begin()
// 23.2.2       iterator end()
// 23.2.2.3     void push_back(const T&)
// 23.2.2       size_type max_size() const
// 23.2.2.2     void resize(size_type s, T c = T())
//
void
test01()
{
  std::list<int> list0101;
  VERIFY(list0101.empty());
  VERIFY(list0101.size() == 0);

  list0101.push_back(1);
  VERIFY(!list0101.empty());
  VERIFY(list0101.size() == 1);

  list0101.resize(3, 2);
  VERIFY(!list0101.empty());
  VERIFY(list0101.size() == 3);

  std::list<int>::iterator i = list0101.begin();
  VERIFY(*i == 1); ++i;
  VERIFY(*i == 2); ++i;
  VERIFY(*i == 2); ++i;
  VERIFY(i == list0101.end());

  list0101.resize(0);
  VERIFY(list0101.empty());
  VERIFY(list0101.size() == 0);
}

int
main(int argc, char* argv[])
{
    test01();

    return !test;
}

// vi:set sw=2 ts=2:
