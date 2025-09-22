// 2002-05-10 ghazi

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

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

// { dg-do run }

#include <locale>
#include <testsuite_hooks.h>

typedef char char_type;
typedef std::char_traits<char_type> traits_type;
class gnu_ctype: public std::ctype<char_type> { };

// These two macros take a ctype mask, a string and a pointer within
// the string that the scan function should return, usually either the
// first or last character if the string contains identical values as
// below.
#define VERIFY_SCAN_IS(MASK, STRING, EXPECTED) \
  VERIFY(gctype.scan_is((MASK), (STRING), \
			(STRING) + traits_type::length(STRING)) == (EXPECTED))
#define VERIFY_SCAN_NOT(MASK, STRING, EXPECTED) \
  VERIFY(gctype.scan_not((MASK), (STRING), \
			 (STRING) + traits_type::length(STRING)) == (EXPECTED))

// Sanity check scan_is() and scan_not().
void test01()
{
  bool test = true;

  const char_type *const ca = "aaaaa";
  const char_type *const cz = "zzzzz";
  const char_type *const cA = "AAAAA";
  const char_type *const cZ = "ZZZZZ";
  const char_type *const c0 = "00000";
  const char_type *const c9 = "99999";
  const char_type *const cs = "     ";
  const char_type *const xf = "fffff";
  const char_type *const xF = "FFFFF";
  const char_type *const p1 = "!!!!!";
  const char_type *const p2 = "/////";
  
  gnu_ctype gctype;

  // 'a'
  VERIFY_SCAN_IS (std::ctype_base::alnum, ca, ca);
  VERIFY_SCAN_IS (std::ctype_base::alpha, ca, ca);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, ca, ca+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, ca, ca+5);
  VERIFY_SCAN_IS (std::ctype_base::graph, ca, ca);
  VERIFY_SCAN_IS (std::ctype_base::lower, ca, ca);
  VERIFY_SCAN_IS (std::ctype_base::print, ca, ca);
  VERIFY_SCAN_IS (std::ctype_base::punct, ca, ca+5);
  VERIFY_SCAN_IS (std::ctype_base::space, ca, ca+5);
  VERIFY_SCAN_IS (std::ctype_base::upper, ca, ca+5);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, ca, ca);

  VERIFY_SCAN_NOT (std::ctype_base::alnum, ca, ca+5);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, ca, ca+5);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, ca, ca);
  VERIFY_SCAN_NOT (std::ctype_base::digit, ca, ca);
  VERIFY_SCAN_NOT (std::ctype_base::graph, ca, ca+5);
  VERIFY_SCAN_NOT (std::ctype_base::lower, ca, ca+5);
  VERIFY_SCAN_NOT (std::ctype_base::print, ca, ca+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, ca, ca);
  VERIFY_SCAN_NOT (std::ctype_base::space, ca, ca);
  VERIFY_SCAN_NOT (std::ctype_base::upper, ca, ca);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, ca, ca+5);

  // 'z'
  VERIFY_SCAN_IS (std::ctype_base::alnum, cz, cz);
  VERIFY_SCAN_IS (std::ctype_base::alpha, cz, cz);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, cz, cz+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, cz, cz+5);
  VERIFY_SCAN_IS (std::ctype_base::graph, cz, cz);
  VERIFY_SCAN_IS (std::ctype_base::lower, cz, cz);
  VERIFY_SCAN_IS (std::ctype_base::print, cz, cz);
  VERIFY_SCAN_IS (std::ctype_base::punct, cz, cz+5);
  VERIFY_SCAN_IS (std::ctype_base::space, cz, cz+5);
  VERIFY_SCAN_IS (std::ctype_base::upper, cz, cz+5);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, cz, cz+5);
  
  VERIFY_SCAN_NOT (std::ctype_base::alnum, cz, cz+5);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, cz, cz+5);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, cz, cz);
  VERIFY_SCAN_NOT (std::ctype_base::digit, cz, cz);
  VERIFY_SCAN_NOT (std::ctype_base::graph, cz, cz+5);
  VERIFY_SCAN_NOT (std::ctype_base::lower, cz, cz+5);
  VERIFY_SCAN_NOT (std::ctype_base::print, cz, cz+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, cz, cz);
  VERIFY_SCAN_NOT (std::ctype_base::space, cz, cz);
  VERIFY_SCAN_NOT (std::ctype_base::upper, cz, cz);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, cz, cz);
  
  // 'A'
  VERIFY_SCAN_IS (std::ctype_base::alnum, cA, cA);
  VERIFY_SCAN_IS (std::ctype_base::alpha, cA, cA);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, cA, cA+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, cA, cA+5);
  VERIFY_SCAN_IS (std::ctype_base::graph, cA, cA);
  VERIFY_SCAN_IS (std::ctype_base::lower, cA, cA+5);
  VERIFY_SCAN_IS (std::ctype_base::print, cA, cA);
  VERIFY_SCAN_IS (std::ctype_base::punct, cA, cA+5);
  VERIFY_SCAN_IS (std::ctype_base::space, cA, cA+5);
  VERIFY_SCAN_IS (std::ctype_base::upper, cA, cA);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, cA, cA);
  
  VERIFY_SCAN_NOT (std::ctype_base::alnum, cA, cA+5);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, cA, cA+5);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, cA, cA);
  VERIFY_SCAN_NOT (std::ctype_base::digit, cA, cA);
  VERIFY_SCAN_NOT (std::ctype_base::graph, cA, cA+5);
  VERIFY_SCAN_NOT (std::ctype_base::lower, cA, cA);
  VERIFY_SCAN_NOT (std::ctype_base::print, cA, cA+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, cA, cA);
  VERIFY_SCAN_NOT (std::ctype_base::space, cA, cA);
  VERIFY_SCAN_NOT (std::ctype_base::upper, cA, cA+5);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, cA, cA+5);
  
  // 'Z'
  VERIFY_SCAN_IS (std::ctype_base::alnum, cZ, cZ);
  VERIFY_SCAN_IS (std::ctype_base::alpha, cZ, cZ);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, cZ, cZ+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, cZ, cZ+5);
  VERIFY_SCAN_IS (std::ctype_base::graph, cZ, cZ);
  VERIFY_SCAN_IS (std::ctype_base::lower, cZ, cZ+5);
  VERIFY_SCAN_IS (std::ctype_base::print, cZ, cZ);
  VERIFY_SCAN_IS (std::ctype_base::punct, cZ, cZ+5);
  VERIFY_SCAN_IS (std::ctype_base::space, cZ, cZ+5);
  VERIFY_SCAN_IS (std::ctype_base::upper, cZ, cZ);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, cZ, cZ+5);
  
  VERIFY_SCAN_NOT (std::ctype_base::alnum, cZ, cZ+5);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, cZ, cZ+5);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, cZ, cZ);
  VERIFY_SCAN_NOT (std::ctype_base::digit, cZ, cZ);
  VERIFY_SCAN_NOT (std::ctype_base::graph, cZ, cZ+5);
  VERIFY_SCAN_NOT (std::ctype_base::lower, cZ, cZ);
  VERIFY_SCAN_NOT (std::ctype_base::print, cZ, cZ+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, cZ, cZ);
  VERIFY_SCAN_NOT (std::ctype_base::space, cZ, cZ);
  VERIFY_SCAN_NOT (std::ctype_base::upper, cZ, cZ+5);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, cZ, cZ);
  
  // '0'
  VERIFY_SCAN_IS (std::ctype_base::alnum, c0, c0);
  VERIFY_SCAN_IS (std::ctype_base::alpha, c0, c0+5);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, c0, c0+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, c0, c0);
  VERIFY_SCAN_IS (std::ctype_base::graph, c0, c0);
  VERIFY_SCAN_IS (std::ctype_base::lower, c0, c0+5);
  VERIFY_SCAN_IS (std::ctype_base::print, c0, c0);
  VERIFY_SCAN_IS (std::ctype_base::punct, c0, c0+5);
  VERIFY_SCAN_IS (std::ctype_base::space, c0, c0+5);
  VERIFY_SCAN_IS (std::ctype_base::upper, c0, c0+5);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, c0, c0);
  
  VERIFY_SCAN_NOT (std::ctype_base::alnum, c0, c0+5);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, c0, c0);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, c0, c0);
  VERIFY_SCAN_NOT (std::ctype_base::digit, c0, c0+5);
  VERIFY_SCAN_NOT (std::ctype_base::graph, c0, c0+5);
  VERIFY_SCAN_NOT (std::ctype_base::lower, c0, c0);
  VERIFY_SCAN_NOT (std::ctype_base::print, c0, c0+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, c0, c0);
  VERIFY_SCAN_NOT (std::ctype_base::space, c0, c0);
  VERIFY_SCAN_NOT (std::ctype_base::upper, c0, c0);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, c0, c0+5);
  
  // '9'
  VERIFY_SCAN_IS (std::ctype_base::alnum, c9, c9);
  VERIFY_SCAN_IS (std::ctype_base::alpha, c9, c9+5);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, c9, c9+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, c9, c9);
  VERIFY_SCAN_IS (std::ctype_base::graph, c9, c9);
  VERIFY_SCAN_IS (std::ctype_base::lower, c9, c9+5);
  VERIFY_SCAN_IS (std::ctype_base::print, c9, c9);
  VERIFY_SCAN_IS (std::ctype_base::punct, c9, c9+5);
  VERIFY_SCAN_IS (std::ctype_base::space, c9, c9+5);
  VERIFY_SCAN_IS (std::ctype_base::upper, c9, c9+5);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, c9, c9);
  
  VERIFY_SCAN_NOT (std::ctype_base::alnum, c9, c9+5);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, c9, c9);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, c9, c9);
  VERIFY_SCAN_NOT (std::ctype_base::digit, c9, c9+5);
  VERIFY_SCAN_NOT (std::ctype_base::graph, c9, c9+5);
  VERIFY_SCAN_NOT (std::ctype_base::lower, c9, c9);
  VERIFY_SCAN_NOT (std::ctype_base::print, c9, c9+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, c9, c9);
  VERIFY_SCAN_NOT (std::ctype_base::space, c9, c9);
  VERIFY_SCAN_NOT (std::ctype_base::upper, c9, c9);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, c9, c9+5);
  
  // ' '
  VERIFY_SCAN_IS (std::ctype_base::alnum, cs, cs+5);
  VERIFY_SCAN_IS (std::ctype_base::alpha, cs, cs+5);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, cs, cs+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, cs, cs+5);
  VERIFY_SCAN_IS (std::ctype_base::graph, cs, cs+5);
  VERIFY_SCAN_IS (std::ctype_base::lower, cs, cs+5);
  VERIFY_SCAN_IS (std::ctype_base::print, cs, cs);
  VERIFY_SCAN_IS (std::ctype_base::punct, cs, cs+5);
  VERIFY_SCAN_IS (std::ctype_base::space, cs, cs);
  VERIFY_SCAN_IS (std::ctype_base::upper, cs, cs+5);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, cs, cs+5);
  
  VERIFY_SCAN_NOT (std::ctype_base::alnum, cs, cs);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, cs, cs);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, cs, cs);
  VERIFY_SCAN_NOT (std::ctype_base::digit, cs, cs);
  VERIFY_SCAN_NOT (std::ctype_base::graph, cs, cs);
  VERIFY_SCAN_NOT (std::ctype_base::lower, cs, cs);
  VERIFY_SCAN_NOT (std::ctype_base::print, cs, cs+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, cs, cs);
  VERIFY_SCAN_NOT (std::ctype_base::space, cs, cs+5);
  VERIFY_SCAN_NOT (std::ctype_base::upper, cs, cs);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, cs, cs);
  
  // 'f'
  VERIFY_SCAN_IS (std::ctype_base::alnum, xf, xf);
  VERIFY_SCAN_IS (std::ctype_base::alpha, xf, xf);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, xf, xf+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, xf, xf+5);
  VERIFY_SCAN_IS (std::ctype_base::graph, xf, xf);
  VERIFY_SCAN_IS (std::ctype_base::lower, xf, xf);
  VERIFY_SCAN_IS (std::ctype_base::print, xf, xf);
  VERIFY_SCAN_IS (std::ctype_base::punct, xf, xf+5);
  VERIFY_SCAN_IS (std::ctype_base::space, xf, xf+5);
  VERIFY_SCAN_IS (std::ctype_base::upper, xf, xf+5);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, xf, xf);
  
  VERIFY_SCAN_NOT (std::ctype_base::alnum, xf, xf+5);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, xf, xf+5);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, xf, xf);
  VERIFY_SCAN_NOT (std::ctype_base::digit, xf, xf);
  VERIFY_SCAN_NOT (std::ctype_base::graph, xf, xf+5);
  VERIFY_SCAN_NOT (std::ctype_base::lower, xf, xf+5);
  VERIFY_SCAN_NOT (std::ctype_base::print, xf, xf+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, xf, xf);
  VERIFY_SCAN_NOT (std::ctype_base::space, xf, xf);
  VERIFY_SCAN_NOT (std::ctype_base::upper, xf, xf);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, xf, xf+5);
  
  // 'F'
  VERIFY_SCAN_IS (std::ctype_base::alnum, xF, xF);
  VERIFY_SCAN_IS (std::ctype_base::alpha, xF, xF);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, xF, xF+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, xF, xF+5);
  VERIFY_SCAN_IS (std::ctype_base::graph, xF, xF);
  VERIFY_SCAN_IS (std::ctype_base::lower, xF, xF+5);
  VERIFY_SCAN_IS (std::ctype_base::print, xF, xF);
  VERIFY_SCAN_IS (std::ctype_base::punct, xF, xF+5);
  VERIFY_SCAN_IS (std::ctype_base::space, xF, xF+5);
  VERIFY_SCAN_IS (std::ctype_base::upper, xF, xF);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, xF, xF);
  
  VERIFY_SCAN_NOT (std::ctype_base::alnum, xF, xF+5);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, xF, xF+5);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, xF, xF);
  VERIFY_SCAN_NOT (std::ctype_base::digit, xF, xF);
  VERIFY_SCAN_NOT (std::ctype_base::graph, xF, xF+5);
  VERIFY_SCAN_NOT (std::ctype_base::lower, xF, xF);
  VERIFY_SCAN_NOT (std::ctype_base::print, xF, xF+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, xF, xF);
  VERIFY_SCAN_NOT (std::ctype_base::space, xF, xF);
  VERIFY_SCAN_NOT (std::ctype_base::upper, xF, xF+5);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, xF, xF+5);
  
  // '!'
  VERIFY_SCAN_IS (std::ctype_base::alnum, p1, p1+5);
  VERIFY_SCAN_IS (std::ctype_base::alpha, p1, p1+5);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, p1, p1+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, p1, p1+5);
  VERIFY_SCAN_IS (std::ctype_base::graph, p1, p1);
  VERIFY_SCAN_IS (std::ctype_base::lower, p1, p1+5);
  VERIFY_SCAN_IS (std::ctype_base::print, p1, p1);
  VERIFY_SCAN_IS (std::ctype_base::punct, p1, p1);
  VERIFY_SCAN_IS (std::ctype_base::space, p1, p1+5);
  VERIFY_SCAN_IS (std::ctype_base::upper, p1, p1+5);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, p1, p1+5);
  
  VERIFY_SCAN_NOT (std::ctype_base::alnum, p1, p1);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, p1, p1);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, p1, p1);
  VERIFY_SCAN_NOT (std::ctype_base::digit, p1, p1);
  VERIFY_SCAN_NOT (std::ctype_base::lower, p1, p1);
  VERIFY_SCAN_NOT (std::ctype_base::print, p1, p1+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, p1, p1+5);
  VERIFY_SCAN_NOT (std::ctype_base::space, p1, p1);
  VERIFY_SCAN_NOT (std::ctype_base::upper, p1, p1);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, p1, p1);
  
  // '/'
  VERIFY_SCAN_IS (std::ctype_base::alnum, p2, p2+5);
  VERIFY_SCAN_IS (std::ctype_base::alpha, p2, p2+5);
  VERIFY_SCAN_IS (std::ctype_base::cntrl, p2, p2+5);
  VERIFY_SCAN_IS (std::ctype_base::digit, p2, p2+5);
  VERIFY_SCAN_IS (std::ctype_base::graph, p2, p2);
  VERIFY_SCAN_IS (std::ctype_base::lower, p2, p2+5);
  VERIFY_SCAN_IS (std::ctype_base::print, p2, p2);
  VERIFY_SCAN_IS (std::ctype_base::punct, p2, p2);
  VERIFY_SCAN_IS (std::ctype_base::space, p2, p2+5);
  VERIFY_SCAN_IS (std::ctype_base::upper, p2, p2+5);
  VERIFY_SCAN_IS (std::ctype_base::xdigit, p2, p2+5);

  VERIFY_SCAN_NOT (std::ctype_base::alnum, p2, p2);
  VERIFY_SCAN_NOT (std::ctype_base::alpha, p2, p2);
  VERIFY_SCAN_NOT (std::ctype_base::cntrl, p2, p2);
  VERIFY_SCAN_NOT (std::ctype_base::digit, p2, p2);
  VERIFY_SCAN_NOT (std::ctype_base::graph, p2, p2+5);
  VERIFY_SCAN_NOT (std::ctype_base::lower, p2, p2);
  VERIFY_SCAN_NOT (std::ctype_base::print, p2, p2+5);
  VERIFY_SCAN_NOT (std::ctype_base::punct, p2, p2+5);
  VERIFY_SCAN_NOT (std::ctype_base::space, p2, p2);
  VERIFY_SCAN_NOT (std::ctype_base::upper, p2, p2);
  VERIFY_SCAN_NOT (std::ctype_base::xdigit, p2, p2);
}

int main() 
{
  test01();
  return 0;
}
