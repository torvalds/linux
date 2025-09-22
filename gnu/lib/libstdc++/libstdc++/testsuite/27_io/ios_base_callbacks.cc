// 1999-11-10 bkoz

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

// 27.4.2.6 ios_base callbacks

#include <string>
#include <sstream>
#include <testsuite_hooks.h>

const std::string str01("the nubians of plutonia");
std::string str02;

void 
callb01(std::ios_base::event e,  std::ios_base& b, int i)
{ str02 += "the nubians"; }

void 
callb02(std::ios_base::event e,  std::ios_base& b, int i)
{ str02 += " of "; }

void 
callb03(std::ios_base::event e,  std::ios_base& b, int i)
{ str02 += "plutonia"; }

bool test01() 
{
  bool test = true;
  std::locale loc("C");
  std::stringbuf 	strbuf01;
  std::ios		ios01(&strbuf01);

  ios01.register_callback(callb03, 1);
  ios01.register_callback(callb02, 1);
  ios01.register_callback(callb01, 1);
  ios01.imbue(loc);
  VERIFY( str01 == str02 );

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}


int main(void)
{
  test01();

  return 0;
}

