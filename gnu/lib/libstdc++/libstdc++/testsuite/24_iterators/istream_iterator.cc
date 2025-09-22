// 2001-06-25  Benjamin Kosnik  <bkoz@redhat.com>

// Copyright (C) 2001 Free Software Foundation, Inc.
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

// 24.5.1 Template class istream_iterator

#include <iterator>
#include <sstream>
#include <testsuite_hooks.h>

void test01()
{
  using namespace std;

  // Check for required base class.
  typedef istream_iterator<long> test_iterator;
  typedef iterator<input_iterator_tag, long, ptrdiff_t, const long*, 
    		   const long&> base_iterator;
  test_iterator  r_it;
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
}

// Instantiate
template class std::istream_iterator<char>;

void test02()
{
  using namespace std;

  string st("R.Rorty");

  string re_01, re_02, re_03;
  re_02 = ",H.Putnam";
  re_03 = "D.Dennett,xxx,H.Putnam";
  
  stringbuf sb_01(st);
  istream is_01(&sb_01);
  istream_iterator<char> inb_01(is_01);
  istream_iterator<char> ine_01;
  re_01.assign(inb_01, ine_01);
  VERIFY( re_01 == "R.Rorty" );

  stringbuf sb_02(st);
  istream is_02(&sb_02);
  istream_iterator<char> inb_02(is_02);
  istream_iterator<char> ine_02;
  re_02.insert(re_02.begin(), inb_02, ine_02);
  VERIFY( re_02 == "R.Rorty,H.Putnam" );

  stringbuf sb_03(st);
  istream is_03(&sb_03);
  istream_iterator<char> inb_03(is_03);
  istream_iterator<char> ine_03;
  re_03.replace(re_03.begin() + 10, re_03.begin() + 13,
		inb_03, ine_03);
  VERIFY( re_03 == "D.Dennett,R.Rorty,H.Putnam" );
}

int main() 
{ 
  test01();
  test02();
  return 0;
}
