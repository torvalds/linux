// 1999-05-21 bkoz
// 2000-05-21 Benjamin Kosnik  <bkoz@redhat.com>
// 2001-01-17 Loren J. Rittle  <ljrittle@acm.org>

// Copyright (C) 1999, 2000, 2001 Free Software Foundation
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

// 27.4.2.4 ios_base static members
// @require@ %-*.tst
// @diff@ %-*.tst %-*.txt

#include <cstdio>
#include <sstream>
#include <iostream>

#include <testsuite_hooks.h>

// N.B. Once we have called sync_with_stdio(false), we can never go back.

void
test01()
{
  std::ios_base::sync_with_stdio();
  std::freopen("ios_base_members_static-1.txt", "w", stdout);
 
  for (int i = 0; i < 2; i++)
    {
      std::printf("1");
      std::cout << "2";
      std::putc('3', stdout); 
      std::cout << '4';
      std::fputs("5", stdout);
      std::cout << 6;
      std::putchar('7');
      std::cout << 8 << '9';
      std::printf("0\n");
    }
}

bool test02() 
{
  bool test = true;
  
  std::stringbuf 	strbuf01;
  std::ios		ios01(&strbuf01);

  // 1: basic invocation
  VERIFY( ios01.sync_with_stdio() );
  VERIFY( ios01.sync_with_stdio(false) ); //returns previous state

  // 2: need to test interleaving of C and C++ io on a file object.
  VERIFY( std::cout.good() );
  VERIFY( !std::cout.sync_with_stdio(0) );
  VERIFY( std::cout.good() );
  VERIFY( !std::cout.sync_with_stdio(0) );
  VERIFY( std::cout.good() );

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
