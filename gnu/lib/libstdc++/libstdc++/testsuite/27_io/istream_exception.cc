// 2003-03-08  Jerry Quinn  <jlquinn@optonline.net>

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

#include <istream>
#include <streambuf>
#include <testsuite_hooks.h>

// libstdc++/9561
struct foobar: std::exception { };

struct buf: std::streambuf
{
    virtual int_type underflow () {
        throw foobar ();
        return -1;
    }
    virtual int_type uflow () {
        throw foobar ();
        return -1;
    }
};

void test01()
{
  using namespace std;

  buf b;
  std::istream strm (&b);
  strm.exceptions (std::ios::badbit);
  int i = 0;

  try {
    i = strm.get();
  }
  catch (foobar) {
    // strm should throw foobar and not do anything else
    VERIFY(strm.bad());
  }
  catch (...) {
    VERIFY(false);
  }

  VERIFY(i == 0);
}


int main()
{
  test01();
  return 0;
}
