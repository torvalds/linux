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

// 22.2.4.1.1 collate members

#include <locale>
#include <testsuite_hooks.h>

// XXX This may not work for non-glibc locale models.
// { dg-do run { xfail *-*-* } }

void test01()
{
  using namespace std;
  typedef std::collate<char>::string_type string_type;

  bool test = true;

  // basic construction
  locale loc_c = locale::classic();
  locale loc_us("en_US");
  locale loc_fr("fr_FR");
  locale loc_de("de_DE");
  VERIFY( loc_c != loc_de );
  VERIFY( loc_us != loc_fr );
  VERIFY( loc_us != loc_de );
  VERIFY( loc_de != loc_fr );

  // cache the collate facets
  const collate<char>& coll_c = use_facet<collate<char> >(loc_c); 
  const collate<char>& coll_us = use_facet<collate<char> >(loc_us); 
  const collate<char>& coll_fr = use_facet<collate<char> >(loc_fr); 
  const collate<char>& coll_de = use_facet<collate<char> >(loc_de); 

  // int compare(const charT*, const charT*, const charT*, const charT*) const
  // long hash(const charT*, const charT*) cosnt
  // string_type transform(const charT*, const charT*) const

  // Check "C" locale.
  const char* strlit1 = "monkey picked tikuanyin oolong";
  const char* strlit2 = "imperial tea court green oolong";

  int i1;
  int size1 = strlen(strlit1) - 1;
  i1 = coll_c.compare(strlit1, strlit1 + size1, strlit1, strlit1 + 7);
  VERIFY ( i1 == 1 );
  i1 = coll_c.compare(strlit1, strlit1 + 7, strlit1, strlit1 + size1);
  VERIFY ( i1 == -1 );
  i1 = coll_c.compare(strlit1, strlit1 + 7, strlit1, strlit1 + 7);
  VERIFY ( i1 == 0 );

  int i2;
  int size2 = strlen(strlit2) - 1;
  i2 = coll_c.compare(strlit2, strlit2 + size2, strlit2, strlit2 + 13);
  VERIFY ( i2 == 1 );
  i2 = coll_c.compare(strlit2, strlit2 + 13, strlit2, strlit2 + size2);
  VERIFY ( i2 == -1 );
  i2 = coll_c.compare(strlit2, strlit2 + size2, strlit2, strlit2 + size2);
  VERIFY ( i2 == 0 );

  long l1;
  long l2;
  l1 = coll_c.hash(strlit1, strlit1 + size1);
  l2 = coll_c.hash(strlit1, strlit1 + size1 - 1);
  VERIFY ( l1 != l2 );
  l1 = coll_c.hash(strlit1, strlit1 + size1);
  l2 = coll_c.hash(strlit2, strlit2 + size2);
  VERIFY ( l1 != l2 );

  string str1 = coll_c.transform(strlit1, strlit1 + size1);
  string str2 = coll_c.transform(strlit2, strlit2 + size2);
  i1 = str1.compare(str2);
  i2 = coll_c.compare(strlit1, strlit1 + size1, strlit2, strlit2 + size2);
  VERIFY ( i2 == 1 );
  VERIFY ( i1 * i2 > 0 );

  // Check German "de_DE" locale.
  const char* strlit3 = "Äuglein Augment"; // "C" == "Augment Äuglein"
  const char* strlit4 = "Base baß Baß Bast"; // "C" == "Base baß Baß Bast"

  int size3 = strlen(strlit3) - 1;
  i1 = coll_de.compare(strlit3, strlit3 + size3, strlit3, strlit3 + 7);
  VERIFY ( i1 == 1 );
  i1 = coll_de.compare(strlit3, strlit3 + 7, strlit3, strlit3 + size1);
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
}

// libstdc++/5280
void test02()
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

void test03()
{
  bool test = true;
  std::string str1("fffff");
  std::string str2("ffffffffffff");

  const std::locale cloc = std::locale::classic();
  const std::collate<char> &col = std::use_facet<std::collate<char> >(cloc);

  long l1 = col.hash(str1.c_str(), str1.c_str() + str1.size());
  long l2 = col.hash(str2.c_str(), str2.c_str() + str2.size());
  VERIFY( l1 != l2 );
}

// http://gcc.gnu.org/ml/libstdc++/2002-05/msg00038.html
void test04()
{
  bool test = true;

  const char* tentLANG = std::setlocale(LC_ALL, "ja_JP.eucjp");
  if (tentLANG != NULL)
    {
      std::string preLANG = tentLANG;
      test01();
      test03();
      std::string postLANG = std::setlocale(LC_ALL, NULL);
      VERIFY( preLANG == postLANG );
    }
}

int main()
{
  test01();
  test02();
  test03();
  test04();
  return 0;
}
