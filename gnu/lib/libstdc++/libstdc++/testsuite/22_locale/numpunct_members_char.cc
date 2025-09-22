// 2001-01-17 Benjamin Kosnik  <bkoz@redhat.com>

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

// 22.2.3.1.1 nunpunct members

#include <locale>
#include <testsuite_hooks.h>

// XXX This test is not working for non-glibc locale models.
// { dg-do run { xfail *-*-* } }

void test01()
{
  using namespace std;
  
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

  // cache the numpunct facets
  const numpunct<char>& nump_c = use_facet<numpunct<char> >(loc_c); 
  const numpunct<char>& nump_us = use_facet<numpunct<char> >(loc_us); 
  const numpunct<char>& nump_fr = use_facet<numpunct<char> >(loc_fr); 
  const numpunct<char>& nump_de = use_facet<numpunct<char> >(loc_de); 

  // sanity check the data is correct.
  char dp1 = nump_c.decimal_point();
  char th1 = nump_c.thousands_sep();
  string g1 = nump_c.grouping();
  string t1 = nump_c.truename();
  string f1 = nump_c.falsename();

  char dp2 = nump_us.decimal_point();
  char th2 = nump_us.thousands_sep();
  string g2 = nump_us.grouping();
  string t2 = nump_us.truename();
  string f2 = nump_us.falsename();

  char dp3 = nump_fr.decimal_point();
  char th3 = nump_fr.thousands_sep();
  string g3 = nump_fr.grouping();
  string t3 = nump_fr.truename();
  string f3 = nump_fr.falsename();

  char dp4 = nump_de.decimal_point();
  char th4 = nump_de.thousands_sep();
  string g4 = nump_de.grouping();
  string t4 = nump_de.truename();
  string f4 = nump_de.falsename();

  VERIFY( dp2 != dp3 );
  VERIFY( th2 != th3 );

  VERIFY( dp2 != dp4 );
  VERIFY( th2 != th4 );
  // XXX This isn't actually supported right now.
  // VERIFY( t2 != t3 );
  // VERIFY( f2 != f3 );
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
