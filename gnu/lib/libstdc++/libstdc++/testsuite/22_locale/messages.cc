// 2001-07-17  Benjamin Kosnik  <bkoz@redhat.com>

// Copyright (C) 2001 Free Software Foundation
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

// 22.2.7 The message retrieval category

// { dg-do compile }

#include <locale>

void test01()
{
  // Check for required base class.
  typedef std::messages<char> test_type;
  typedef std::locale::facet base_type1;
  typedef std::messages_base base_type2;
  const test_type& obj = std::use_facet<test_type>(std::locale()); 
  const base_type1* base1 = &obj;
  const base_type2* base2 = &obj;

  // Check for required typedefs
  typedef test_type::char_type char_type;
  typedef test_type::string_type string_type;
}

// Should be able to instantiate this for other types besides char, wchar_t
class gnu_messages: public std::messages<unsigned char> 
{ };

void test02()
{ 
  gnu_messages facet01;
}

int main()
{
  test01();
  test02();
  return 0;
}
