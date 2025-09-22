// 2001-07-17 Benjamin Kosnik  <bkoz@redhat.com>

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

// 22.2.7.1.1 messages members

#include <locale>
#include <testsuite_hooks.h>

// XXX This test is not working for non-glibc locale models.
// { dg-do run { xfail *-*-* } }

void test01()
{
  using namespace std;
  typedef std::messages<char>::catalog catalog;
  typedef std::messages<char>::string_type string_type;

  bool test = true;
  // This is exported through RUNTESTFLAGS in testsuite/Makefile.am.
  const char* dir = LOCALEDIR;

  // basic construction
  locale loc_c = locale::classic();
  locale loc_us("en_US");
  locale loc_fr("fr_FR");
  locale loc_de("de_DE");
  VERIFY( loc_c != loc_de );
  VERIFY( loc_us != loc_fr );
  VERIFY( loc_us != loc_de );
  VERIFY( loc_de != loc_fr );

  // cache the messages facets
  const messages<char>& mssg_c = use_facet<messages<char> >(loc_c); 
  const messages<char>& mssg_us = use_facet<messages<char> >(loc_us); 
  const messages<char>& mssg_fr = use_facet<messages<char> >(loc_fr); 
  const messages<char>& mssg_de = use_facet<messages<char> >(loc_de); 

  // catalog open(const string&, const locale&) const;
  // string_type get(catalog, int, int, const string_type& ) const; 
  // void close(catalog) const;

  // Check German (de_DE) locale.
  catalog cat_de = mssg_de.open("libstdc++", loc_c, dir);
  string s01 = mssg_de.get(cat_de, 0, 0, "please");
  string s02 = mssg_de.get(cat_de, 0, 0, "thank you");
  VERIFY ( s01 == "bitte" );
  VERIFY ( s02 == "danke" );
  mssg_de.close(cat_de);

  // Check French (fr_FR) locale.
  catalog cat_fr = mssg_fr.open("libstdc++", loc_c, dir);
  s01 = mssg_fr.get(cat_fr, 0, 0, "please");
  s02 = mssg_fr.get(cat_fr, 0, 0, "thank you");
  VERIFY ( s01 == "s'il vous plaît" );
  VERIFY ( s02 == "merci" );
  mssg_fr.close(cat_fr);

  // Check US (en_US) locale.
  catalog cat_us = mssg_fr.open("libstdc++", loc_c, dir);
  s01 = mssg_us.get(cat_us, 0, 0, "please");
  s02 = mssg_us.get(cat_us, 0, 0, "thank you");
  VERIFY ( s01 == "please" );
  VERIFY ( s02 == "thank you" );
  mssg_us.close(cat_us);
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

// http://gcc.gnu.org/ml/libstdc++/2002-05/msg00038.html
void test03()
{
  bool test = true;

  const char* tentLANG = std::setlocale(LC_ALL, "ja_JP.eucjp");
  if (tentLANG != NULL)
    {
      std::string preLANG = tentLANG;
      test01();
      std::string postLANG = std::setlocale(LC_ALL, NULL);
      VERIFY( preLANG == postLANG );
    }
}

int main()
{
  test01();
  test02();
  test03();
  return 0;
}
