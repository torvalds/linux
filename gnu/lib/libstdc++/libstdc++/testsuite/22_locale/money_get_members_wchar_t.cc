// 2001-09-14 Benjamin Kosnik  <bkoz@redhat.com>

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

// 22.2.6.1.1 money_get members

#include <locale>
#include <sstream>
#include <testsuite_hooks.h>

// XXX This test is not working for non-glibc locale models.
// { dg-do run { xfail *-*-* } }

#ifdef _GLIBCPP_USE_WCHAR_T
// test string version
void test01()
{
  using namespace std;
  typedef money_base::part part;
  typedef money_base::pattern pattern;
  typedef istreambuf_iterator<wchar_t> iterator_type;

  bool test = true;

  // basic construction
  locale loc_c = locale::classic();
  locale loc_hk("en_HK");
  locale loc_fr("fr_FR@euro");
  locale loc_de("de_DE@euro");
  VERIFY( loc_c != loc_de );
  VERIFY( loc_hk != loc_fr );
  VERIFY( loc_hk != loc_de );
  VERIFY( loc_de != loc_fr );

  // cache the moneypunct facets
  typedef moneypunct<wchar_t, true> __money_true;
  typedef moneypunct<wchar_t, false> __money_false;
  const __money_true& monpunct_c_t = use_facet<__money_true>(loc_c); 
  const __money_true& monpunct_de_t = use_facet<__money_true>(loc_de); 
  const __money_false& monpunct_c_f = use_facet<__money_false>(loc_c); 
  const __money_false& monpunct_de_f = use_facet<__money_false>(loc_de); 
  const __money_true& monpunct_hk_t = use_facet<__money_true>(loc_hk); 
  const __money_false& monpunct_hk_f = use_facet<__money_false>(loc_hk); 

  // sanity check the data is correct.
  const wstring empty;

  // total EPA budget FY 2002
  const wstring digits1(L"720000000000");

  // est. cost, national missile "defense", expressed as a loss in USD 2001
  const wstring digits2(L"-10000000000000");  

  // not valid input
  const wstring digits3(L"-A"); 

  // input less than frac_digits
  const wstring digits4(L"-1");
  
  iterator_type end;
  wistringstream iss;
  iss.imbue(loc_de);
  // cache the money_get facet
  const money_get<wchar_t>& mon_get = use_facet<money_get<wchar_t> >(iss.getloc()); 


  iss.str(L"7.200.000.000,00 ");
  iterator_type is_it01(iss);
  wstring result1;
  ios_base::iostate err01 = ios_base::goodbit;
  mon_get.get(is_it01, end, true, iss, err01, result1);
  VERIFY( result1 == digits1 );
  VERIFY( err01 == ios_base::eofbit );

  iss.str(L"7.200.000.000,00  ");
  iterator_type is_it02(iss);
  wstring result2;
  ios_base::iostate err02 = ios_base::goodbit;
  mon_get.get(is_it02, end, true, iss, err02, result2);
  VERIFY( result2 == digits1 );
  VERIFY( err02 == ios_base::eofbit );

  iss.str(L"7.200.000.000,00  a");
  iterator_type is_it03(iss);
  wstring result3;
  ios_base::iostate err03 = ios_base::goodbit;
  mon_get.get(is_it03, end, true, iss, err03, result3);
  VERIFY( result3 == digits1 );
  VERIFY( err03 == ios_base::goodbit );

  iss.str(L"");
  iterator_type is_it04(iss);
  wstring result4;
  ios_base::iostate err04 = ios_base::goodbit;
  mon_get.get(is_it04, end, true, iss, err04, result4);
  VERIFY( result4 == empty );
  VERIFY( err04 == ios_base::failbit | ios_base::eofbit );

  iss.str(L"working for enlightenment and peace in a mad world");
  iterator_type is_it05(iss);
  wstring result5;
  ios_base::iostate err05 = ios_base::goodbit;
  mon_get.get(is_it05, end, true, iss, err05, result5);
  VERIFY( result5 == empty );
  VERIFY( err05 == ios_base::failbit );

  // now try with showbase, to get currency symbol in format
  iss.setf(ios_base::showbase);

  iss.str(L"7.200.000.000,00 EUR ");
  iterator_type is_it06(iss);
  wstring result6;
  ios_base::iostate err06 = ios_base::goodbit;
  mon_get.get(is_it06, end, true, iss, err06, result6);
  VERIFY( result6 == digits1 );
  VERIFY( err06 == ios_base::eofbit );

  iss.str(L"7.200.000.000,00 EUR  "); // Extra space.
  iterator_type is_it07(iss);
  wstring result7;
  ios_base::iostate err07 = ios_base::goodbit;
  mon_get.get(is_it07, end, true, iss, err07, result7);
  VERIFY( result7 == digits1 );
  VERIFY( err07 == ios_base::goodbit );

  iss.str(L"7.200.000.000,00 \x20ac"); 
  iterator_type is_it08(iss);
  wstring result8;
  ios_base::iostate err08 = ios_base::goodbit;
  mon_get.get(is_it08, end, false, iss, err08, result8);
  VERIFY( result8 == digits1 );
  VERIFY( err08 == ios_base::eofbit );

  iss.imbue(loc_hk);
  iss.str(L"HK$7,200,000,000.00"); 
  iterator_type is_it09(iss);
  wstring result9;
  ios_base::iostate err09 = ios_base::goodbit;
  mon_get.get(is_it09, end, false, iss, err09, result9);
  VERIFY( result9 == digits1 );
  VERIFY( err09 == ios_base::eofbit );

  iss.str(L"(HKD 100,000,000,000.00)"); 
  iterator_type is_it10(iss);
  wstring result10;
  ios_base::iostate err10 = ios_base::goodbit;
  mon_get.get(is_it10, end, true, iss, err10, result10);
  VERIFY( result10 == digits2 );
  VERIFY( err10 == ios_base::goodbit );

  iss.str(L"(HKD .01)"); 
  iterator_type is_it11(iss);
  wstring result11;
  ios_base::iostate err11 = ios_base::goodbit;
  mon_get.get(is_it11, end, true, iss, err11, result11);
  VERIFY( result11 == digits4 );
  VERIFY( err11 == ios_base::goodbit );

  // for the "en_HK" locale the parsing of the very same input streams must
  // be successful without showbase too, since the symbol field appears in
  // the first positions in the format and the symbol, when present, must be
  // consumed.
  iss.unsetf(ios_base::showbase);

  iss.str(L"HK$7,200,000,000.00"); 
  iterator_type is_it12(iss);
  wstring result12;
  ios_base::iostate err12 = ios_base::goodbit;
  mon_get.get(is_it12, end, false, iss, err12, result12);
  VERIFY( result12 == digits1 );
  VERIFY( err12 == ios_base::eofbit );

  iss.str(L"(HKD 100,000,000,000.00)"); 
  iterator_type is_it13(iss);
  wstring result13;
  ios_base::iostate err13 = ios_base::goodbit;
  mon_get.get(is_it13, end, true, iss, err13, result13);
  VERIFY( result13 == digits2 );
  VERIFY( err13 == ios_base::goodbit );

  iss.str(L"(HKD .01)"); 
  iterator_type is_it14(iss);
  wstring result14;
  ios_base::iostate err14 = ios_base::goodbit;
  mon_get.get(is_it14, end, true, iss, err14, result14);
  VERIFY( result14 == digits4 );
  VERIFY( err14 == ios_base::goodbit );
}


