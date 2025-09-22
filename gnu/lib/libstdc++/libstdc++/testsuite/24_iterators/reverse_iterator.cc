// 2001-06-21  Benjamin Kosnik  <bkoz@redhat.com>

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
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

// 24.4.1.2 Reverse iterators

#include <iterator>

void test01()
{
  using namespace std;

  // Check for required base class.
  long l;
  typedef reverse_iterator<long*> test_iterator;
  typedef iterator<iterator_traits<long*>::iterator_category,
		   iterator_traits<long*>::value_type,
		   iterator_traits<long*>::difference_type,
		   iterator_traits<long*>::pointer,
                   iterator_traits<long*>::reference>
    base_iterator;
  test_iterator  r_it(&l);
  base_iterator* base = &r_it;

  // Check for required typedefs
  typedef test_iterator::value_type value_type;
  typedef test_iterator::difference_type difference_type;
  typedef test_iterator::pointer pointer;
  typedef test_iterator::reference reference;
  typedef test_iterator::iterator_category iteratory_category;
}


// Make sure iterator can be instantiated.
template class std::reverse_iterator<int*>;

void test02()
{
  typedef std::reverse_iterator<int*> iterator_type;
  iterator_type it01;
  iterator_type it02;

  // Sanity check non-member operators and functions can be instantiated. 
  it01 == it02;
  it01 != it02;
  it01 < it02;
  it01 <= it02;
  it01 > it02;
  it01 >= it02;
  it01 - it02;
  5 + it02;
}

// Check data member 'current' accessible.
class test_dm : public std::reverse_iterator<int*>
{
  int* p;
public:
  test_dm(): p(current) { }
};

int main() 
{ 
  test01();
  test02();
  return 0;
}
