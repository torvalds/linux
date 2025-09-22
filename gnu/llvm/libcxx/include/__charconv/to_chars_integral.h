// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHARCONV_TO_CHARS_INTEGRAL_H
#define _LIBCPP___CHARCONV_TO_CHARS_INTEGRAL_H

#include <__algorithm/copy_n.h>
#include <__assert>
#include <__bit/countl.h>
#include <__charconv/tables.h>
#include <__charconv/to_chars_base_10.h>
#include <__charconv/to_chars_result.h>
#include <__charconv/traits.h>
#include <__config>
#include <__system_error/errc.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_same.h>
#include <__type_traits/make_32_64_or_128_bit.h>
#include <__type_traits/make_unsigned.h>
#include <__utility/unreachable.h>
#include <cstddef>
#include <cstdint>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

to_chars_result to_chars(char*, char*, bool, int = 10) = delete;

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
__to_chars_itoa(char* __first, char* __last, _Tp __value, false_type);

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
__to_chars_itoa(char* __first, char* __last, _Tp __value, true_type) {
  auto __x = std::__to_unsigned_like(__value);
  if (__value < 0 && __first != __last) {
    *__first++ = '-';
    __x        = std::__complement(__x);
  }

  return std::__to_chars_itoa(__first, __last, __x, false_type());
}

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
__to_chars_itoa(char* __first, char* __last, _Tp __value, false_type) {
  using __tx  = __itoa::__traits<_Tp>;
  auto __diff = __last - __first;

  if (__tx::digits <= __diff || __tx::__width(__value) <= __diff)
    return {__tx::__convert(__first, __value), errc(0)};
  else
    return {__last, errc::value_too_large};
}

#  ifndef _LIBCPP_HAS_NO_INT128
template <>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
__to_chars_itoa(char* __first, char* __last, __uint128_t __value, false_type) {
  // When the value fits in 64-bits use the 64-bit code path. This reduces
  // the number of expensive calculations on 128-bit values.
  //
  // NOTE the 128-bit code path requires this optimization.
  if (__value <= numeric_limits<uint64_t>::max())
    return __to_chars_itoa(__first, __last, static_cast<uint64_t>(__value), false_type());

  using __tx  = __itoa::__traits<__uint128_t>;
  auto __diff = __last - __first;

  if (__tx::digits <= __diff || __tx::__width(__value) <= __diff)
    return {__tx::__convert(__first, __value), errc(0)};
  else
    return {__last, errc::value_too_large};
}
#  endif

template <class _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
__to_chars_integral(char* __first, char* __last, _Tp __value, int __base, false_type);

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
__to_chars_integral(char* __first, char* __last, _Tp __value, int __base, true_type) {
  auto __x = std::__to_unsigned_like(__value);
  if (__value < 0 && __first != __last) {
    *__first++ = '-';
    __x        = std::__complement(__x);
  }

  return std::__to_chars_integral(__first, __last, __x, __base, false_type());
}

namespace __itoa {

template <unsigned _Base>
struct _LIBCPP_HIDDEN __integral;

template <>
struct _LIBCPP_HIDDEN __integral<2> {
  template <typename _Tp>
  _LIBCPP_HIDE_FROM_ABI static constexpr int __width(_Tp __value) noexcept {
    // If value == 0 still need one digit. If the value != this has no
    // effect since the code scans for the most significant bit set. (Note
    // that __libcpp_clz doesn't work for 0.)
    return numeric_limits<_Tp>::digits - std::__libcpp_clz(__value | 1);
  }

  template <typename _Tp>
  _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI static to_chars_result
  __to_chars(char* __first, char* __last, _Tp __value) {
    ptrdiff_t __cap = __last - __first;
    int __n         = __width(__value);
    if (__n > __cap)
      return {__last, errc::value_too_large};

    __last                   = __first + __n;
    char* __p                = __last;
    const unsigned __divisor = 16;
    while (__value > __divisor) {
      unsigned __c = __value % __divisor;
      __value /= __divisor;
      __p -= 4;
      std::copy_n(&__base_2_lut[4 * __c], 4, __p);
    }
    do {
      unsigned __c = __value % 2;
      __value /= 2;
      *--__p = "01"[__c];
    } while (__value != 0);
    return {__last, errc(0)};
  }
};

template <>
struct _LIBCPP_HIDDEN __integral<8> {
  template <typename _Tp>
  _LIBCPP_HIDE_FROM_ABI static constexpr int __width(_Tp __value) noexcept {
    // If value == 0 still need one digit. If the value != this has no
    // effect since the code scans for the most significat bit set. (Note
    // that __libcpp_clz doesn't work for 0.)
    return ((numeric_limits<_Tp>::digits - std::__libcpp_clz(__value | 1)) + 2) / 3;
  }

