// 1999-11-15 Kevin Ediger  <kediger@licor.com>
// test the floating point inserters (facet num_put)

// Copyright (C) 1999, 2002 Free Software Foundation, Inc.
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

#include <cstdio> // for sprintf
#include <cmath> // for abs
#include <cfloat> // for DBL_EPSILON
#include <iostream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <limits>
#include <testsuite_hooks.h>

using namespace std;

#ifndef DEBUG_ASSERT
#  define TEST_NUMPUT_VERBOSE 1
#endif

struct _TestCase
{
  double val;
    
  int precision;
  int width;
  char decimal;
  char fill;

  bool fixed;
  bool scientific;
  bool showpos;
  bool showpoint;
  bool uppercase;
  bool internal;
  bool left;
  bool right;

  const char* result;
#if defined(_GLIBCPP_USE_WCHAR_T)
  const wchar_t* wresult;
#endif
};

static bool T=true;
static bool F=false;

static _TestCase testcases[] =
{
#if defined(_GLIBCPP_USE_WCHAR_T)
  // standard output (no formatting applied) 1-4
  { 1.2, 6,0,'.',' ', F,F,F,F,F,F,F,F, "1.2",L"1.2" },
  { 54, 6,0,'.',' ', F,F,F,F,F,F,F,F, "54",L"54" },
  { -.012, 6,0,'.',' ', F,F,F,F,F,F,F,F, "-0.012",L"-0.012" },
  { -.00000012, 6,0,'.',' ', F,F,F,F,F,F,F,F, "-1.2e-07",L"-1.2e-07" },
    
  // fixed formatting 5-11
  { 10.2345, 0,0,'.',' ', T,F,F,F,F,F,F,F, "10",L"10" },
  { 10.2345, 0,0,'.',' ', T,F,F,T,F,F,F,F, "10.",L"10." },
  { 10.2345, 1,0,'.',' ', T,F,F,F,F,F,F,F, "10.2",L"10.2" },
  { 10.2345, 4,0,'.',' ', T,F,F,F,F,F,F,F, "10.2345",L"10.2345" },
  { 10.2345, 6,0,'.',' ', T,F,T,F,F,F,F,F, "+10.234500",L"+10.234500" },
  { -10.2345, 6,0,'.',' ', T,F,F,F,F,F,F,F, "-10.234500",L"-10.234500" },
  { -10.2345, 6,0,',',' ', T,F,F,F,F,F,F,F, "-10,234500",L"-10,234500" },

  // fixed formatting with width 12-22
  { 10.2345, 4,5,'.',' ', T,F,F,F,F,F,F,F, "10.2345",L"10.2345" },
  { 10.2345, 4,6,'.',' ', T,F,F,F,F,F,F,F, "10.2345",L"10.2345" },
  { 10.2345, 4,7,'.',' ', T,F,F,F,F,F,F,F, "10.2345",L"10.2345" },
  { 10.2345, 4,8,'.',' ', T,F,F,F,F,F,F,F, " 10.2345",L" 10.2345" },
  { 10.2345, 4,10,'.',' ', T,F,F,F,F,F,F,F, "   10.2345",L"   10.2345" },
  { 10.2345, 4,10,'.',' ', T,F,F,F,F,F,T,F, "10.2345   ",L"10.2345   " },
  { 10.2345, 4,10,'.',' ', T,F,F,F,F,F,F,T, "   10.2345",L"   10.2345" },
  { 10.2345, 4,10,'.',' ', T,F,F,F,F,T,F,F, "   10.2345",L"   10.2345" },
  { -10.2345, 4,10,'.',' ', T,F,F,F,F,T,F,F, "-  10.2345",L"-  10.2345" },
  { -10.2345, 4,10,'.','A', T,F,F,F,F,T,F,F, "-AA10.2345",L"-AA10.2345" },
  { 10.2345, 4,10,'.','#', T,F,T,F,F,T,F,F, "+##10.2345",L"+##10.2345" },

  // scientific formatting 23-29
  { 1.23e+12, 1,0,'.',' ', F,T,F,F,F,F,F,F, "1.2e+12",L"1.2e+12" },
  { 1.23e+12, 1,0,'.',' ', F,T,F,F,T,F,F,F, "1.2E+12",L"1.2E+12" },
  { 1.23e+12, 2,0,'.',' ', F,T,F,F,F,F,F,F, "1.23e+12",L"1.23e+12" },
  { 1.23e+12, 3,0,'.',' ', F,T,F,F,F,F,F,F, "1.230e+12",L"1.230e+12" },
  { 1.23e+12, 3,0,'.',' ', F,T,T,F,F,F,F,F, "+1.230e+12",L"+1.230e+12" },
  { -1.23e-12, 3,0,'.',' ', F,T,F,F,F,F,F,F, "-1.230e-12",L"-1.230e-12" },
  { 1.23e+12, 3,0,',',' ', F,T,F,F,F,F,F,F, "1,230e+12",L"1,230e+12" },
#else
  // standard output (no formatting applied)
  { 1.2, 6,0,'.',' ', F,F,F,F,F,F,F,F, "1.2" },
  { 54, 6,0,'.',' ', F,F,F,F,F,F,F,F, "54" },
  { -.012, 6,0,'.',' ', F,F,F,F,F,F,F,F, "-0.012" },
  { -.00000012, 6,0,'.',' ', F,F,F,F,F,F,F,F, "-1.2e-07" },
    
  // fixed formatting
  { 10.2345, 0,0,'.',' ', T,F,F,F,F,F,F,F, "10" },
  { 10.2345, 0,0,'.',' ', T,F,F,T,F,F,F,F, "10." },
  { 10.2345, 1,0,'.',' ', T,F,F,F,F,F,F,F, "10.2" },
  { 10.2345, 4,0,'.',' ', T,F,F,F,F,F,F,F, "10.2345" },
  { 10.2345, 6,0,'.',' ', T,F,T,F,F,F,F,F, "+10.234500" },
  { -10.2345, 6,0,'.',' ', T,F,F,F,F,F,F,F, "-10.234500" },
  { -10.2345, 6,0,',',' ', T,F,F,F,F,F,F,F, "-10,234500" },

  // fixed formatting with width
  { 10.2345, 4,5,'.',' ', T,F,F,F,F,F,F,F, "10.2345" },
  { 10.2345, 4,6,'.',' ', T,F,F,F,F,F,F,F, "10.2345" },
  { 10.2345, 4,7,'.',' ', T,F,F,F,F,F,F,F, "10.2345" },
  { 10.2345, 4,8,'.',' ', T,F,F,F,F,F,F,F, " 10.2345" },
  { 10.2345, 4,10,'.',' ', T,F,F,F,F,F,F,F, "   10.2345" },
  { 10.2345, 4,10,'.',' ', T,F,F,F,F,F,T,F, "10.2345   " },
  { 10.2345, 4,10,'.',' ', T,F,F,F,F,F,F,T, "   10.2345" },
  { 10.2345, 4,10,'.',' ', T,F,F,F,F,T,F,F, "   10.2345" },
  { -10.2345, 4,10,'.',' ', T,F,F,F,F,T,F,F, "-  10.2345" },
  { -10.2345, 4,10,'.','A', T,F,F,F,F,T,F,F, "-AA10.2345" },
  { 10.2345, 4,10,'.','#', T,F,T,F,F,T,F,F, "+##10.2345" },

  // scientific formatting
  { 1.23e+12, 1,0,'.',' ', F,T,F,F,F,F,F,F, "1.2e+12" },
  { 1.23e+12, 1,0,'.',' ', F,T,F,F,T,F,F,F, "1.2E+12" },
  { 1.23e+12, 2,0,'.',' ', F,T,F,F,F,F,F,F, "1.23e+12" },
  { 1.23e+12, 3,0,'.',' ', F,T,F,F,F,F,F,F, "1.230e+12" },
  { 1.23e+12, 3,0,'.',' ', F,T,T,F,F,F,F,F, "+1.230e+12" },
  { -1.23e-12, 3,0,'.',' ', F,T,F,F,F,F,F,F, "-1.230e-12" },
  { 1.23e+12, 3,0,',',' ', F,T,F,F,F,F,F,F, "1,230e+12" },
#endif
};

