// 1999-06-28 bkoz

// Copyright (C) 1999, 2001 Free Software Foundation, Inc.
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

// 24.5.3 template class istreambuf_iterator

#include <sstream>
#include <iterator>
#include <testsuite_hooks.h>

void test01()
{
  using namespace std;

  // Check for required base class.
  typedef istreambuf_iterator<char> test_iterator;
  typedef char_traits<char>::off_type off_type;
  typedef iterator<input_iterator_tag, char, off_type, char*, char&> base_iterator;

  istringstream isstream("this tag");
  test_iterator  r_it(isstream);
  base_iterator* base = &r_it;

  // Check for required typedefs
  typedef test_iterator::value_type value_type;
  typedef test_iterator::difference_type difference_type;
  typedef test_iterator::pointer pointer;
  typedef test_iterator::reference reference;
  typedef test_iterator::iterator_category iteratory_category;

  typedef test_iterator::char_type char_type;
  typedef test_iterator::traits_type traits_type;
  typedef test_iterator::istream_type istream_type;
  typedef test_iterator::streambuf_type streambuf_type;
}

bool test02(void)
{

  typedef std::istreambuf_iterator<char> cistreambuf_iter;
  typedef cistreambuf_iter::streambuf_type cstreambuf_type;
  bool test = true;
  const char slit01[] = "playa hermosa, liberia, guanacaste";
  std::string str01(slit01);
  std::istringstream istrs00(str01);
  std::istringstream istrs01(str01);

  // ctor sanity checks
  cistreambuf_iter istrb_it01(istrs00);
  cistreambuf_iter istrb_it02;
  std::string tmp(istrb_it01, istrb_it02); 
  VERIFY( tmp == str01 );

  cistreambuf_iter istrb_it03(0);
  cistreambuf_iter istrb_it04;
  VERIFY( istrb_it03 == istrb_it04 );

  cistreambuf_iter istrb_it05(istrs01);
  cistreambuf_iter istrb_it06(istrs01.rdbuf());
  VERIFY( istrb_it05 == istrb_it06 );
  
  // bool equal(istreambuf_iter& b)
  cistreambuf_iter istrb_it07(0);
  cistreambuf_iter istrb_it08;
  VERIFY( istrb_it07.equal(istrb_it08) );
  cistreambuf_iter istrb_it09(0);
  cistreambuf_iter istrb_it10;
  VERIFY( istrb_it10.equal(istrb_it09) );

  cistreambuf_iter istrb_it11(istrs01);
  cistreambuf_iter istrb_it12(istrs01.rdbuf());
  VERIFY( istrb_it11.equal(istrb_it12) );
  cistreambuf_iter istrb_it13(istrs01);
  cistreambuf_iter istrb_it14(istrs01.rdbuf());
  VERIFY( istrb_it14.equal(istrb_it13) );

  cistreambuf_iter istrb_it15(istrs01);
  cistreambuf_iter istrb_it16;
  VERIFY( !(istrb_it15.equal(istrb_it16)) );
  cistreambuf_iter istrb_it17(istrs01);
  cistreambuf_iter istrb_it18;
  VERIFY( !(istrb_it18.equal(istrb_it17)) );

  // bool operator==(const istreambuf_iterator&a, const istreambuf_iterator& b)
  // bool operator!=(const istreambuf_iterator&a, const istreambuf_iterator& b)
  cistreambuf_iter istrb_it19(0);
  cistreambuf_iter istrb_it20;
  VERIFY( istrb_it19 == istrb_it20 );

  cistreambuf_iter istrb_it21(istrs01);
  cistreambuf_iter istrb_it22(istrs01.rdbuf());
  VERIFY( istrb_it22 == istrb_it21 );

  cistreambuf_iter istrb_it23(istrs01);
  cistreambuf_iter istrb_it24;
  VERIFY( istrb_it23 != istrb_it24 );

  cistreambuf_iter istrb_it25(0);
  cistreambuf_iter istrb_it26(istrs01.rdbuf());
  VERIFY( istrb_it25 != istrb_it26 );

  // charT operator*() const
  // istreambuf_iterator& operator++();
  // istreambuf_iterator& operator++(int);
  cistreambuf_iter istrb_it27(istrs01.rdbuf());
  char c;
  for (int i = 0; i < sizeof(slit01) - 2; ++i)
    {
      c = *istrb_it27++;
      VERIFY( c == slit01[i] );
    }

  std::istringstream istrs02(str01);
  cistreambuf_iter istrb_it28(istrs02);
  for (int i = 0; i < sizeof(slit01) - 2;)
    {
      c = *++istrb_it28;
      VERIFY( c == slit01[++i] );
    }

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}

// libstdc++/2627
void test03()
{
  bool test = true;
  const std::string s("free the vieques");

  // 1
  std::string res_postfix;
  std::istringstream iss01(s);
  std::istreambuf_iterator<char> isbufit01(iss01);
  for (int j = 0; j < s.size(); ++j, isbufit01++)
    res_postfix += *isbufit01;

  // 2
  std::string res_prefix;
  std::istringstream iss02(s);
  std::istreambuf_iterator<char> isbufit02(iss02);
  for (int j = 0; j < s.size(); ++j, ++isbufit02)
    res_prefix += *isbufit02;

  // 3 mixed
  std::string res_mixed;
  std::istringstream iss03(s);
  std::istreambuf_iterator<char> isbufit03(iss03);
  for (int j = 0; j < int(s.size() / 2); ++j)
    {
      res_mixed += *isbufit03;
      ++isbufit03;
      res_mixed += *isbufit03;
      isbufit03++;
    }

  VERIFY ( res_postfix == res_prefix );
  VERIFY ( res_mixed == res_prefix );
}

int main()
{
  test01();
  test02();
  test03();
  return 0;
}
