// Copyright (C) 2000, 2001, 2002 Free Software Foundation, Inc.
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

// 22.2.1.3.2 ctype<char> members

#include <locale>
#include <vector>
#include <testsuite_hooks.h>

// XXX This test (test02) is not working for non-glibc locale models.
// { dg-do run { xfail *-*-* } }

#ifdef _GLIBCPP_USE_WCHAR_T
typedef wchar_t char_type;
class gnu_ctype: public std::ctype<char_type> { };

void test01()
{
  bool test = true;
  const char_type strlit00[] = L"manilla, cebu, tandag PHILIPPINES";
  const char_type strlit01[] = L"MANILLA, CEBU, TANDAG PHILIPPINES";
  const char_type strlit02[] = L"manilla, cebu, tandag philippines";
  const char_type c00 = L'S';
  const char_type c10 = L's';
  const char_type c20 = L'9';
  const char_type c30 = L' ';
  const char_type c40 = L'!';
  const char_type c50 = L'F';
  const char_type c60 = L'f';
  const char_type c70 = L'X';
  const char_type c80 = L'x';

  gnu_ctype gctype;
  char_type c100;
  int len = std::char_traits<char_type>::length(strlit00);
  char_type c_array[len + 1];

  // sanity check ctype_base::mask members
  int i01 = std::ctype_base::space;
  int i02 = std::ctype_base::upper;
  int i03 = std::ctype_base::lower;
  int i04 = std::ctype_base::digit;
  int i05 = std::ctype_base::punct;
  int i06 = std::ctype_base::alpha;
  int i07 = std::ctype_base::xdigit;
  int i08 = std::ctype_base::alnum;
  int i09 = std::ctype_base::graph;
  int i10 = std::ctype_base::print;
  int i11 = std::ctype_base::cntrl;
  int i12 = sizeof(std::ctype_base::mask);
  VERIFY ( i01 != i02);
  VERIFY ( i02 != i03);
  VERIFY ( i03 != i04);
  VERIFY ( i04 != i05);
  VERIFY ( i05 != i06);
  VERIFY ( i06 != i07);
  VERIFY ( i07 != i08);
  VERIFY ( i08 != i09);
  VERIFY ( i09 != i10);
  VERIFY ( i10 != i11);
  VERIFY ( i11 != i01);

  // bool is(mask m, char_type c) const;
  VERIFY( gctype.is(std::ctype_base::space, c30) );
  VERIFY( gctype.is(std::ctype_base::upper, c00) );
  VERIFY( gctype.is(std::ctype_base::lower, c10) );
  VERIFY( gctype.is(std::ctype_base::digit, c20) );
  VERIFY( gctype.is(std::ctype_base::punct, c40) );
  VERIFY( gctype.is(std::ctype_base::alpha, c50) );
  VERIFY( gctype.is(std::ctype_base::alpha, c60) );
  VERIFY( gctype.is(std::ctype_base::xdigit, c20) );
  VERIFY( !gctype.is(std::ctype_base::xdigit, c80) );
  VERIFY( gctype.is(std::ctype_base::alnum, c50) );
  VERIFY( gctype.is(std::ctype_base::alnum, c20) );
  VERIFY( gctype.is(std::ctype_base::graph, c40) );
  VERIFY( gctype.is(std::ctype_base::graph, c20) );

  // const char* is(const char* low, const char* high, mask* vec) const
  std::ctype_base::mask m00 = static_cast<std::ctype_base::mask>(0);
  std::ctype_base::mask m01[3];
  std::ctype_base::mask m02[13];
  const char_type* cc0 = strlit00;
  const char_type* cc1 = NULL;
  const char_type* cc2 = NULL;

  cc0 = strlit00;
  m01[0] = m00;
  m01[1] = m00;
  m01[2] = m00;
  cc1 = gctype.is(cc0, cc0, m01);
  VERIFY( cc1 == strlit00 );
  VERIFY( m01[0] == m00 );
  VERIFY( m01[1] == m00 );
  VERIFY( m01[2] == m00 );

  cc0 = strlit00;
  m01[0] = m00;
  m01[1] = m00;
  m01[2] = m00;
  cc2 = gctype.is(cc0, cc0 + 3, m01);
  VERIFY( cc2 == strlit00 + 3);
#if 0
  VERIFY( m01[0] != m00 );
  VERIFY( m01[1] != m00 );
  VERIFY( m01[2] != m00 );
  VERIFY( gctype.is(m01[0], cc0[0]) );
  VERIFY( gctype.is(m01[1], cc0[1]) );
  VERIFY( gctype.is(m01[2], cc0[2]) );
#endif

  cc0 = strlit01;
  cc1 = gctype.is(cc0, cc0 + 13, m02);
  VERIFY( cc1 == strlit01 + 13);
#if 0
  VERIFY( m02[6] != m00 );
  VERIFY( m02[7] != m00 );
  VERIFY( m02[8] != m00 );
  VERIFY( m02[8] != m02[6] );
  VERIFY( m02[6] != m02[7] );
  VERIFY( static_cast<bool>(m02[6] & std::ctype_base::alnum) );
  VERIFY( static_cast<bool>(m02[6] & std::ctype_base::upper) );
  VERIFY( static_cast<bool>(m02[6] & std::ctype_base::alpha) );
  VERIFY( static_cast<bool>(m02[7] & std::ctype_base::punct) );
  VERIFY( static_cast<bool>(m02[8] & std::ctype_base::space) );
  VERIFY( gctype.is(m02[6], cc0[6]) );
  VERIFY( gctype.is(m02[7], cc0[7]) );
  VERIFY( gctype.is(m02[8], cc0[8]) );
#endif
}

// libstdc++/5280
void test04()
{
#ifdef _GLIBCPP_HAVE_SETENV 
  // Set the global locale to non-"C".
  std::locale loc_de("de_DE");
  std::locale::global(loc_de);

  // Set LANG environment variable to de_DE.
  const char* oldLANG = getenv("LANG");
  if (!setenv("LANG", "de_DE", 1))
    {
      test01();
      setenv("LANG", oldLANG ? oldLANG : "", 1);
    }
#endif
}

// http://gcc.gnu.org/ml/libstdc++/2002-05/msg00038.html
void test05()
{
  bool test = true;

  const char* tentLANG = std::setlocale(LC_ALL, "ja_JP.eucjp");
  if (tentLANG != NULL)
    {
      std::string preLANG = tentLANG;
      test01();
      std::string postLANG = std::setlocale(LC_ALL, NULL);
      VERIFY( preLANG == postLANG );
    }
}
#endif

int main() 
{
#ifdef _GLIBCPP_USE_WCHAR_T
  test01();
  test04();
  test05();
#endif
  return 0;
}