// test double/wstring versions
void test02()
{
  using namespace std;
  typedef money_base::part part;
  typedef money_base::pattern pattern;
  typedef istreambuf_iterator<wchar_t> iterator_type;

  bool test = true;

  // basic construction
  locale loc_c = locale::classic();
  locale loc_hk("en_HK");
  locale loc_fr("fr_FR@euro");
  locale loc_de("de_DE@euro");
  VERIFY( loc_c != loc_de );
  VERIFY( loc_hk != loc_fr );
  VERIFY( loc_hk != loc_de );
  VERIFY( loc_de != loc_fr );

  // cache the moneypunct facets
  typedef moneypunct<wchar_t, true> __money_true;
  typedef moneypunct<wchar_t, false> __money_false;
  const __money_true& monpunct_c_t = use_facet<__money_true>(loc_c); 
  const __money_true& monpunct_de_t = use_facet<__money_true>(loc_de); 
  const __money_false& monpunct_c_f = use_facet<__money_false>(loc_c); 
  const __money_false& monpunct_de_f = use_facet<__money_false>(loc_de); 
  const __money_true& monpunct_hk_t = use_facet<__money_true>(loc_hk); 
  const __money_false& monpunct_hk_f = use_facet<__money_false>(loc_hk); 

  // sanity check the data is correct.
  const wstring empty;

  // total EPA budget FY 2002
  const long double  digits1 = 720000000000.0;

  // est. cost, national missile "defense", expressed as a loss in USD 2001
  const long double digits2 = -10000000000000.0;  

  // input less than frac_digits
  const long double digits4 = -1.0;
  
  iterator_type end;
  wistringstream iss;
  iss.imbue(loc_de);
  // cache the money_get facet
  const money_get<wchar_t>& mon_get = use_facet<money_get<wchar_t> >(iss.getloc()); 

  iss.str(L"7.200.000.000,00 ");
  iterator_type is_it01(iss);
  long double result1;
  ios_base::iostate err01 = ios_base::goodbit;
  mon_get.get(is_it01, end, true, iss, err01, result1);
  VERIFY( result1 == digits1 );
  VERIFY( err01 == ios_base::eofbit );

  iss.str(L"7.200.000.000,00 ");
  iterator_type is_it02(iss);
  long double result2;
  ios_base::iostate err02 = ios_base::goodbit;
  mon_get.get(is_it02, end, false, iss, err02, result2);
  VERIFY( result2 == digits1 );
  VERIFY( err02 == ios_base::eofbit );

  // now try with showbase, to get currency symbol in format
  iss.setf(ios_base::showbase);

  iss.imbue(loc_hk);
  iss.str(L"(HKD .01)"); 
  iterator_type is_it03(iss);
  long double result3;
  ios_base::iostate err03 = ios_base::goodbit;
  mon_get.get(is_it03, end, true, iss, err03, result3);
  VERIFY( result3 == digits4 );
  VERIFY( err03 == ios_base::goodbit );
}