  template <typename _Tp>
  _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI static to_chars_result
  __to_chars(char* __first, char* __last, _Tp __value) {
    ptrdiff_t __cap = __last - __first;
    int __n         = __width(__value);
    if (__n > __cap)
      return {__last, errc::value_too_large};

    __last             = __first + __n;
    char* __p          = __last;
    unsigned __divisor = 64;
    while (__value > __divisor) {
      unsigned __c = __value % __divisor;
      __value /= __divisor;
      __p -= 2;
      std::copy_n(&__base_8_lut[2 * __c], 2, __p);
    }
    do {
      unsigned __c = __value % 8;
      __value /= 8;
      *--__p = "01234567"[__c];
    } while (__value != 0);
    return {__last, errc(0)};
  }
};

template <>
struct _LIBCPP_HIDDEN __integral<16> {
  template <typename _Tp>
  _LIBCPP_HIDE_FROM_ABI static constexpr int __width(_Tp __value) noexcept {
    // If value == 0 still need one digit. If the value != this has no
    // effect since the code scans for the most significat bit set. (Note
    // that __libcpp_clz doesn't work for 0.)
    return (numeric_limits<_Tp>::digits - std::__libcpp_clz(__value | 1) + 3) / 4;
  }

  template <typename _Tp>
  _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI static to_chars_result
  __to_chars(char* __first, char* __last, _Tp __value) {
    ptrdiff_t __cap = __last - __first;
    int __n         = __width(__value);
    if (__n > __cap)
      return {__last, errc::value_too_large};

    __last             = __first + __n;
    char* __p          = __last;
    unsigned __divisor = 256;
    while (__value > __divisor) {
      unsigned __c = __value % __divisor;
      __value /= __divisor;
      __p -= 2;
      std::copy_n(&__base_16_lut[2 * __c], 2, __p);
    }
    if (__first != __last)
      do {
        unsigned __c = __value % 16;
        __value /= 16;
        *--__p = "0123456789abcdef"[__c];
      } while (__value != 0);
    return {__last, errc(0)};
  }
};

} // namespace __itoa

template <unsigned _Base, typename _Tp, __enable_if_t<(sizeof(_Tp) >= sizeof(unsigned)), int> = 0>
_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI int __to_chars_integral_width(_Tp __value) {
  return __itoa::__integral<_Base>::__width(__value);
}

template <unsigned _Base, typename _Tp, __enable_if_t<(sizeof(_Tp) < sizeof(unsigned)), int> = 0>
_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI int __to_chars_integral_width(_Tp __value) {
  return std::__to_chars_integral_width<_Base>(static_cast<unsigned>(__value));
}

template <unsigned _Base, typename _Tp, __enable_if_t<(sizeof(_Tp) >= sizeof(unsigned)), int> = 0>
_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
__to_chars_integral(char* __first, char* __last, _Tp __value) {
  return __itoa::__integral<_Base>::__to_chars(__first, __last, __value);
}

template <unsigned _Base, typename _Tp, __enable_if_t<(sizeof(_Tp) < sizeof(unsigned)), int> = 0>
_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
__to_chars_integral(char* __first, char* __last, _Tp __value) {
  return std::__to_chars_integral<_Base>(__first, __last, static_cast<unsigned>(__value));
}

template <typename _Tp>
_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI int __to_chars_integral_width(_Tp __value, unsigned __base) {
  _LIBCPP_ASSERT_INTERNAL(__value >= 0, "The function requires a non-negative value.");

  unsigned __base_2 = __base * __base;
  unsigned __base_3 = __base_2 * __base;
  unsigned __base_4 = __base_2 * __base_2;

  int __r = 0;
  while (true) {
    if (__value < __base)
      return __r + 1;
    if (__value < __base_2)
      return __r + 2;
    if (__value < __base_3)
      return __r + 3;
    if (__value < __base_4)
      return __r + 4;

    __value /= __base_4;
    __r += 4;
  }

  __libcpp_unreachable();
}

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
__to_chars_integral(char* __first, char* __last, _Tp __value, int __base, false_type) {
  if (__base == 10) [[likely]]
    return std::__to_chars_itoa(__first, __last, __value, false_type());

  switch (__base) {
  case 2:
    return std::__to_chars_integral<2>(__first, __last, __value);
  case 8:
    return std::__to_chars_integral<8>(__first, __last, __value);
  case 16:
    return std::__to_chars_integral<16>(__first, __last, __value);
  }

  ptrdiff_t __cap = __last - __first;
  int __n         = std::__to_chars_integral_width(__value, __base);
  if (__n > __cap)
    return {__last, errc::value_too_large};

  __last    = __first + __n;
  char* __p = __last;
  do {
    unsigned __c = __value % __base;
    __value /= __base;
    *--__p = "0123456789abcdefghijklmnopqrstuvwxyz"[__c];
  } while (__value != 0);
  return {__last, errc(0)};
}

template <typename _Tp, __enable_if_t<is_integral<_Tp>::value, int> = 0>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
to_chars(char* __first, char* __last, _Tp __value) {
  using _Type = __make_32_64_or_128_bit_t<_Tp>;
  static_assert(!is_same<_Type, void>::value, "unsupported integral type used in to_chars");
  return std::__to_chars_itoa(__first, __last, static_cast<_Type>(__value), is_signed<_Tp>());
}

template <typename _Tp, __enable_if_t<is_integral<_Tp>::value, int> = 0>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI to_chars_result
to_chars(char* __first, char* __last, _Tp __value, int __base) {
  _LIBCPP_ASSERT_UNCATEGORIZED(2 <= __base && __base <= 36, "base not in [2, 36]");

  using _Type = __make_32_64_or_128_bit_t<_Tp>;
  return std::__to_chars_integral(__first, __last, static_cast<_Type>(__value), __base, is_signed<_Tp>());
}

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___CHARCONV_TO_CHARS_INTEGRAL_H
