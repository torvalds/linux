// 2001-08-23 Benjamin Kosnik  <bkoz@redhat.com>

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

// 22.2.6.3.1 moneypunct members

#include <locale>
#include <testsuite_hooks.h>

// XXX This test is not working for non-glibc locale models.
// { dg-do run { xfail *-*-* } }

void test01()
{
  using namespace std;
  typedef money_base::part part;
  typedef money_base::pattern pattern;

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

  // cache the moneypunct facets
  typedef moneypunct<char, true> __money_true;
  typedef moneypunct<char, false> __money_false;
  const __money_true& monp_c_t = use_facet<__money_true>(loc_c); 
  const __money_true& monp_us_t = use_facet<__money_true>(loc_us); 
  const __money_true& monp_fr_t = use_facet<__money_true>(loc_fr); 
  const __money_true& monp_de_t = use_facet<__money_true>(loc_de); 
  const __money_false& monp_c_f = use_facet<__money_false>(loc_c); 
  const __money_false& monp_us_f = use_facet<__money_false>(loc_us); 
  const __money_false& monp_fr_f = use_facet<__money_false>(loc_fr); 
  const __money_false& monp_de_f = use_facet<__money_false>(loc_de); 

  // quick sanity check for data.
  char q1 = monp_c_t.decimal_point();
  char q2 = monp_c_t.thousands_sep();
  char q3 = monp_c_f.decimal_point();
  char q4 = monp_c_f.thousands_sep();
  VERIFY( q1 != char() );
  VERIFY( q2 != char() );
  VERIFY( q3 != char() );
  VERIFY( q4 != char() );

  // sanity check the data is correct.
  char dp1 = monp_c_t.decimal_point();
  char th1 = monp_c_t.thousands_sep();
  string g1 = monp_c_t.grouping();
  string cs1 = monp_c_t.curr_symbol();
  string ps1 = monp_c_t.positive_sign();
  string ns1 = monp_c_t.negative_sign();
  int fd1 = monp_c_t.frac_digits();
  pattern pos1 = monp_c_t.pos_format();
  pattern neg1 = monp_c_t.neg_format();

  char dp2 = monp_de_t.decimal_point();
  char th2 = monp_de_t.thousands_sep();
  string g2 = monp_de_t.grouping();
  string cs2 = monp_de_t.curr_symbol();
  string ps2 = monp_de_t.positive_sign();
  string ns2 = monp_de_t.negative_sign();
  int fd2 = monp_de_t.frac_digits();
  pattern pos2 = monp_de_t.pos_format();
  pattern neg2 = monp_de_t.neg_format();

  VERIFY( dp1 != dp2 );
  VERIFY( th1 != th2 );
  VERIFY( g1 != g2 );
  VERIFY( cs1 != cs2 );
  //  VERIFY( ps1 != ps2 );
  VERIFY( ns1 != ns2 );
  VERIFY( fd1 != fd2 );
  VERIFY(static_cast<part>(pos1.field[0]) != static_cast<part>(pos2.field[0]));
  VERIFY(static_cast<part>(pos1.field[1]) != static_cast<part>(pos2.field[1]));
  VERIFY(static_cast<part>(pos1.field[2]) != static_cast<part>(pos2.field[2]));
  VERIFY(static_cast<part>(pos1.field[3]) != static_cast<part>(pos2.field[3]));

  VERIFY(static_cast<part>(neg1.field[0]) != static_cast<part>(neg2.field[0]));
  VERIFY(static_cast<part>(neg1.field[1]) != static_cast<part>(neg2.field[1]));
  VERIFY(static_cast<part>(neg1.field[2]) != static_cast<part>(neg2.field[2]));
  VERIFY(static_cast<part>(neg1.field[3]) != static_cast<part>(neg2.field[3]));
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