template<typename _CharT>
class testpunct : public numpunct<_CharT>
{
public:
  typedef _CharT  char_type;
  const char_type dchar;

  explicit
  testpunct(char_type decimal_char) : numpunct<_CharT>(), dchar(decimal_char)
  { }

protected:
  char_type 
  do_decimal_point() const
  { return dchar; }
    
  char_type 
  do_thousands_sep() const
  { return ','; }

  string 
  do_grouping() const
  { return string(); }
};
 
template<typename _CharT>  
void apply_formatting(const _TestCase & tc, basic_ostream<_CharT> & os)
{
  os.precision(tc.precision);
  os.width(tc.width);
  os.fill(static_cast<_CharT>(tc.fill));
  if (tc.fixed)
    os.setf(ios::fixed);
  if (tc.scientific)
    os.setf(ios::scientific);
  if (tc.showpos)
    os.setf(ios::showpos);
  if (tc.showpoint)
    os.setf(ios::showpoint);
  if (tc.uppercase)
    os.setf(ios::uppercase);
  if (tc.internal)
    os.setf(ios::internal);
  if (tc.left)
    os.setf(ios::left);
  if (tc.right)
    os.setf(ios::right);
}

int
test01()
{
  bool test = true;
  for (int j=0; j<sizeof(testcases)/sizeof(testcases[0]); j++)
    {
      _TestCase & tc = testcases[j];
#ifdef TEST_NUMPUT_VERBOSE
      cout << "expect: " << tc.result << endl;
#endif
      // test double with char type
      {
        testpunct<char>* __tp = new testpunct<char>(tc.decimal);
        ostringstream os;
        locale __loc(os.getloc(), __tp);
        os.imbue(__loc);
        apply_formatting(tc, os);
        os << tc.val;
#ifdef TEST_NUMPUT_VERBOSE
        cout << j << "result 1: " << os.str() << endl;
#endif
        VERIFY( os && os.str() == tc.result );
      }
      // test long double with char type
      {
        testpunct<char>* __tp = new testpunct<char>(tc.decimal);
        ostringstream os;
        locale __loc(os.getloc(), __tp);
        os.imbue(__loc);
        apply_formatting(tc, os);
        os << (long double)tc.val;
#ifdef TEST_NUMPUT_VERBOSE
        cout << j << "result 2: " << os.str() << endl;
#endif
        VERIFY( os && os.str() == tc.result );
      }
#if defined(_GLIBCPP_USE_WCHAR_T)
      // test double with wchar_t type
      {
        testpunct<wchar_t>* __tp = new testpunct<wchar_t>(tc.decimal);
        wostringstream os;
        locale __loc(os.getloc(), __tp);
        os.imbue(__loc);
        apply_formatting(tc, os);
        os << tc.val;
        VERIFY( os && os.str() == tc.wresult );
      }
      // test long double with wchar_t type
      {
        testpunct<wchar_t>* __tp = new testpunct<wchar_t>(tc.decimal);
        wostringstream os;
        locale __loc(os.getloc(), __tp);
        os.imbue(__loc);
        apply_formatting(tc, os);
        os << (long double)tc.val;
        VERIFY( os && os.str() == tc.wresult );
      }
#endif
    }
    
  return 0;
}