void test03()
{
  using namespace std;
  bool test = true;

  // Check money_get works with other iterators besides streambuf
  // output iterators.
  typedef wstring::const_iterator iter_type;
  typedef money_get<wchar_t, iter_type> mon_get_type;
  const ios_base::iostate goodbit = ios_base::goodbit;
  const ios_base::iostate eofbit = ios_base::eofbit;
  ios_base::iostate err = goodbit;
  const locale loc_c = locale::classic();
  const wstring str = L"0.01Eleanor Roosevelt";

  wistringstream iss; 
  iss.imbue(locale(loc_c, new mon_get_type));

  // Iterator advanced, state, output.
  const mon_get_type& mg = use_facet<mon_get_type>(iss.getloc());

  // 01 string
  wstring res1;
  iter_type end1 = mg.get(str.begin(), str.end(), false, iss, err, res1);
  wstring rem1(end1, str.end());
  VERIFY( err == goodbit );
  VERIFY( res1 == L"1" );
  VERIFY( rem1 == L"Eleanor Roosevelt" );

  // 02 long double
  iss.clear();
  err = goodbit;
  long double res2;
  iter_type end2 = mg.get(str.begin(), str.end(), false, iss, err, res2);
  wstring rem2(end2, str.end());
  VERIFY( err == goodbit );
  VERIFY( res2 == 1 );
  VERIFY( rem2 == L"Eleanor Roosevelt" );
}

