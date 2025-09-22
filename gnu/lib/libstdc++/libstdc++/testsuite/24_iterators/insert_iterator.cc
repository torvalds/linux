// 2001-06-21  Benjamin Kosnik  <bkoz@redhat.com>

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

// 24.4.2.5 Template class insert_iterator

#include <iterator>
#include <list>

void test01()
{
  using namespace std;

  // Check for required base class.
  list<int> l;
  list<int>::iterator li;

  typedef insert_iterator<list<int> > test_iterator;
  typedef iterator<output_iterator_tag, void, void, void, void> base_iterator;
  test_iterator  r_it(l, li);
  base_iterator* base = &r_it;

  // Check for required typedefs
  typedef test_iterator::value_type value_type;
  typedef test_iterator::difference_type difference_type;
  typedef test_iterator::pointer pointer;
  typedef test_iterator::reference reference;
  typedef test_iterator::iterator_category iteratory_category;
  typedef test_iterator::container_type container_type;
}



// Make sure iterator can be instantiated.
template class std::insert_iterator<std::list<int> >;

void test02()
{
  typedef std::insert_iterator<std::list<int> > iterator_type;

  std::list<int> li;
  std::list<int>::iterator liit;
  iterator_type it01(li, liit);
  iterator_type it02 = std::inserter(li, liit);
}

// Check data member 'container' accessible.
class test_dm : public std::insert_iterator<std::list<int> >
{
  container_type l;
  container_type::iterator i;
  container_type* p;
public:
  test_dm(): std::insert_iterator<std::list<int> >(l, i), p(container) { }
};

int main() 
{ 
  test01();
  test02();
  return 0;
}
