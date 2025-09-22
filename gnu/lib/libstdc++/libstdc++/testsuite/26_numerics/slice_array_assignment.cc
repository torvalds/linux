// 20010613 gdr

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

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.


// This is DR-253.  Test for accessible assignment-operators.
#include <valarray>
#include <testsuite_hooks.h>

int main()
{
  using std::valarray;
  using std::slice;
  valarray<int> v(1, 10), w(2, 10);

  w[slice(0, 3, 3)] = v[slice(2, 3, 3)];

  VERIFY(v[0] == 1 && w[0] == 1);
  VERIFY(v[3] == 1 && w[3] == 1);
  VERIFY(v[6] == 1 && w[6] == 1);

  std::slice_array<int> t = v[slice(0, 10, 1)];
  
  return 0;
}
