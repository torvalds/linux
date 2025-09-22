// 2001-10-02 Benjamin Kosnik  <bkoz@redhat.com>

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

// 22.2.5.1.1 time_get members

#include <locale>
#include <sstream>
#include <testsuite_hooks.h>

// XXX This test is not working for non-glibc locale models.
// { dg-do run { xfail *-*-* } }

#ifdef _GLIBCPP_USE_WCHAR_T
void test01()
{
  using namespace std;
  typedef time_base::dateorder dateorder;
  typedef istreambuf_iterator<wchar_t> iterator_type;

  bool test = true;

  // basic construction and sanity checks.
  locale loc_c = locale::classic();
  locale loc_hk("en_HK");
  locale loc_fr("fr_FR@euro");
  locale loc_de("de_DE");
  VERIFY( loc_hk != loc_c );
  VERIFY( loc_hk != loc_fr );
  VERIFY( loc_hk != loc_de );
  VERIFY( loc_de != loc_fr );
  // cache the __timepunct facets, for quicker gdb inspection
  const __timepunct<wchar_t>& time_c = use_facet<__timepunct<wchar_t> >(loc_c); 
  const __timepunct<wchar_t>& time_de = use_facet<__timepunct<wchar_t> >(loc_de); 
  const __timepunct<wchar_t>& time_hk = use_facet<__timepunct<wchar_t> >(loc_hk); 
  const __timepunct<wchar_t>& time_fr = use_facet<__timepunct<wchar_t> >(loc_fr); 

  const wstring empty;

  // create an ostream-derived object, cache the time_get facet
  iterator_type end;

  wistringstream iss;
  const time_get<wchar_t>& tim_get = use_facet<time_get<wchar_t> >(iss.getloc()); 

  const ios_base::iostate good = ios_base::goodbit;
  ios_base::iostate errorstate = good;

  // create "C" time objects
  const tm time_bday = { 0, 0, 12, 4, 3, 71 };
  const wchar_t* all = L"%a %A %b %B %c %d %H %I %j %m %M %p %s %U "
                    L"%w %W %x %X %y %Y %Z %%";
  const wchar_t* date = L"%A, the second of %B";
  const wchar_t* date_ex = L"%Ex";

  // 1
  // dateorder date_order() const
  iss.imbue(loc_c);
  dateorder do1 = tim_get.date_order();
  //  VERIFY( do1 == time_base::mdy );
  VERIFY( do1 == time_base::no_order );

  // 2
  // iter_type 
  // get_time(iter_type, iter_type, ios_base&, ios_base::iostate&, tm*) const

  // sanity checks for "C" locale
  iss.str(L"12:00:00");
  iterator_type is_it01(iss);
  tm time01;
  errorstate = good;
  tim_get.get_time(is_it01, end, iss, errorstate, &time01);
  VERIFY( time01.tm_sec == time_bday.tm_sec );
  VERIFY( time01.tm_min == time_bday.tm_min );
  VERIFY( time01.tm_hour == time_bday.tm_hour );
  VERIFY( errorstate == ios_base::eofbit );

  iss.str(L"12:00:00 ");
  iterator_type is_it02(iss);
  tm time02;
  errorstate = good;
  tim_get.get_time(is_it02, end, iss, errorstate, &time02);
  VERIFY( time01.tm_sec == time_bday.tm_sec );
  VERIFY( time01.tm_min == time_bday.tm_min );
  VERIFY( time01.tm_hour == time_bday.tm_hour );
  VERIFY( errorstate == good );

  iss.str(L"12:61:00 ");
  iterator_type is_it03(iss);
  tm time03;
  errorstate = good;
  tim_get.get_time(is_it03, end, iss, errorstate, &time03);
  VERIFY( time01.tm_hour == time_bday.tm_hour );
  VERIFY( errorstate == ios_base::failbit );

  iss.str(L"12:a:00 ");
  iterator_type is_it04(iss);
  tm time04;
  errorstate = good;
  tim_get.get_time(is_it04, end, iss, errorstate, &time04);
  VERIFY( time01.tm_hour == time_bday.tm_hour );
  VERIFY( *is_it04 == 'a');
  VERIFY( errorstate == ios_base::failbit );

  // inspection of named locales, de_DE
  iss.imbue(loc_de);
  iss.str(L"12:00:00");
  iterator_type is_it10(iss);
  tm time10;
  errorstate = good;
  tim_get.get_time(is_it10, end, iss, errorstate, &time10);
  VERIFY( time10.tm_sec == time_bday.tm_sec );
  VERIFY( time10.tm_min == time_bday.tm_min );
  VERIFY( time10.tm_hour == time_bday.tm_hour );
  VERIFY( errorstate == ios_base::eofbit );

  // inspection of named locales, en_HK
  iss.imbue(loc_hk);
  iss.str(L"12:00:00 PST"); 
  // Hong Kong in California! Well, they have Paris in Vegas... this
  // is all a little disney-esque anyway. Besides, you can get decent
  // Dim Sum in San Francisco.
  iterator_type is_it20(iss);
  tm time20;
  errorstate = good;
  tim_get.get_time(is_it20, end, iss, errorstate, &time20);
  VERIFY( time10.tm_sec == time_bday.tm_sec );
  VERIFY( time10.tm_min == time_bday.tm_min );
  VERIFY( time10.tm_hour == time_bday.tm_hour );
  VERIFY( errorstate == ios_base::eofbit );
}

