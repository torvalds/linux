// 2000-09-06
// bkoz 

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

// 23.2.4 vector 

#include <vector>
#include <stdexcept>
#include <testsuite_hooks.h>

template<typename T>
struct A { };

struct B { };

// http://gcc.gnu.org/ml/libstdc++/2000-09/msg00002.html
bool test01()
{
  bool test = true;
  std::vector< A<B> > vec01;
  std::vector< A<B> > vec02(5);
  typedef std::vector< A<B> >::size_type size_type;
  typedef std::vector< A<B> >::reference reference;

  try
    { 
      reference r01 = vec01.at(6); 
      VERIFY( false ); // Should not get here, as exception thrown.
    }
  catch(std::out_of_range& err)
    {
      VERIFY( true );
    }
  catch(...)
    {
      VERIFY( false );
    }

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
