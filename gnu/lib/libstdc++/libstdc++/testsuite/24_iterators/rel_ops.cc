// 2002-04-13  Paolo Carlini  <pcarlini@unitus.it>

// Copyright (C) 2002 Free Software Foundation, Inc.
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

// 20.2.1 Operators

#include <utility>
#include <vector>

using namespace std::rel_ops;

// libstdc++/3628
void test01()
{
  std::vector<int> v;
  std::vector<int>::iterator vi;

  vi != v.begin();
  vi > v.begin();
  vi <= v.begin();
  vi >= v.begin();
}

int main()
{
  test01();
  return 0;
}