void test02()
{
  using namespace std;
  typedef time_base::dateorder dateorder;
  typedef istreambuf_iterator<wchar_t> iterator_type;

  bool test = true;

  // basic construction and sanity checks.
  locale loc_c = locale::classic();
  locale loc_hk("en_HK");
  locale loc_fr("fr_FR@euro");
  locale loc_de("de_DE");
  VERIFY( loc_hk != loc_c );
  VERIFY( loc_hk != loc_fr );
  VERIFY( loc_hk != loc_de );
  VERIFY( loc_de != loc_fr );

  // cache the __timepunct facets, for quicker gdb inspection
  const __timepunct<wchar_t>& time_c = use_facet<__timepunct<wchar_t> >(loc_c); 
  const __timepunct<wchar_t>& time_de = use_facet<__timepunct<wchar_t> >(loc_de); 
  const __timepunct<wchar_t>& time_hk = use_facet<__timepunct<wchar_t> >(loc_hk); 
  const __timepunct<wchar_t>& time_fr = use_facet<__timepunct<wchar_t> >(loc_fr); 

  const wstring empty;

  // create an ostream-derived object, cache the time_get facet
  iterator_type end;

  wistringstream iss;
  const time_get<wchar_t>& tim_get = use_facet<time_get<wchar_t> >(iss.getloc()); 

  const ios_base::iostate good = ios_base::goodbit;
  ios_base::iostate errorstate = good;

  // create "C" time objects
  const tm time_bday = { 0, 0, 12, 4, 3, 71 };
  const wchar_t* all = L"%a %A %b %B %c %d %H %I %j %m %M %p %s %U "
                    L"%w %W %x %X %y %Y %Z %%";
  const wchar_t* date = L"%A, the second of %B";
  const wchar_t* date_ex = L"%Ex";

  // iter_type 
  // get_weekday(iter_type, iter_type, ios_base&, 
  //             ios_base::iostate&, tm*) const

  // sanity checks for "C" locale
  iss.imbue(loc_c);
  iss.str(L"Sunday");
  iterator_type is_it01(iss);
  tm time01;
  errorstate = good;
  tim_get.get_weekday(is_it01, end, iss, errorstate, &time01);
  VERIFY( time01.tm_wday == time_bday.tm_wday );
  VERIFY( errorstate == ios_base::eofbit );

  iss.str(L"Sun");
  iterator_type is_it02(iss);
  tm time02;
  errorstate = good;
  tim_get.get_weekday(is_it02, end, iss, errorstate, &time02);
  VERIFY( time02.tm_wday == time_bday.tm_wday );
  VERIFY( errorstate == ios_base::eofbit );

  iss.str(L"Sun ");
  iterator_type is_it03(iss);
  tm time03;
  errorstate = good;
  tim_get.get_weekday(is_it03, end, iss, errorstate, &time03);
  VERIFY( time03.tm_wday == time_bday.tm_wday );
  VERIFY( errorstate == good );
  VERIFY( *is_it03 == ' ');

  iss.str(L"San");
  iterator_type is_it04(iss);
  tm time04;
  time04.tm_wday = 4;
  errorstate = good;
  tim_get.get_weekday(is_it04, end, iss, errorstate, &time04);
  VERIFY( time04.tm_wday == 4 );
  VERIFY( *is_it04 == 'n');
  VERIFY( errorstate == ios_base::failbit );

  iss.str(L"Tuesday ");
  iterator_type is_it05(iss);
  tm time05;
  errorstate = good;
  tim_get.get_weekday(is_it05, end, iss, errorstate, &time05);
  VERIFY( time05.tm_wday == 2 );
  VERIFY( errorstate == good );
  VERIFY( *is_it05 == ' ');

  iss.str(L"Tuesducky "); // Kind of like Fryday, without the swirls.
  iterator_type is_it06(iss);
  tm time06;
  time06.tm_wday = 4;
  errorstate = good;
  tim_get.get_weekday(is_it06, end, iss, errorstate, &time06);
  VERIFY( time06.tm_wday == 4 );
  VERIFY( errorstate == ios_base::failbit );
  VERIFY( *is_it05 == 'u');

  // inspection of named locales, de_DE
  iss.imbue(loc_de);
  iss.str(L"Sonntag");
  iterator_type is_it10(iss);
  tm time10;
  errorstate = good;
  tim_get.get_weekday(is_it10, end, iss, errorstate, &time10);
  VERIFY( time10.tm_wday == time_bday.tm_wday );
  VERIFY( errorstate == ios_base::eofbit );

  // inspection of named locales, en_HK
  iss.imbue(loc_hk);
  iss.str(L"Sunday"); 
  iterator_type is_it20(iss);
  tm time20;
  errorstate = good;
  tim_get.get_weekday(is_it20, end, iss, errorstate, &time20);
  VERIFY( time20.tm_wday == time_bday.tm_wday );
  VERIFY( errorstate == ios_base::eofbit );
}