int
test02()
{
  bool test = true;
  // make sure we can output a very long float
  long double val = 1.2345678901234567890123456789e+1000L;
  int prec = numeric_limits<long double>::digits10;

  ostringstream os;
  os.precision(prec);
  os.setf(ios::scientific);
  os << val;

  char largebuf[512];
  snprintf(largebuf, 512, "%.*Le", prec, val);
#ifdef TEST_NUMPUT_VERBOSE
  cout << "expect: " << largebuf << endl;
  cout << "result: " << os.str() << endl;
#endif
  VERIFY(os && os.str() == largebuf);

  // Make sure we can output a long float in fixed format
  // without seg-faulting (libstdc++/4402)
  double val2 = 3.5e230;

  ostringstream os2;
  os2.precision(3);
  os2.setf(ios::fixed);
  os2 << val2;

  snprintf(largebuf, 512, "%.*f", 3, val2);
#ifdef TEST_NUMPUT_VERBOSE
  cout << "expect: " << largebuf << endl;
  cout << "result: " << os2.str() << endl;
#endif
  VERIFY(os2 && os2.str() == largebuf);

  // Check it can be done in a locale with grouping on.
  locale loc2("de_DE");
  os2.imbue(loc2);
  os2 << fixed << setprecision(3) << val2 << endl;
  os2 << endl;
  os2 << fixed << setprecision(1) << val2 << endl;

  return 0;
}

template<typename T>
bool
test03_check(T n)
{
  stringbuf strbuf;
  ostream o(&strbuf);
  const char *expect;
  bool test = true;

  if (numeric_limits<T>::digits + 1 == 16)
    expect = "177777 ffff";
  else if (numeric_limits<T>::digits + 1 == 32)
    expect = "37777777777 ffffffff";
  else if (numeric_limits<T>::digits + 1 == 64)
    expect = "1777777777777777777777 ffffffffffffffff";
  else
    expect = "wow, you've got some big numbers here";

  o << oct << n << ' ' << hex << n;
  VERIFY ( strbuf.str() == expect );

  return test;
}

int 
test03()
{
  short s = -1;
  int i = -1;
  long l = -1;
  bool test = true;

  test &= test03_check (s);
  test &= test03_check (i);
  test &= test03_check (l);

  return 0;
}

// libstdc++/3655
int
test04()
{
  stringbuf strbuf1, strbuf2;
  ostream o1(&strbuf1), o2(&strbuf2);
  bool test = true;

  o1 << hex << showbase << setw(6) << internal << 0xff;
  VERIFY( strbuf1.str() == "0x  ff" );
  
  // ... vs internal-adjusted const char*-type objects
  o2 << hex << showbase << setw(6) << internal << "0xff";
  VERIFY( strbuf2.str() == "  0xff" );

  return 0;
}

int
test05()
{
  bool test = true;

  double pi = 3.14159265358979323846;
  ostringstream ostr;
  ostr.precision(20);
  ostr << pi;
  string sval = ostr.str();
  istringstream istr (sval);
  double d;
  istr >> d;
  VERIFY( abs(pi-d)/pi < DBL_EPSILON );
  return 0;
}


// libstdc++/9151
int
test06()
{
  bool test = true;

  int prec = numeric_limits<double>::digits10 + 2;
  double oval = numeric_limits<double>::min();

  stringstream ostr;
  ostr.precision(prec);
  ostr << oval;
  string sval = ostr.str();
  istringstream istr (sval);
  double ival;
  istr >> ival;
  VERIFY( abs(oval-ival)/oval < DBL_EPSILON ); 
  return 0;
}

int 
main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
  test06();
#ifdef TEST_NUMPUT_VERBOSE
  cout << "Test passed!" << endl;
#endif
  return 0;
}