// libstdc++/5280
void test04()
{
#ifdef _GLIBCPP_HAVE_SETENV 
  // Set the global locale to non-"C".
  std::locale loc_de("de_DE@euro");
  std::locale::global(loc_de);

  // Set LANG environment variable to de_DE@euro.
  const char* oldLANG = getenv("LANG");
  if (!setenv("LANG", "de_DE@euro", 1))
    {
      test01();
      test02();
      test03();
      setenv("LANG", oldLANG ? oldLANG : "", 1);
    }
#endif
}

struct My_money_io : public std::moneypunct<wchar_t,false>
{
  char_type do_decimal_point() const { return L'.'; }
  std::string do_grouping() const { return "\004"; }
  
  std::wstring do_curr_symbol() const { return L"$"; }
  std::wstring do_positive_sign() const { return L""; }
  std::wstring do_negative_sign() const { return L"-"; }
  
  int do_frac_digits() const { return 2; }

  pattern do_pos_format() const
  {
    pattern pat = { { symbol, none, sign, value } };
    return pat;
  }

  pattern do_neg_format() const
  {
    pattern pat = { { symbol, none, sign, value } };
    return pat;
  }
};

// libstdc++/5579
void test05()
{
  using namespace std;
  bool test = true;
  typedef istreambuf_iterator<wchar_t> InIt;

  locale loc(locale::classic(), new My_money_io);

  wstring bufferp(L"$1234.56");
  wstring buffern(L"$-1234.56");
  wstring bufferp_ns(L"1234.56");
  wstring buffern_ns(L"-1234.56");

  bool intl = false;

  InIt iendp, iendn, iendp_ns, iendn_ns;
  ios_base::iostate err;
  wstring valp, valn, valp_ns, valn_ns;

  const money_get<wchar_t,InIt>& mg  =
    use_facet<money_get<wchar_t, InIt> >(loc);

  wistringstream fmtp(bufferp);
  fmtp.imbue(loc);
  InIt ibegp(fmtp);
  mg.get(ibegp,iendp,intl,fmtp,err,valp);
  VERIFY( valp == L"123456" );

  wistringstream fmtn(buffern);
  fmtn.imbue(loc);
  InIt ibegn(fmtn);
  mg.get(ibegn,iendn,intl,fmtn,err,valn);
  VERIFY( valn == L"-123456" );

  wistringstream fmtp_ns(bufferp_ns);
  fmtp_ns.imbue(loc);
  InIt ibegp_ns(fmtp_ns);
  mg.get(ibegp_ns,iendp_ns,intl,fmtp_ns,err,valp_ns);
  VERIFY( valp_ns == L"123456" );

  wistringstream fmtn_ns(buffern_ns);
  fmtn_ns.imbue(loc);
  InIt ibegn_ns(fmtn_ns);
  mg.get(ibegn_ns,iendn_ns,intl,fmtn_ns,err,valn_ns);
  VERIFY( valn_ns == L"-123456" );
}

// We were appending to the string val passed by reference, instead
// of constructing a temporary candidate, eventually copied into
// val in case of successful parsing.
void test06()
{
  using namespace std;
  bool test = true;

  typedef istreambuf_iterator<wchar_t> InIt;
  InIt iend1, iend2, iend3;

  locale loc;
  wstring buffer1(L"123");
  wstring buffer2(L"456");
  wstring buffer3(L"Golgafrincham");

  wstring val;

  ios_base::iostate err;

  const money_get<wchar_t,InIt>& mg =
    use_facet<money_get<wchar_t, InIt> >(loc);

  wistringstream fmt1(buffer1);
  InIt ibeg1(fmt1);
  mg.get(ibeg1,iend1,false,fmt1,err,val);
  VERIFY( val == buffer1 );

  wistringstream fmt2(buffer2);
  InIt ibeg2(fmt2);
  mg.get(ibeg2,iend2,false,fmt2,err,val);
  VERIFY( val == buffer2 );

  val = buffer3;
  wistringstream fmt3(buffer3);
  InIt ibeg3(fmt3);
  mg.get(ibeg3,iend3,false,fmt3,err,val);
  VERIFY( val == buffer3 );
}

