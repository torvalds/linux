// 2001-11-21 Benjamin Kosnik  <bkoz@redhat.com>

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

// 22.2.2.1.1  num_get members

#include <locale>
#include <sstream>
#include <testsuite_hooks.h>

// XXX This test is not working for non-glibc locale models.
// { dg-do run { xfail *-*-* } }

void test01()
{
  using namespace std;
  typedef istreambuf_iterator<char> iterator_type;

  bool test = true;

  // basic construction
  locale loc_c = locale::classic();
  locale loc_hk("en_HK");
  locale loc_fr("fr_FR@euro");
  locale loc_de("de_DE");
  VERIFY( loc_c != loc_de );
  VERIFY( loc_hk != loc_fr );
  VERIFY( loc_hk != loc_de );
  VERIFY( loc_de != loc_fr );

  // cache the numpunct facets
  const numpunct<char>& numpunct_c = use_facet<numpunct<char> >(loc_c); 
  const numpunct<char>& numpunct_de = use_facet<numpunct<char> >(loc_de); 
  const numpunct<char>& numpunct_hk = use_facet<numpunct<char> >(loc_hk); 

  // sanity check the data is correct.
  const string empty;
  char c;

  bool b1 = true;
  bool b0 = false;
  long l1 = 2147483647;
  long l2 = -2147483647;
  long l;
  unsigned long ul1 = 1294967294;
  unsigned long ul2 = 0;
  unsigned long ul;
  double d1 =  1.02345e+308;
  double d2 = 3.15e-308;
  double d;
  long double ld1 = 6.630025e+4;
  long double ld2 = 0.0;
  long double ld;
  void* v;
  const void* cv = &ul2;

  // cache the num_get facet
  istringstream iss;
  iss.imbue(loc_de);
  const num_get<char>& ng = use_facet<num_get<char> >(iss.getloc()); 
  const ios_base::iostate goodbit = ios_base::goodbit;
  const ios_base::iostate eofbit = ios_base::eofbit;
  ios_base::iostate err = ios_base::goodbit;

  // bool, simple
  iss.str("1");
  iterator_type os_it00 = iss.rdbuf();
  iterator_type os_it01 = ng.get(os_it00, 0, iss, err, b1);
  VERIFY( b1 == true );
  VERIFY( err & ios_base::eofbit );

  iss.str("0");
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, b0);
  VERIFY( b0 == false );
  VERIFY( err & eofbit );

  // bool, more twisted examples
  iss.imbue(loc_c);
  iss.str("true ");
  iss.clear();
  iss.setf(ios_base::boolalpha);
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, b0);
  VERIFY( b0 == true );
  VERIFY( err == goodbit );

  iss.str("false ");
  iss.clear();
  iss.setf(ios_base::boolalpha);
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, b1);
  VERIFY( b1 == false );
  VERIFY( err == goodbit );

  // long, in a locale that expects grouping
  iss.imbue(loc_hk);
  iss.str("2,147,483,647 ");
  iss.clear();
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, l);
  VERIFY( l == l1 );
  VERIFY( err == goodbit );

  iss.str("-2,147,483,647++++++");
  iss.clear();
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, l);
  VERIFY( l == l2 );
  VERIFY( err == goodbit );

  // unsigned long, in a locale that does not group
  iss.imbue(loc_c);
  iss.str("1294967294");
  iss.clear();
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ul);
  VERIFY( ul == ul1);
  VERIFY( err == eofbit );

  iss.str("0+++++++++++++++++++");
  iss.clear();
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ul);
  VERIFY( ul == ul2);
  VERIFY( err == goodbit );

  // ... and one that does
  iss.imbue(loc_de);
  iss.str("1.294.967.294+++++++");
  iss.clear();
  iss.width(20);
  iss.setf(ios_base::left, ios_base::adjustfield);
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ul);
  VERIFY( ul == ul1 );
  VERIFY( err == goodbit );

  // double
  iss.imbue(loc_c);
  iss.str("1.02345e+308++++++++");
  iss.clear();
  iss.width(20);
  iss.setf(ios_base::left, ios_base::adjustfield);
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, d);
  VERIFY( d == d1 );
  VERIFY( err == goodbit );

  iss.str("+3.15e-308");
  iss.clear();
  iss.width(20);
  iss.setf(ios_base::right, ios_base::adjustfield);
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, d);
  VERIFY( d == d2 );
  VERIFY( err == eofbit );

  iss.imbue(loc_de);
  iss.str("+1,02345e+308");
  iss.clear();
  iss.width(20);
  iss.setf(ios_base::right, ios_base::adjustfield);
  iss.setf(ios_base::scientific, ios_base::floatfield);
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, d);
  VERIFY( d == d1 );
  VERIFY( err == eofbit );

  iss.str("3,15E-308 ");
  iss.clear();
  iss.width(20);
  iss.precision(10);
  iss.setf(ios_base::right, ios_base::adjustfield);
  iss.setf(ios_base::scientific, ios_base::floatfield);
  iss.setf(ios_base::uppercase);
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, d);
  VERIFY( d == d2 );
  VERIFY( err == goodbit );

  // long double
  iss.str("6,630025e+4");
  iss.clear();
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ld);
  VERIFY( ld == ld1 );
  VERIFY( err == eofbit );

  iss.str("0 ");
  iss.clear();
  iss.precision(0);
  iss.setf(ios_base::fixed, ios_base::floatfield);
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ld);
  VERIFY( ld == 0 );
  VERIFY( err == goodbit );

  // const void
  iss.str("0xbffff74c,");
  iss.clear();
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, v);
  VERIFY( &v != &cv );
  VERIFY( err == goodbit );