void test03()
{
  using namespace std;
  typedef time_base::dateorder dateorder;
  typedef istreambuf_iterator<wchar_t> iterator_type;

  bool test = true;

  // basic construction and sanity checks.
  locale loc_c = locale::classic();
  locale loc_hk("en_HK");
  locale loc_fr("fr_FR@euro");
  locale loc_de("de_DE");
  VERIFY( loc_hk != loc_c );
  VERIFY( loc_hk != loc_fr );
  VERIFY( loc_hk != loc_de );
  VERIFY( loc_de != loc_fr );

  // cache the __timepunct facets, for quicker gdb inspection
  const __timepunct<wchar_t>& time_c = use_facet<__timepunct<wchar_t> >(loc_c); 
  const __timepunct<wchar_t>& time_de = use_facet<__timepunct<wchar_t> >(loc_de); 
  const __timepunct<wchar_t>& time_hk = use_facet<__timepunct<wchar_t> >(loc_hk); 
  const __timepunct<wchar_t>& time_fr = use_facet<__timepunct<wchar_t> >(loc_fr); 

  const wstring empty;

  // create an ostream-derived object, cache the time_get facet
  iterator_type end;

  wistringstream iss;
  const time_get<wchar_t>& tim_get = use_facet<time_get<wchar_t> >(iss.getloc()); 

  const ios_base::iostate good = ios_base::goodbit;
  ios_base::iostate errorstate = good;

  // create "C" time objects
  const tm time_bday = { 0, 0, 12, 4, 3, 71 };
  const wchar_t* all = L"%a %A %b %B %c %d %H %I %j %m %M %p %s %U "
                    L"%w %W %x %X %y %Y %Z %%";
  const wchar_t* date = L"%A, the second of %B";
  const wchar_t* date_ex = L"%Ex";

  // iter_type 
  // get_monthname(iter_type, iter_type, ios_base&, 
  //               ios_base::iostate&, tm*) const

  // sanity checks for "C" locale
  iss.imbue(loc_c);
  iss.str(L"April");
  iterator_type is_it01(iss);
  tm time01;
  errorstate = good;
  tim_get.get_monthname(is_it01, end, iss, errorstate, &time01);
  VERIFY( time01.tm_wday == time_bday.tm_wday );
  VERIFY( errorstate == ios_base::eofbit );

  iss.str(L"Apr");
  iterator_type is_it02(iss);
  tm time02;
  errorstate = good;
  tim_get.get_monthname(is_it02, end, iss, errorstate, &time02);
  VERIFY( time02.tm_mon == time_bday.tm_mon );
  VERIFY( errorstate == ios_base::eofbit );

  iss.str(L"Apr ");
  iterator_type is_it03(iss);
  tm time03;
  errorstate = good;
  tim_get.get_monthname(is_it03, end, iss, errorstate, &time03);
  VERIFY( time03.tm_mon == time_bday.tm_mon );
  VERIFY( errorstate == good );
  VERIFY( *is_it03 == ' ');

  iss.str(L"Aar");
  iterator_type is_it04(iss);
  tm time04;
  time04.tm_mon = 5;
  errorstate = good;
  tim_get.get_monthname(is_it04, end, iss, errorstate, &time04);
  VERIFY( time04.tm_mon == 5 );
  VERIFY( *is_it04 == 'a');
  VERIFY( errorstate == ios_base::failbit );

  iss.str(L"December ");
  iterator_type is_it05(iss);
  tm time05;
  errorstate = good;
  tim_get.get_monthname(is_it05, end, iss, errorstate, &time05);
  VERIFY( time05.tm_mon == 11 );
  VERIFY( errorstate == good );
  VERIFY( *is_it05 == ' ');

  iss.str(L"Decelember "); 
  iterator_type is_it06(iss);
  tm time06;
  time06.tm_mon = 4;
  errorstate = good;
  tim_get.get_monthname(is_it06, end, iss, errorstate, &time06);
  VERIFY( time06.tm_mon == 4 );
  VERIFY( errorstate == ios_base::failbit );
  VERIFY( *is_it05 == 'l');

  // inspection of named locales, de_DE
  iss.imbue(loc_de);
  iss.str(L"April");
  iterator_type is_it10(iss);
  tm time10;
  errorstate = good;
  tim_get.get_monthname(is_it10, end, iss, errorstate, &time10);
  VERIFY( time10.tm_mon == time_bday.tm_mon );
  VERIFY( errorstate == ios_base::eofbit );

  // inspection of named locales, en_HK
  iss.imbue(loc_hk);
  iss.str(L"April"); 
  iterator_type is_it20(iss);
  tm time20;
  errorstate = good;
  tim_get.get_monthname(is_it20, end, iss, errorstate, &time20);
  VERIFY( time20.tm_mon == time_bday.tm_mon );
  VERIFY( errorstate == ios_base::eofbit );
}