struct My_money_io_a : public std::moneypunct<wchar_t,false>
{
  char_type do_decimal_point() const { return L'.'; }
  std::string do_grouping() const { return "\004"; }
  
  std::wstring do_curr_symbol() const { return L"$"; }
  std::wstring do_positive_sign() const { return L"()"; }
  
  int do_frac_digits() const { return 2; }

  pattern do_pos_format() const
  {
    pattern pat = { { sign, value, space, symbol } };
    return pat;
  }
};

struct My_money_io_b : public std::moneypunct<wchar_t,false>
{
  char_type do_decimal_point() const { return L'.'; }
  std::string do_grouping() const { return "\004"; }
  
  std::wstring do_curr_symbol() const { return L"$"; }
  std::wstring do_positive_sign() const { return L"()"; }
  
  int do_frac_digits() const { return 2; }

  pattern do_pos_format() const
  {
    pattern pat = { { sign, value, symbol, none } };
    return pat;
  }
};

// This one exercises patterns of the type { X, Y, Z, symbol } and
// { X, Y, symbol, none } for a two character long sign. Therefore
// the optional symbol (showbase is false by default) must be consumed
// if present, since "rest of the sign" is left to read.
void test07()
{
  using namespace std;
  bool test = true;
  typedef istreambuf_iterator<wchar_t> InIt;

  bool intl = false;
  ios_base::iostate err;

  locale loc_a(locale::classic(), new My_money_io_a);

  wstring buffer_a(L"(1234.56 $)");
  wstring buffer_a_ns(L"(1234.56 )");

  InIt iend_a, iend_a_ns;
  wstring val_a, val_a_ns;

  const money_get<wchar_t,InIt>& mg_a  =
    use_facet<money_get<wchar_t, InIt> >(loc_a);

  wistringstream fmt_a(buffer_a);
  fmt_a.imbue(loc_a);
  InIt ibeg_a(fmt_a);
  mg_a.get(ibeg_a,iend_a,intl,fmt_a,err,val_a);
  VERIFY( val_a == L"123456" );

  wistringstream fmt_a_ns(buffer_a_ns);
  fmt_a_ns.imbue(loc_a);
  InIt ibeg_a_ns(fmt_a_ns);
  mg_a.get(ibeg_a_ns,iend_a_ns,intl,fmt_a_ns,err,val_a_ns);
  VERIFY( val_a_ns == L"123456" );

  locale loc_b(locale::classic(), new My_money_io_b);

  wstring buffer_b(L"(1234.56$)");
  wstring buffer_b_ns(L"(1234.56)");

  InIt iend_b, iend_b_ns;
  wstring val_b, val_b_ns;

  const money_get<wchar_t,InIt>& mg_b  =
    use_facet<money_get<wchar_t, InIt> >(loc_b);

  wistringstream fmt_b(buffer_b);
  fmt_b.imbue(loc_b);
  InIt ibeg_b(fmt_b);
  mg_b.get(ibeg_b,iend_b,intl,fmt_b,err,val_b);
  VERIFY( val_b == L"123456" );

  wistringstream fmt_b_ns(buffer_b_ns);
  fmt_b_ns.imbue(loc_b);
  InIt ibeg_b_ns(fmt_b_ns);
  mg_b.get(ibeg_b_ns,iend_b_ns,intl,fmt_b_ns,err,val_b_ns);
  VERIFY( val_b_ns == L"123456" );
}

// http://gcc.gnu.org/ml/libstdc++/2002-05/msg00038.html
void test08()
{
  bool test = true;

  const char* tentLANG = std::setlocale(LC_ALL, "ja_JP.eucjp");
  if (tentLANG != NULL)
    {
      std::string preLANG = tentLANG;
      test01();
      test02();
      test03();
      test05();
      test06();
      test07();
      std::string postLANG = std::setlocale(LC_ALL, NULL);
      VERIFY( preLANG == postLANG );
    }
}
#endif

int main()
{
#ifdef _GLIBCPP_USE_WCHAR_T
  test01();
  test02();
  test03();
  test04();
  test05();
  test06();
  test07();
  test08();
#endif
  return 0;
}
