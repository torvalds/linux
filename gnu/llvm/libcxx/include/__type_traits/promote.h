//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_PROMOTE_H
#define _LIBCPP___TYPE_TRAITS_PROMOTE_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_arithmetic.h>

#if defined(_LIBCPP_CLANG_VER) && _LIBCPP_CLANG_VER == 1700
#  include <__type_traits/is_same.h>
#  include <__utility/declval.h>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// TODO(LLVM-20): Remove this workaround
#if !defined(_LIBCPP_CLANG_VER) || _LIBCPP_CLANG_VER != 1700

template <class... _Args>
class __promote {
  static_assert((is_arithmetic<_Args>::value && ...));

  static float __test(float);
  static double __test(char);
  static double __test(int);
  static double __test(unsigned);
  static double __test(long);
  static double __test(unsigned long);
  static double __test(long long);
  static double __test(unsigned long long);
#  ifndef _LIBCPP_HAS_NO_INT128
  static double __test(__int128_t);
  static double __test(__uint128_t);
#  endif
  static double __test(double);
  static long double __test(long double);

public:
  using type = decltype((__test(_Args()) + ...));
};

#else

template <class _Tp>
struct __numeric_type {
  static void __test(...);
  static float __test(float);
  static double __test(char);
  static double __test(int);
  static double __test(unsigned);
  static double __test(long);
  static double __test(unsigned long);
  static double __test(long long);
  static double __test(unsigned long long);
#  ifndef _LIBCPP_HAS_NO_INT128
  static double __test(__int128_t);
  static double __test(__uint128_t);
#  endif
  static double __test(double);
  static long double __test(long double);

  typedef decltype(__test(std::declval<_Tp>())) type;
  static const bool value = _IsNotSame<type, void>::value;
};

template <>
struct __numeric_type<void> {
  static const bool value = true;
};

template <class _A1,
          class _A2 = void,
          class _A3 = void,
          bool      = __numeric_type<_A1>::value && __numeric_type<_A2>::value && __numeric_type<_A3>::value>
class __promote_imp {
public:
  static const bool value = false;
};

template <class _A1, class _A2, class _A3>
class __promote_imp<_A1, _A2, _A3, true> {
private:
  typedef typename __promote_imp<_A1>::type __type1;
  typedef typename __promote_imp<_A2>::type __type2;
  typedef typename __promote_imp<_A3>::type __type3;

public:
  typedef decltype(__type1() + __type2() + __type3()) type;
  static const bool value = true;
};

template <class _A1, class _A2>
class __promote_imp<_A1, _A2, void, true> {
private:
  typedef typename __promote_imp<_A1>::type __type1;
  typedef typename __promote_imp<_A2>::type __type2;

public:
  typedef decltype(__type1() + __type2()) type;
  static const bool value = true;
};

template <class _A1>
class __promote_imp<_A1, void, void, true> {
public:
  typedef typename __numeric_type<_A1>::type type;
  static const bool value = true;
};

template <class _A1, class _A2 = void, class _A3 = void>
class __promote : public __promote_imp<_A1, _A2, _A3> {};

#endif // !defined(_LIBCPP_CLANG_VER) || _LIBCPP_CLANG_VER >= 1700

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_PROMOTE_H
