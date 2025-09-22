// 2001-08-27  Benjamin Kosnik  <bkoz@redhat.com>

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

// 22.2.1.5  Template class codecvt

// { dg-do compile }

#include <locale>

void test01()
{
  // Check for required base class.
  typedef std::codecvt<char, char, mbstate_t> test_type;
  typedef std::locale::facet base_type;
  const test_type& obj = std::use_facet<test_type>(std::locale()); 
  const base_type* base = &obj;
  
  // Check for required typedefs
  typedef test_type::intern_type intern_type;
  typedef test_type::extern_type extern_type;
  typedef test_type::state_type state_type;
}

// Should be able to instantiate this for other types besides char, wchar_t
class gnu_codecvt: public std::codecvt<unsigned char, unsigned long, char> 
{ };

void test02()
{ 
  gnu_codecvt facet01;
}

int main()
{
  test01();
  test02();
  return 0;
}
