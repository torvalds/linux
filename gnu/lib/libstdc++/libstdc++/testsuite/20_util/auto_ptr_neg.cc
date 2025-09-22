// Copyright (C) 2002 Free Software Foundation
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

// 20.4.5 Template class auto_ptr negative tests [lib.auto.ptr]

#include <memory>
#include <testsuite_hooks.h>

// { dg-do compile }
// { dg-excess-errors "" }

// via Jack Reeves <jack_reeves@hispeed.ch>
// libstdc++/3946
// http://gcc.gnu.org/ml/libstdc++/2002-07/msg00024.html
struct Base { };
struct Derived : public Base { };

std::auto_ptr<Derived> 
foo() { return std::auto_ptr<Derived>(new Derived); }

int
test01()
{
  std::auto_ptr<Base> ptr2;
  ptr2 = new Base; // { dg-error "no" "candidates" "auto_ptr"} 
  return 0;
}

int 
main()
{
  test01();

  return 0;
}
