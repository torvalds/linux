// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHARCONV_TRAITS
#define _LIBCPP___CHARCONV_TRAITS

#include <__assert>
#include <__bit/countl.h>
#include <__charconv/tables.h>
#include <__charconv/to_chars_base_10.h>
#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_unsigned.h>
#include <cstdint>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

namespace __itoa {

template <typename _Tp, typename = void>
struct _LIBCPP_HIDDEN __traits_base;

template <typename _Tp>
struct _LIBCPP_HIDDEN __traits_base<_Tp, __enable_if_t<sizeof(_Tp) <= sizeof(uint32_t)>> {
  using type = uint32_t;

  /// The width estimation using a log10 algorithm.
  ///
  /// The algorithm is based on
  /// http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog10
  /// Instead of using IntegerLogBase2 it uses __libcpp_clz. Since that
  /// function requires its input to have at least one bit set the value of
  /// zero is set to one. This means the first element of the lookup table is
  /// zero.
  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI int __width(_Tp __v) {
    auto __t = (32 - std::__libcpp_clz(static_cast<type>(__v | 1))) * 1233 >> 12;
    return __t - (__v < __itoa::__pow10_32[__t]) + 1;
  }

  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI char* __convert(char* __p, _Tp __v) {
    return __itoa::__base_10_u32(__p, __v);
  }

  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI decltype(__pow10_32)& __pow() {
    return __itoa::__pow10_32;
  }
};

template <typename _Tp>
struct _LIBCPP_HIDDEN __traits_base<_Tp, __enable_if_t<sizeof(_Tp) == sizeof(uint64_t)>> {
  using type = uint64_t;

  /// The width estimation using a log10 algorithm.
  ///
  /// The algorithm is based on
  /// http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog10
  /// Instead of using IntegerLogBase2 it uses __libcpp_clz. Since that
  /// function requires its input to have at least one bit set the value of
  /// zero is set to one. This means the first element of the lookup table is
  /// zero.
  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI int __width(_Tp __v) {
    auto __t = (64 - std::__libcpp_clz(static_cast<type>(__v | 1))) * 1233 >> 12;
    return __t - (__v < __itoa::__pow10_64[__t]) + 1;
  }

  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI char* __convert(char* __p, _Tp __v) {
    return __itoa::__base_10_u64(__p, __v);
  }

  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI decltype(__pow10_64)& __pow() {
    return __itoa::__pow10_64;
  }
};

#  ifndef _LIBCPP_HAS_NO_INT128
template <typename _Tp>
struct _LIBCPP_HIDDEN __traits_base<_Tp, __enable_if_t<sizeof(_Tp) == sizeof(__uint128_t)> > {
  using type = __uint128_t;

  /// The width estimation using a log10 algorithm.
  ///
  /// The algorithm is based on
  /// http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog10
  /// Instead of using IntegerLogBase2 it uses __libcpp_clz. Since that
  /// function requires its input to have at least one bit set the value of
  /// zero is set to one. This means the first element of the lookup table is
  /// zero.
  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI int __width(_Tp __v) {
    _LIBCPP_ASSERT_INTERNAL(
        __v > numeric_limits<uint64_t>::max(), "The optimizations for this algorithm fail when this isn't true.");
    // There's always a bit set in the upper 64-bits.
    auto __t = (128 - std::__libcpp_clz(static_cast<uint64_t>(__v >> 64))) * 1233 >> 12;
    _LIBCPP_ASSERT_INTERNAL(__t >= __itoa::__pow10_128_offset, "Index out of bounds");
    // __t is adjusted since the lookup table misses the lower entries.
    return __t - (__v < __itoa::__pow10_128[__t - __itoa::__pow10_128_offset]) + 1;
  }

  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI char* __convert(char* __p, _Tp __v) {
    return __itoa::__base_10_u128(__p, __v);
  }

  // TODO FMT This pow function should get an index.
  // By moving this to its own header it can be reused by the pow function in to_chars_base_10.
  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI decltype(__pow10_128)& __pow() {
    return __itoa::__pow10_128;
  }
};
#  endif

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI bool
__mul_overflowed(unsigned char __a, _Tp __b, unsigned char& __r) {
  auto __c = __a * __b;
  __r      = __c;
  return __c > numeric_limits<unsigned char>::max();
}

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI bool
__mul_overflowed(unsigned short __a, _Tp __b, unsigned short& __r) {
  auto __c = __a * __b;
  __r      = __c;
  return __c > numeric_limits<unsigned short>::max();
}

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI bool __mul_overflowed(_Tp __a, _Tp __b, _Tp& __r) {
  static_assert(is_unsigned<_Tp>::value, "");
  return __builtin_mul_overflow(__a, __b, &__r);
}

template <typename _Tp, typename _Up>
inline _LIBCPP_HIDE_FROM_ABI bool _LIBCPP_CONSTEXPR_SINCE_CXX23 __mul_overflowed(_Tp __a, _Up __b, _Tp& __r) {
  return __itoa::__mul_overflowed(__a, static_cast<_Tp>(__b), __r);
}

template <typename _Tp>
struct _LIBCPP_HIDDEN __traits : __traits_base<_Tp> {
  static constexpr int digits = numeric_limits<_Tp>::digits10 + 1;
  using __traits_base<_Tp>::__pow;
  using typename __traits_base<_Tp>::type;

  // precondition: at least one non-zero character available
  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI char const*
  __read(char const* __p, char const* __ep, type& __a, type& __b) {
    type __cprod[digits];
    int __j = digits - 1;
    int __i = digits;
    do {
      if (*__p < '0' || *__p > '9')
        break;
      __cprod[--__i] = *__p++ - '0';
    } while (__p != __ep && __i != 0);

    __a = __inner_product(__cprod + __i + 1, __cprod + __j, __pow() + 1, __cprod[__i]);
    if (__itoa::__mul_overflowed(__cprod[__j], __pow()[__j - __i], __b))
      --__p;
    return __p;
  }

  template <typename _It1, typename _It2, class _Up>
  static _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI _Up
  __inner_product(_It1 __first1, _It1 __last1, _It2 __first2, _Up __init) {
    for (; __first1 < __last1; ++__first1, ++__first2)
      __init = __init + *__first1 * *__first2;
    return __init;
  }
};

} // namespace __itoa

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI _Tp __complement(_Tp __x) {
  static_assert(is_unsigned<_Tp>::value, "cast to unsigned first");
  return _Tp(~__x + 1);
}

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___CHARCONV_TRAITS
