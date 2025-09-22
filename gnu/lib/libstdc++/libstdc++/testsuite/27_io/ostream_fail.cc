// 2003-02-24 Petur Runolfsson <peturr02 at ru dot is>

// Copyright (C) 2003 Free Software Foundation, Inc.
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

#include <ostream>
#include <streambuf>
#include <testsuite_hooks.h>

// libstdc++/9827
class Buf : public std::streambuf
{
};

bool test01()
{
  using namespace std;
  bool test = true;

  Buf buf;
  ostream stream (&buf);

  stream << 1;
  VERIFY(!stream.good());

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