#ifdef _GLIBCPP_USE_LONG_LONG
  long long ll1 = 9223372036854775807LL;
  long long ll2 = -9223372036854775807LL;
  long long ll;

  iss.str("9.223.372.036.854.775.807");
  iss.clear();
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ll);
  VERIFY( ll == ll1 );
  VERIFY( err == eofbit );
#endif
}

// 2002-01-10  David Seymour  <seymour_dj@yahoo.com>
// libstdc++/5331
void test02()
{
  using namespace std;
  bool test = true;

  // Check num_get works with other iterators besides streambuf
  // output iterators. (As long as output_iterator requirements are met.)
  typedef string::const_iterator iter_type;
  typedef num_get<char, iter_type> num_get_type;
  const ios_base::iostate goodbit = ios_base::goodbit;
  const ios_base::iostate eofbit = ios_base::eofbit;
  ios_base::iostate err = ios_base::goodbit;
  const locale loc_c = locale::classic();
  const string str("20000106 Elizabeth Durack");
  const string str2("0 true 0xbffff74c Durack");

  istringstream iss; // need an ios, add my num_get facet
  iss.imbue(locale(loc_c, new num_get_type));

  // Iterator advanced, state, output.
  const num_get_type& ng = use_facet<num_get_type>(iss.getloc());

  // 01 get(long)
  // 02 get(long double)
  // 03 get(bool)
  // 04 get(void*)

  // 01 get(long)
  long i = 0;
  err = goodbit;
  iter_type end1 = ng.get(str.begin(), str.end(), iss, err, i);
  string rem1(end1, str.end());
  VERIFY( err == goodbit );
  VERIFY( i == 20000106);
  VERIFY( rem1 == " Elizabeth Durack" );

  // 02 get(long double)
  long double ld = 0.0;
  err = goodbit;
  iter_type end2 = ng.get(str.begin(), str.end(), iss, err, ld);
  string rem2(end2, str.end());
  VERIFY( err == goodbit );
  VERIFY( ld == 20000106);
  VERIFY( rem2 == " Elizabeth Durack" );

  // 03 get(bool)
  bool b = 1;
  iss.clear();
  err = goodbit;
  iter_type end3 = ng.get(str2.begin(), str2.end(), iss, err, b);
  string rem3(end3, str2.end());
  VERIFY( err == goodbit );
  VERIFY( b == 0 );
  VERIFY( rem3 == " true 0xbffff74c Durack" );

  iss.clear();
  err = goodbit;
  iss.setf(ios_base::boolalpha);
  iter_type end4 = ng.get(++end3, str2.end(), iss, err, b);
  string rem4(end4, str2.end());
  VERIFY( err == goodbit );
  VERIFY( b == true );
  VERIFY( rem4 == " 0xbffff74c Durack" );

  // 04 get(void*)
  void* v;
  iss.clear();
  err = goodbit;
  iss.setf(ios_base::fixed, ios_base::floatfield);
  iter_type end5 = ng.get(++end4, str2.end(), iss, err, v);
  string rem5(end5, str2.end());
  VERIFY( err == goodbit );
  VERIFY( b == true );
  VERIFY( rem5 == " Durack" );
}

// libstdc++/5280
void test03()
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
      test02();
      setenv("LANG", oldLANG ? oldLANG : "", 1);
    }
#endif
}

// Testing the correct parsing of grouped hexadecimals and octals.
void test04()
{
  using namespace std;

  bool test = true;
 
  unsigned long ul;

  istringstream iss;

  // A locale that expects grouping  
  locale loc_de("de_DE");
  iss.imbue(loc_de);

  const num_get<char>& ng = use_facet<num_get<char> >(iss.getloc()); 
  const ios_base::iostate goodbit = ios_base::goodbit;
  ios_base::iostate err = ios_base::goodbit;

  iss.setf(ios::hex, ios::basefield);
  iss.str("0xbf.fff.74c ");
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ul);
  VERIFY( err == goodbit );
  VERIFY( ul == 0xbffff74c );

  iss.str("0Xf.fff ");
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ul);
  VERIFY( err == goodbit );
  VERIFY( ul == 0xffff );

  iss.str("ffe ");
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ul);
  VERIFY( err == goodbit );
  VERIFY( ul == 0xffe );

  iss.setf(ios::oct, ios::basefield);
  iss.str("07.654.321 ");
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ul);
  VERIFY( err == goodbit );
  VERIFY( ul == 07654321 );

  iss.str("07.777 ");
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ul);
  VERIFY( err == goodbit );
  VERIFY( ul == 07777 );

  iss.str("776 ");
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, ul);
  VERIFY( err == goodbit );
  VERIFY( ul == 0776 );
}

// libstdc++/5816
void test05()
{
  using namespace std;
  bool test = true;

  double d = 0.0;

  istringstream iss;
  locale loc_de("de_DE");
  iss.imbue(loc_de);

  const num_get<char>& ng = use_facet<num_get<char> >(iss.getloc()); 
  const ios_base::iostate goodbit = ios_base::goodbit;
  ios_base::iostate err = ios_base::goodbit;

  iss.str("1234,5 ");
  err = goodbit;
  ng.get(iss.rdbuf(), 0, iss, err, d);
  VERIFY( err == goodbit );
  VERIFY( d == 1234.5 );
}

// http://gcc.gnu.org/ml/libstdc++/2002-05/msg00038.html
void test06()
{
  bool test = true;

  const char* tentLANG = std::setlocale(LC_ALL, "ja_JP.eucjp");
  if (tentLANG != NULL)
    {
      std::string preLANG = tentLANG;
      test01();
      test02();
      test04();
      test05();
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
  test05();
  test06();
  return 0;
}


// Kathleen Hannah, humanitarian, woman, art-thief
