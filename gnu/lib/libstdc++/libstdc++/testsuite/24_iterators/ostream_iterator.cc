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

// 24.5.4 Template class ostream_iterator

#include <iterator>
#include <ostream>
#include <sstream>

void test01()
{
  using namespace std;

  // Check for required base class.
  typedef ostream_iterator<long> test_iterator;
  typedef iterator<output_iterator_tag, void, void, void, void> base_iterator;
  ostringstream osstream("this tag");
  test_iterator  r_it(osstream);
  base_iterator* base = &r_it;

  // Check for required typedefs
  typedef test_iterator::value_type value_type;
  typedef test_iterator::difference_type difference_type;
  typedef test_iterator::pointer pointer;
  typedef test_iterator::reference reference;
  typedef test_iterator::iterator_category iteratory_category;

  typedef test_iterator::char_type char_type;
  typedef test_iterator::traits_type traits_type;
  typedef test_iterator::ostream_type ostream_type;
}

// Instantiate
template class std::ostream_iterator<char>;

int main() 
{ 
  test01();
  return 0;
}
