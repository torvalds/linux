// 2001-08-24 Benjamin Kosnik  <bkoz@redhat.com>

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

// 22.2.6.4 Template class moneypunct_byname

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
  string str;

  locale loc_byname(locale::classic(), new moneypunct_byname<char>("de_DE"));
  str = loc_byname.name();

  locale loc_de("de_DE");
  str = loc_de.name();

  locale loc_c = locale::classic();

  VERIFY( loc_de != loc_byname );

  // cache the moneypunct facets
  const moneypunct<char>& monp_c = use_facet<moneypunct<char> >(loc_c); 
  const moneypunct<char>& monp_byname = 
                                    use_facet<moneypunct<char> >(loc_byname); 
  const moneypunct<char>& monp_de = use_facet<moneypunct<char> >(loc_de); 

  // sanity check that the data match
  char dp1 = monp_de.decimal_point();
  char th1 = monp_de.thousands_sep();
  string g1 = monp_de.grouping();
  string cs1 = monp_de.curr_symbol();
  string ps1 = monp_de.positive_sign();
  string ns1 = monp_de.negative_sign();
  int fd1 = monp_de.frac_digits();
  pattern pos1 = monp_de.pos_format();
  pattern neg1 = monp_de.neg_format();

  char dp2 = monp_byname.decimal_point();
  char th2 = monp_byname.thousands_sep();
  string g2 = monp_byname.grouping();
  string cs2 = monp_byname.curr_symbol();
  string ps2 = monp_byname.positive_sign();
  string ns2 = monp_byname.negative_sign();
  int fd2 = monp_byname.frac_digits();
  pattern pos2 = monp_byname.pos_format();
  pattern neg2 = monp_byname.neg_format();

  VERIFY( dp1 == dp2 );
  VERIFY( th1 == th2 );
  VERIFY( g1 == g2 );
  VERIFY( cs1 == cs2 );
  VERIFY( ps1 == ps2 );
  VERIFY( ns1 == ns2 );
  VERIFY( fd1 == fd2 );
  VERIFY(static_cast<part>(pos1.field[0]) == static_cast<part>(pos2.field[0]));
  VERIFY(static_cast<part>(pos1.field[1]) == static_cast<part>(pos2.field[1]));
  VERIFY(static_cast<part>(pos1.field[2]) == static_cast<part>(pos2.field[2]));
  VERIFY(static_cast<part>(pos1.field[3]) == static_cast<part>(pos2.field[3]));

  VERIFY(static_cast<part>(neg1.field[0]) == static_cast<part>(neg2.field[0]));
  VERIFY(static_cast<part>(neg1.field[1]) == static_cast<part>(neg2.field[1]));
  VERIFY(static_cast<part>(neg1.field[2]) == static_cast<part>(neg2.field[2]));
  VERIFY(static_cast<part>(neg1.field[3]) == static_cast<part>(neg2.field[3]));

  // ...and don't match "C"
  char dp3 = monp_c.decimal_point();
  VERIFY( dp1 != dp3 );
}

int main()
{
  test01();

  return 0;
}
