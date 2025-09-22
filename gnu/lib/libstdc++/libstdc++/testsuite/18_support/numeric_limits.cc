// { dg-options "-mieee" { target alpha*-*-* } }

// 1999-08-23 bkoz

// Copyright (C) 1999, 2001, 2002 Free Software Foundation
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

// 18.2.1.1 template class numeric_limits

#include <limits>
#include <limits.h>
#include <float.h>
#include <cwchar>
#include <testsuite_hooks.h>

template<typename T>
struct extrema {
  static T min;
  static T max;
};


#define DEFINE_EXTREMA(T, m, M) \
  template<> T extrema<T>::min = m; \
  template<> T extrema<T>::max = M

DEFINE_EXTREMA(char, CHAR_MIN, CHAR_MAX);
DEFINE_EXTREMA(signed char, SCHAR_MIN, SCHAR_MAX);
DEFINE_EXTREMA(unsigned char, 0, UCHAR_MAX);
DEFINE_EXTREMA(short, SHRT_MIN, SHRT_MAX);
DEFINE_EXTREMA(unsigned short, 0, USHRT_MAX);
DEFINE_EXTREMA(int, INT_MIN, INT_MAX);
DEFINE_EXTREMA(unsigned, 0U, UINT_MAX);
DEFINE_EXTREMA(long, LONG_MIN, LONG_MAX);
DEFINE_EXTREMA(unsigned long, 0UL, ULONG_MAX);

#if defined(_GLIBCPP_USE_WCHAR_T)
DEFINE_EXTREMA(wchar_t, WCHAR_MIN, WCHAR_MAX);
#endif //_GLIBCPP_USE_WCHAR_T

DEFINE_EXTREMA(float, FLT_MIN, FLT_MAX);
DEFINE_EXTREMA(double, DBL_MIN, DBL_MAX);
DEFINE_EXTREMA(long double, LDBL_MIN, LDBL_MAX);

#undef DEFINE_EXTREMA

template<typename T>
void test_extrema()
{
  bool test = true;
  T limits_min = std::numeric_limits<T>::min();
  T limits_max = std::numeric_limits<T>::max();
  T extrema_min = extrema<T>::min;
  T extrema_max = extrema<T>::max;
  VERIFY( extrema_min == limits_min );
  VERIFY( extrema_max == limits_max );
}

template<typename T>
void test_epsilon()
{
  bool test = true;
  T epsilon = std::numeric_limits<T>::epsilon();
  T one = 1;

  VERIFY( one != (one + epsilon) );
}

#ifdef __CHAR_UNSIGNED__
#define char_is_signed false
#else
#define char_is_signed true
#endif

void test_sign()
{
  bool test = true;
  VERIFY( std::numeric_limits<char>::is_signed == char_is_signed );
  VERIFY( std::numeric_limits<signed char>::is_signed == true );
  VERIFY( std::numeric_limits<unsigned char>::is_signed == false );
  VERIFY( std::numeric_limits<short>::is_signed == true );
  VERIFY( std::numeric_limits<unsigned short>::is_signed == false );
  VERIFY( std::numeric_limits<int>::is_signed == true );
  VERIFY( std::numeric_limits<unsigned>::is_signed == false );
  VERIFY( std::numeric_limits<long>::is_signed == true );
  VERIFY( std::numeric_limits<unsigned long>::is_signed == false );
  VERIFY( std::numeric_limits<float>::is_signed == true );
  VERIFY( std::numeric_limits<double>::is_signed == true );
  VERIFY( std::numeric_limits<long double>::is_signed == true );
}


template<typename T>
void
test_infinity()
{
  bool test;

  if (std::numeric_limits<T>::has_infinity)
    {
      T inf = std::numeric_limits<T>::infinity();
      test = (inf + inf == inf);
    }
  else
    test = true;

  VERIFY (test);
}

template<typename T>
void
test_denorm_min()
{
  bool test;

  if (std::numeric_limits<T>::has_denorm == std::denorm_present)
    {
      T denorm = std::numeric_limits<T>::denorm_min();
      test = (denorm > 0);
    }
  else
    test = true;

  VERIFY (test);
}

template<typename T>
void
test_qnan()
{
  bool test;

  if (std::numeric_limits<T>::has_quiet_NaN)
    {
      T nan = std::numeric_limits<T>::quiet_NaN();
      test = (nan != nan);
    }
  else
    test = true;

  VERIFY (test);
}


template<typename T>
void
test_is_iec559()
{
  bool test;

  if (std::numeric_limits<T>::is_iec559)
    {
      // IEC 559 requires all of the following.
      test = (std::numeric_limits<T>::has_infinity
	      && std::numeric_limits<T>::has_quiet_NaN
	      && std::numeric_limits<T>::has_signaling_NaN);
    }
  else
    {
      // If we had all of the following, why didn't we set IEC 559?
      test = (!std::numeric_limits<T>::has_infinity
	      || !std::numeric_limits<T>::has_quiet_NaN
	      || !std::numeric_limits<T>::has_signaling_NaN);
    }

  VERIFY (test);
}