void test04()
{
  using namespace std;
  typedef time_base::dateorder dateorder;
  typedef istreambuf_iterator<wchar_t> iterator_type;

  bool test = true;

  // basic construction and sanity checks.
  locale loc_c = locale::classic();
  locale loc_hk("en_HK");
  locale loc_fr("fr_FR@euro");
  locale loc_de("de_DE");
  VERIFY( loc_hk != loc_c );
  VERIFY( loc_hk != loc_fr );
  VERIFY( loc_hk != loc_de );
  VERIFY( loc_de != loc_fr );

  // cache the __timepunct facets, for quicker gdb inspection
  const __timepunct<wchar_t>& time_c = use_facet<__timepunct<wchar_t> >(loc_c); 
  const __timepunct<wchar_t>& time_de = use_facet<__timepunct<wchar_t> >(loc_de); 
  const __timepunct<wchar_t>& time_hk = use_facet<__timepunct<wchar_t> >(loc_hk); 
  const __timepunct<wchar_t>& time_fr = use_facet<__timepunct<wchar_t> >(loc_fr); 

  const wstring empty;

  // create an ostream-derived object, cache the time_get facet
  iterator_type end;

  wistringstream iss;
  const time_get<wchar_t>& tim_get = use_facet<time_get<wchar_t> >(iss.getloc()); 

  const ios_base::iostate good = ios_base::goodbit;
  ios_base::iostate errorstate = good;

  // create "C" time objects
  const tm time_bday = { 0, 0, 12, 4, 3, 71 };
  const wchar_t* all = L"%a %A %b %B %c %d %H %I %j %m %M %p %s %U "
                    L"%w %W %x %X %y %Y %Z %%";
  const wchar_t* date = L"%A, the second of %B";
  const wchar_t* date_ex = L"%Ex";

  // iter_type 
  // get_year(iter_type, iter_type, ios_base&, ios_base::iostate&, tm*) const

  // sanity checks for "C" locale
  iss.imbue(loc_c);
  iss.str(L"1971");
  iterator_type is_it01(iss);
  tm time01;
  errorstate = good;
  tim_get.get_year(is_it01, end, iss, errorstate, &time01);
  VERIFY( time01.tm_year == time_bday.tm_year );
  VERIFY( errorstate == ios_base::eofbit );

  iss.str(L"1971 ");
  iterator_type is_it02(iss);
  tm time02;
  errorstate = good;
  tim_get.get_year(is_it02, end, iss, errorstate, &time02);
  VERIFY( time02.tm_year == time_bday.tm_year );
  VERIFY( errorstate == good );
  VERIFY( *is_it02 == ' ');

  iss.str(L"197d1 ");
  iterator_type is_it03(iss);
  tm time03;
  time03.tm_year = 3;
  errorstate = good;
  tim_get.get_year(is_it03, end, iss, errorstate, &time03);
  VERIFY( time03.tm_year == 3 );
  VERIFY( errorstate == ios_base::failbit );
  VERIFY( *is_it03 == 'd');

  iss.str(L"71d71");
  iterator_type is_it04(iss);
  tm time04;
  errorstate = good;
  tim_get.get_year(is_it04, end, iss, errorstate, &time04);
  VERIFY( time04.tm_year == time_bday.tm_year );
  VERIFY( errorstate == good );
  VERIFY( *is_it03 == 'd');

  iss.str(L"71");
  iterator_type is_it05(iss);
  tm time05;
  errorstate = good;
  tim_get.get_year(is_it05, end, iss, errorstate, &time05);
  VERIFY( time05.tm_year == time_bday.tm_year );
  VERIFY( errorstate == ios_base::eofbit );
}

