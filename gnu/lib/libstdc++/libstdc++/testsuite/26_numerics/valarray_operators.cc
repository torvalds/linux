// { dg-do run }
// 2003-02-03  Volker Reichelt  <reichelt@igpm.rwth-aachen.de>

// Copyright (C) 2003 Free Software Foundation
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

#include <valarray>
#include <testsuite_hooks.h>

void test01() // check unary operators
{
  std::valarray<int> u(1);
  u[0]=1;

  VERIFY( (+u)[0] == +1 );
  VERIFY( (-u)[0] == -1 );
  VERIFY( (!u)[0] == !1 );
  VERIFY( (~u)[0] == ~1 );
}

void test02() // check binary operators
{
  std::valarray<int> u(1), v(1);
  u[0]=1;
  v[0]=3;

  VERIFY( (u+ v)[0] == (1+ 3) );
  VERIFY( (u- v)[0] == (1- 3) );
  VERIFY( (u* v)[0] == (1* 3) );
  VERIFY( (u/ v)[0] == (1/ 3) );
  VERIFY( (u% v)[0] == (1% 3) );
  VERIFY( (u^ v)[0] == (1^ 3) );
  VERIFY( (u& v)[0] == (1& 3) );
  VERIFY( (u| v)[0] == (1| 3) );
  VERIFY( (u<<v)[0] == (1<<3) );
  VERIFY( (u>>v)[0] == (1>>3) );
  VERIFY( (u&&v)[0] == (1&&3) );
  VERIFY( (u||v)[0] == (1||3) );
  VERIFY( (u==v)[0] == (1==3) );
  VERIFY( (u!=v)[0] == (1!=3) );
  VERIFY( (u< v)[0] == (1< 3) );
  VERIFY( (u> v)[0] == (1> 3) );
  VERIFY( (u<=v)[0] == (1<=3) );
  VERIFY( (u>=v)[0] == (1>=3) );
}

int main()
{
  test01();
  test02();
  return 0;
}
