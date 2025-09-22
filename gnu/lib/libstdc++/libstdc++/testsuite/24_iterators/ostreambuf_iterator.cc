// 2001-04-30  Benjamin Kosnik  <bkoz@redhat.com>

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

// 24.5.4 template class ostreambuf_iterator

#include <sstream>
#include <iterator>
#include <testsuite_hooks.h>

void test01()
{
  using namespace std;

  // Check for required base class.
  typedef ostreambuf_iterator<char> test_iterator;
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
  typedef test_iterator::streambuf_type streambuf_type;
}

bool test02(void)
{
  typedef std::ostreambuf_iterator<char> costreambuf_iter;
  typedef costreambuf_iter::streambuf_type cstreambuf_type;
  bool test = true;
  const char slit01[] = "playa hermosa, liberia, guanacaste";
  const char slit02[] = "bodega bay, lost coast, california";
  std::string str01(slit01);
  std::string str02(slit02);
  std::string tmp;
  std::stringbuf     strbuf01;
  std::stringbuf     strbuf02(str01);
  std::ostringstream ostrs00(str01);
  std::ostringstream ostrs01(str01);

  // ctor sanity checks
  costreambuf_iter ostrb_it01(ostrs00);
  VERIFY( !ostrb_it01.failed() );
  ostrb_it01++;
  ++ostrb_it01;
  VERIFY( !ostrb_it01.failed() );
  ostrb_it01 = 'a';
  VERIFY( !ostrb_it01.failed() );
  *ostrb_it01;
  VERIFY( !ostrb_it01.failed() );

  costreambuf_iter ostrb_it02(NULL);
  VERIFY( ostrb_it02.failed() );
  ostrb_it02++;
  ++ostrb_it02;
  VERIFY( ostrb_it02.failed() );
  *ostrb_it02;
  VERIFY( ostrb_it02.failed() );
  ostrb_it02 = 'a';
  VERIFY( ostrb_it02.failed() );
  
  // charT operator*() const
  // ostreambuf_iterator& operator++();
  // ostreambuf_iterator& operator++(int);
  costreambuf_iter ostrb_it27(ostrs01);
  VERIFY( !ostrb_it27.failed() );
  int j = str02.size();
  for (int i = 0; i < j; ++i)
    ostrb_it27 = str02[i];
  VERIFY( !ostrb_it27.failed() );
  tmp = ostrs01.str();
  VERIFY ( tmp != str01 );
  VERIFY ( tmp == str02 );

  costreambuf_iter ostrb_it28(ostrs00);
  VERIFY( !ostrb_it28.failed() );
  j = ostrs00.str().size();
  for (int i = 0; i < j + 2; ++i)
    ostrb_it28 = 'b';
  VERIFY( !ostrb_it28.failed() );
  tmp = ostrs00.str();
  VERIFY ( tmp != str01 );
  VERIFY ( tmp != str02 );

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}

int main()
{
  test01();
  test02();

  return 0;
}