void test05()
{
  using namespace std;
  typedef time_base::dateorder dateorder;
  typedef istreambuf_iterator<wchar_t> iterator_type;

  bool test = true;

  // basic construction and sanity checks.
  locale loc_c = locale::classic();
  locale loc_hk("en_HK");
  locale loc_fr("fr_FR@euro");
  locale loc_de("de_DE");
  VERIFY( loc_hk != loc_c );
  VERIFY( loc_hk != loc_fr );
  VERIFY( loc_hk != loc_de );
  VERIFY( loc_de != loc_fr );

  // cache the __timepunct facets, for quicker gdb inspection
  const __timepunct<wchar_t>& time_c = use_facet<__timepunct<wchar_t> >(loc_c); 
  const __timepunct<wchar_t>& time_de = use_facet<__timepunct<wchar_t> >(loc_de); 
  const __timepunct<wchar_t>& time_hk = use_facet<__timepunct<wchar_t> >(loc_hk); 
  const __timepunct<wchar_t>& time_fr = use_facet<__timepunct<wchar_t> >(loc_fr); 

  const wstring empty;

  // create an ostream-derived object, cache the time_get facet
  iterator_type end;

  wistringstream iss;
  const time_get<wchar_t>& tim_get = use_facet<time_get<wchar_t> >(iss.getloc()); 

  const ios_base::iostate good = ios_base::goodbit;
  ios_base::iostate errorstate = good;

  // create "C" time objects
  const tm time_bday = { 0, 0, 12, 4, 3, 71 };
  const wchar_t* all = L"%a %A %b %B %c %d %H %I %j %m %M %p %s %U "
                    L"%w %W %x %X %y %Y %Z %%";
  const wchar_t* date = L"%A, the second of %B";
  const wchar_t* date_ex = L"%Ex";

  // iter_type 
  // get_date(iter_type, iter_type, ios_base&, ios_base::iostate&, tm*) const

  // sanity checks for "C" locale
  iss.imbue(loc_c);
  iss.str(L"04/04/71");
  iterator_type is_it01(iss);
  tm time01;
  errorstate = good;
  tim_get.get_date(is_it01, end, iss, errorstate, &time01);
  VERIFY( time01.tm_year == time_bday.tm_year );
  VERIFY( time01.tm_mon == time_bday.tm_mon );
  VERIFY( time01.tm_mday == time_bday.tm_mday );
  VERIFY( errorstate == ios_base::eofbit );

  iss.str(L"04/04/71 ");
  iterator_type is_it02(iss);
  tm time02;
  errorstate = good;
  tim_get.get_date(is_it02, end, iss, errorstate, &time02);
  VERIFY( time02.tm_year == time_bday.tm_year );
  VERIFY( time02.tm_mon == time_bday.tm_mon );
  VERIFY( time02.tm_mday == time_bday.tm_mday );
  VERIFY( errorstate == good );
  VERIFY( *is_it02 == ' ');

  iss.str(L"04/04d/71 ");
  iterator_type is_it03(iss);
  tm time03;
  time03.tm_year = 3;
  errorstate = good;
  tim_get.get_date(is_it03, end, iss, errorstate, &time03);
  VERIFY( time03.tm_year == 3 );
  VERIFY( time03.tm_mon == time_bday.tm_mon );
  VERIFY( time03.tm_mday == time_bday.tm_mday );
  VERIFY( errorstate == ios_base::failbit );
  VERIFY( *is_it03 == 'd');

  // inspection of named locales, de_DE
  iss.imbue(loc_de);
  iss.str(L"04.04.1971");
  iterator_type is_it10(iss);
  tm time10;
  errorstate = good;
  tim_get.get_date(is_it10, end, iss, errorstate, &time10);
  VERIFY( time10.tm_mon == time_bday.tm_mon );
  VERIFY( time10.tm_mday == time_bday.tm_mday );
  VERIFY( time10.tm_year == time_bday.tm_year );
  VERIFY( errorstate == ios_base::eofbit );

  // inspection of named locales, en_HK
  iss.imbue(loc_hk);
  iss.str(L"Sunday, April 04, 1971"); 
  iterator_type is_it20(iss);
  tm time20;
  errorstate = good;
  tim_get.get_date(is_it20, end, iss, errorstate, &time20);
  VERIFY( time20.tm_mon == time_bday.tm_mon );
  VERIFY( time20.tm_mday == time_bday.tm_mday );
  VERIFY( time20.tm_year == time_bday.tm_year );
  VERIFY( errorstate == ios_base::eofbit );
}