template<typename T>
  struct A 
  {
    int key;
  public:
    A(int i = 0): key(i) { }
    bool
    operator==(int i) { return i == key; }
  };

struct B 
{
  B(int i = 0) { }
};


bool test01()
{
  bool test = true;
  std::numeric_limits< A<B> > obj;

  VERIFY( !obj.is_specialized );
  VERIFY( obj.min() == 0 );
  VERIFY( obj.max() == 0 );
  VERIFY( obj.digits ==  0 );
  VERIFY( obj.digits10 == 0 );
  VERIFY( !obj.is_signed );
  VERIFY( !obj.is_integer );
  VERIFY( !obj.is_exact );
  VERIFY( obj.radix == 0 );
  VERIFY( obj.epsilon() == 0 );
  VERIFY( obj.round_error() == 0 );
  VERIFY( obj.min_exponent == 0 );
  VERIFY( obj.min_exponent10 == 0 );
  VERIFY( obj.max_exponent == 0 );
  VERIFY( obj.max_exponent10 == 0 );
  VERIFY( !obj.has_infinity );
  VERIFY( !obj.has_quiet_NaN );
  VERIFY( !obj.has_signaling_NaN );
  VERIFY( !obj.has_denorm );
  VERIFY( !obj.has_denorm_loss );
  VERIFY( obj.infinity() == 0 );
  VERIFY( obj.quiet_NaN() == 0 );
  VERIFY( obj.signaling_NaN() == 0 );
  VERIFY( obj.denorm_min() == 0 );
  VERIFY( !obj.is_iec559 );
  VERIFY( !obj.is_bounded );
  VERIFY( !obj.is_modulo );
  VERIFY( !obj.traps );
  VERIFY( !obj.tinyness_before );
  VERIFY( obj.round_style == std::round_toward_zero );

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}

// test linkage of the generic bits
template struct std::numeric_limits<B>;

void test02()
{
  typedef std::numeric_limits<B> b_nl_type;
  
  // Should probably do all of them...
  const int* pi1 = &b_nl_type::digits;
  const int* pi2 = &b_nl_type::digits10;
  const int* pi3 = &b_nl_type::max_exponent10;
  const bool* pb1 = &b_nl_type::traps;
}

// libstdc++/5045
bool test03()
{
  bool test = true;

  VERIFY( std::numeric_limits<bool>::digits10 == 0 );
  if (__CHAR_BIT__ == 8)
    {
      VERIFY( std::numeric_limits<signed char>::digits10 == 2 );
      VERIFY( std::numeric_limits<unsigned char>::digits10 == 2 );
    }
  if (__CHAR_BIT__ * sizeof(short) == 16)
    {
      VERIFY( std::numeric_limits<signed short>::digits10 == 4 );
      VERIFY( std::numeric_limits<unsigned short>::digits10 == 4 );
    }
  if (__CHAR_BIT__ * sizeof(int) == 32)
    {
      VERIFY( std::numeric_limits<signed int>::digits10 == 9 );
      VERIFY( std::numeric_limits<unsigned int>::digits10 == 9 );
    }
  if (__CHAR_BIT__ * sizeof(long long) == 64)
    {
      VERIFY( std::numeric_limits<signed long long>::digits10 == 18 );
      VERIFY( std::numeric_limits<unsigned long long>::digits10 == 19 );
    }

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}

// libstdc++/8949
bool test04()
{
  bool test = true;

  VERIFY( !std::numeric_limits<short>::is_iec559 );
  VERIFY( !std::numeric_limits<unsigned short>::is_iec559 );
  VERIFY( !std::numeric_limits<int>::is_iec559 );
  VERIFY( !std::numeric_limits<unsigned int>::is_iec559 );
  VERIFY( !std::numeric_limits<long>::is_iec559 );
  VERIFY( !std::numeric_limits<unsigned long>::is_iec559 );
  VERIFY( !std::numeric_limits<long long>::is_iec559 );
  VERIFY( !std::numeric_limits<unsigned long long>::is_iec559 );

#ifdef DEBUG_ASSERT
  assert(test);
#endif

  return test;
}

int main()
{
  test01();
  test02();
  test03();
  test04();

  test_extrema<char>();
  test_extrema<signed char>();
  test_extrema<unsigned char>();
  
  test_extrema<short>();
  test_extrema<unsigned short>();

  test_extrema<int>();
  test_extrema<unsigned>();

  test_extrema<long>();
  test_extrema<unsigned long>();

  test_extrema<float>();
  test_extrema<double>();
  test_extrema<long double>();

  test_epsilon<float>();
  test_epsilon<double>();
  test_epsilon<long double>();

  test_sign();

  test_infinity<float>();
  test_infinity<double>();
  test_infinity<long double>();

  test_denorm_min<float>();
  test_denorm_min<double>();
  test_denorm_min<long double>();

  test_qnan<float>();
  test_qnan<double>();
  test_qnan<long double>();

  // ??? How to test SNaN?  We'd perhaps have to be prepared
  // to catch SIGFPE.  Can't rely on a signal getting through
  // since the exception can be disabled in the FPU.

  test_is_iec559<float>();
  test_is_iec559<double>();
  test_is_iec559<long double>();

  return 0;
}
