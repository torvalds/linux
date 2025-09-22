// 2001-08-15 Benjamin Kosnik  <bkoz@redhat.com>

// Copyright (C) 2001, 2002 Free Software Foundation
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

// 22.2.4.2 Template class collate_byname

#include <locale>
#include <testsuite_hooks.h>

// XXX This test is not working for non-glibc locale models.
// { dg-do run { xfail *-*-* } }

void test01()
{
  using namespace std;
  typedef std::collate<char>::string_type string_type;

  bool test = true;
  string str;
  locale loc_c = locale::classic();

  locale loc_byname(locale::classic(), new collate_byname<char>("de_DE"));
  str = loc_byname.name();

  locale loc_de("de_DE");
  str = loc_de.name();

  VERIFY( loc_de != loc_byname );

  // cache the collate facets
  const collate<char>& coll_byname = use_facet<collate<char> >(loc_byname); 
  const collate<char>& coll_de = use_facet<collate<char> >(loc_de); 

  // Check German "de_DE" locale.
  int i1;
  int i2;
  long l1;
  long l2;
  const char* strlit3 = "Äuglein Augment"; // "C" == "Augment Äuglein"
  const char* strlit4 = "Base baß Baß Bast"; // "C" == "Base baß Baß Bast"

  int size3 = strlen(strlit3) - 1;
  i1 = coll_de.compare(strlit3, strlit3 + size3, strlit3, strlit3 + 7);
  VERIFY ( i1 == 1 );
  i1 = coll_de.compare(strlit3, strlit3 + 7, strlit3, strlit3 + size3);
  VERIFY ( i1 == -1 );
  i1 = coll_de.compare(strlit3, strlit3 + 7, strlit3, strlit3 + 7);
  VERIFY ( i1 == 0 );

  i1 = coll_de.compare(strlit3, strlit3 + 6, strlit3 + 8, strlit3 + 14);
  VERIFY ( i1 == -1 );

  int size4 = strlen(strlit4) - 1;
  i2 = coll_de.compare(strlit4, strlit4 + size4, strlit4, strlit4 + 13);
  VERIFY ( i2 == 1 );
  i2 = coll_de.compare(strlit4, strlit4 + 13, strlit4, strlit4 + size4);
  VERIFY ( i2 == -1 );
  i2 = coll_de.compare(strlit4, strlit4 + size4, strlit4, strlit4 + size4);
  VERIFY ( i2 == 0 );

  l1 = coll_de.hash(strlit3, strlit3 + size3);
  l2 = coll_de.hash(strlit3, strlit3 + size3 - 1);
  VERIFY ( l1 != l2 );
  l1 = coll_de.hash(strlit3, strlit3 + size3);
  l2 = coll_de.hash(strlit4, strlit4 + size4);
  VERIFY ( l1 != l2 );

  string str3 = coll_de.transform(strlit3, strlit3 + size3);
  string str4 = coll_de.transform(strlit4, strlit4 + size4);
  i1 = str3.compare(str4);
  i2 = coll_de.compare(strlit3, strlit3 + size3, strlit4, strlit4 + size4);
  VERIFY ( i2 == -1 );
  VERIFY ( i1 * i2 > 0 );


  // Check byname locale
  int i3;
  int i4;
  long l3;
  long l4;
  size4 = strlen(strlit3) - 1;
  i3 = coll_de.compare(strlit3, strlit3 + size4, strlit3, strlit3 + 7);
  VERIFY ( i3 == 1 );
  i3 = coll_de.compare(strlit3, strlit3 + 7, strlit3, strlit3 + size4);
  VERIFY ( i3 == -1 );
  i3 = coll_de.compare(strlit3, strlit3 + 7, strlit3, strlit3 + 7);
  VERIFY ( i3 == 0 );

  i3 = coll_de.compare(strlit3, strlit3 + 6, strlit3 + 8, strlit3 + 14);
  VERIFY ( i3 == -1 );

  size4 = strlen(strlit4) - 1;
  i4 = coll_de.compare(strlit4, strlit4 + size4, strlit4, strlit4 + 13);
  VERIFY ( i4 == 1 );
  i4 = coll_de.compare(strlit4, strlit4 + 13, strlit4, strlit4 + size4);
  VERIFY ( i4 == -1 );
  i4 = coll_de.compare(strlit4, strlit4 + size4, strlit4, strlit4 + size4);
  VERIFY ( i4 == 0 );

  l3 = coll_de.hash(strlit3, strlit3 + size4);
  l4 = coll_de.hash(strlit3, strlit3 + size4 - 1);
  VERIFY ( l3 != l4 );
  l3 = coll_de.hash(strlit3, strlit3 + size4);
  l4 = coll_de.hash(strlit4, strlit4 + size4);
  VERIFY ( l3 != l4 );

  string str5 = coll_de.transform(strlit3, strlit3 + size3);
  string str6 = coll_de.transform(strlit4, strlit4 + size4);
  i3 = str5.compare(str6);
  i4 = coll_de.compare(strlit3, strlit3 + size4, strlit4, strlit4 + size4);
  VERIFY ( i4 == -1 );
  VERIFY ( i3 * i4 > 0 );

  // Verify byname == de
  VERIFY ( str5 == str3 );
  VERIFY ( str6 == str4 );
}

int main()
{
  test01();

  return 0;
}