void test06()
{
  using namespace std;
  bool test = true;

  // Check time_get works with other iterators besides streambuf
  // input iterators.
  typedef wstring::const_iterator iter_type;
  typedef time_get<wchar_t, iter_type> time_get_type;
  const ios_base::iostate goodbit = ios_base::goodbit;
  const ios_base::iostate eofbit = ios_base::eofbit;
  ios_base::iostate err = goodbit;
  const locale loc_c = locale::classic();
  // Cindy Sherman's Untitled Film Stills
  // June 26-September 2, 1997
  const wstring str = L"12:00:00 06/26/97 Tuesday September 1997 Cindy Sherman";
 
  // Create "C" time objects
  const tm time_sanity = { 0, 0, 12, 26, 5, 97, 2 };
  tm tm1;

  wistringstream iss; 
  iss.imbue(locale(loc_c, new time_get_type));

  // Iterator advanced, state, output.
  const time_get_type& tg = use_facet<time_get_type>(iss.getloc());

  // 01 get_time
  // 02 get_date
  // 03 get_weekday
  // 04 get_monthname
  // 05 get_year

  // 01 get_time
  wstring res1;
  err = goodbit;
  iter_type end1 = tg.get_time(str.begin(), str.end(), iss, err, &tm1);
  wstring rem1(end1, str.end());
  VERIFY( err == goodbit );
  VERIFY( tm1.tm_sec == time_sanity.tm_sec );
  VERIFY( tm1.tm_min == time_sanity.tm_min );
  VERIFY( tm1.tm_hour == time_sanity.tm_hour );
  VERIFY( rem1 ==  L" 06/26/97 Tuesday September 1997 Cindy Sherman" );

  // 02 get_date
  wstring res2;
  err = goodbit;
  // White space is not eaten, so manually increment past it.
  iter_type end2 = tg.get_date(++end1, str.end(), iss, err, &tm1);
  wstring rem2(end2, str.end());
  VERIFY( err == goodbit );
  VERIFY( tm1.tm_year == time_sanity.tm_year );
  VERIFY( tm1.tm_mon == time_sanity.tm_mon );
  VERIFY( tm1.tm_mday == time_sanity.tm_mday );
  VERIFY( rem2 ==  L" Tuesday September 1997 Cindy Sherman" );

  // 03 get_weekday
  wstring res3;
  err = goodbit;
  // White space is not eaten, so manually increment past it.
  iter_type end3 = tg.get_weekday(++end2, str.end(), iss, err, &tm1);
  wstring rem3(end3, str.end());
  VERIFY( err == goodbit );
  VERIFY( tm1.tm_wday == time_sanity.tm_wday );
  VERIFY( rem3 ==  L" September 1997 Cindy Sherman" );

  // 04 get_monthname
  wstring res4;
  err = goodbit;
  // White space is not eaten, so manually increment past it.
  iter_type end4 = tg.get_monthname(++end3, str.end(), iss, err, &tm1);
  wstring rem4(end4, str.end());
  VERIFY( err == goodbit );
  VERIFY( tm1.tm_mon == 8 );
  VERIFY( rem4 ==  L" 1997 Cindy Sherman" );

  // 05 get_year
  wstring res5;
  err = goodbit;
  // White space is not eaten, so manually increment past it.
  iter_type end5 = tg.get_year(++end4, str.end(), iss, err, &tm1);
  wstring rem5(end5, str.end());
  VERIFY( err == goodbit );
  VERIFY( tm1.tm_year == time_sanity.tm_year );
  VERIFY( rem5 ==  L" Cindy Sherman" );
}

// libstdc++/5280
void test07()
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
      test03();
      test04();
      test05();
      test06();
      setenv("LANG", oldLANG ? oldLANG : "", 1);
    }
#endif
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
      test04();
      test05();
      test06();
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
