// 1999-05-20 bkoz

// Copyright (C) 1999 Free Software Foundation, Inc.
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

// 17.4.1.2 Headers, ciso646

// { dg-do link }

#include <ciso646>
#include <testsuite_hooks.h>


// 2.11 Keywords
// alternative representations
// and, and_eq, bitand, bitor, compl, not, not_eq, or, or_eq, xor, xor_eq

// C 2.2.2 Header <iso646.h> 
// The tokens (as above) are keywords and do not appear as macros in <ciso646>.

// Test for macros.
bool test01() 
{
  bool test = true;

#if 0

#ifdef and
  test = false;
#endif

#ifdef and_eq
  test = false;
#endif

#ifdef bitand
  test = false;
#endif

#ifdef bitor
  test = false;
#endif

#ifdef compl
  test = false;
#endif

#ifdef not_eq
  test = false;
#endif

#ifdef not_or
  test = false;
#endif

#ifdef or
  test = false;
#endif

#ifdef or_eq
  test = false;
#endif

#ifdef xor
  test = false;
#endif

#ifdef xor_eq
  test = false;
#endif

#endif

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}


// Equivalance in usage.
bool test02()
{
  bool test = true;

  bool arg1 = true;
  bool arg2 = false;
  int  int1 = 45;
  int  int2 = 0;
  
  VERIFY( arg1 && int1 );
  VERIFY( arg1 and int1 );

  VERIFY( (arg1 && arg2) == (arg1 and arg2) );
  VERIFY( (arg1 && int1) == (arg1 and int1) );

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}


int main(void)
{
  test01();
  test02();

  return 0;
}
