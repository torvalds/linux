// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHARCONV_FROM_CHARS_INTEGRAL_H
#define _LIBCPP___CHARCONV_FROM_CHARS_INTEGRAL_H

#include <__algorithm/copy_n.h>
#include <__assert>
#include <__charconv/from_chars_result.h>
#include <__charconv/traits.h>
#include <__config>
#include <__memory/addressof.h>
#include <__system_error/errc.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_unsigned.h>
#include <__type_traits/make_unsigned.h>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

from_chars_result from_chars(const char*, const char*, bool, int = 10) = delete;

template <typename _It, typename _Tp, typename _Fn, typename... _Ts>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI from_chars_result
__sign_combinator(_It __first, _It __last, _Tp& __value, _Fn __f, _Ts... __args) {
  using __tl = numeric_limits<_Tp>;
  decltype(std::__to_unsigned_like(__value)) __x;

  bool __neg = (__first != __last && *__first == '-');
  auto __r   = __f(__neg ? __first + 1 : __first, __last, __x, __args...);
  switch (__r.ec) {
  case errc::invalid_argument:
    return {__first, __r.ec};
  case errc::result_out_of_range:
    return __r;
  default:
    break;
  }

  if (__neg) {
    if (__x <= std::__complement(std::__to_unsigned_like(__tl::min()))) {
      __x = std::__complement(__x);
      std::copy_n(std::addressof(__x), 1, std::addressof(__value));
      return __r;
    }
  } else {
    if (__x <= std::__to_unsigned_like(__tl::max())) {
      __value = __x;
      return __r;
    }
  }

  return {__r.ptr, errc::result_out_of_range};
}

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI bool __in_pattern(_Tp __c) {
  return '0' <= __c && __c <= '9';
}

struct _LIBCPP_HIDDEN __in_pattern_result {
  bool __ok;
  int __val;

  explicit _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI operator bool() const { return __ok; }
};

template <typename _Tp>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI __in_pattern_result __in_pattern(_Tp __c, int __base) {
  if (__base <= 10)
    return {'0' <= __c && __c < '0' + __base, __c - '0'};
  else if (std::__in_pattern(__c))
    return {true, __c - '0'};
  else if ('a' <= __c && __c < 'a' + __base - 10)
    return {true, __c - 'a' + 10};
  else
    return {'A' <= __c && __c < 'A' + __base - 10, __c - 'A' + 10};
}

template <typename _It, typename _Tp, typename _Fn, typename... _Ts>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI from_chars_result
__subject_seq_combinator(_It __first, _It __last, _Tp& __value, _Fn __f, _Ts... __args) {
  auto __find_non_zero = [](_It __firstit, _It __lastit) {
    for (; __firstit != __lastit; ++__firstit)
      if (*__firstit != '0')
        break;
    return __firstit;
  };

  auto __p = __find_non_zero(__first, __last);
  if (__p == __last || !std::__in_pattern(*__p, __args...)) {
    if (__p == __first)
      return {__first, errc::invalid_argument};
    else {
      __value = 0;
      return {__p, {}};
    }
  }

  auto __r = __f(__p, __last, __value, __args...);
  if (__r.ec == errc::result_out_of_range) {
    for (; __r.ptr != __last; ++__r.ptr) {
      if (!std::__in_pattern(*__r.ptr, __args...))
        break;
    }
  }

  return __r;
}

template <typename _Tp, __enable_if_t<is_unsigned<_Tp>::value, int> = 0>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI from_chars_result
__from_chars_atoi(const char* __first, const char* __last, _Tp& __value) {
  using __tx          = __itoa::__traits<_Tp>;
  using __output_type = typename __tx::type;

  return std::__subject_seq_combinator(
      __first, __last, __value, [](const char* __f, const char* __l, _Tp& __val) -> from_chars_result {
        __output_type __a, __b;
        auto __p = __tx::__read(__f, __l, __a, __b);
        if (__p == __l || !std::__in_pattern(*__p)) {
          __output_type __m = numeric_limits<_Tp>::max();
          if (__m >= __a && __m - __a >= __b) {
            __val = __a + __b;
            return {__p, {}};
          }
        }
        return {__p, errc::result_out_of_range};
      });
}

template <typename _Tp, __enable_if_t<is_signed<_Tp>::value, int> = 0>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI from_chars_result
__from_chars_atoi(const char* __first, const char* __last, _Tp& __value) {
  using __t = decltype(std::__to_unsigned_like(__value));
  return std::__sign_combinator(__first, __last, __value, __from_chars_atoi<__t>);
}

/*
// Code used to generate __from_chars_log2f_lut.
#include <cmath>
#include <format>
#include <iostream>

int main() {
  for (int i = 2; i <= 36; ++i)
    std::cout << std::format("{},\n", log2f(i));
}
*/
/// log2f table for bases [2, 36].
inline constexpr float __from_chars_log2f_lut[35] = {
    1,         1.5849625, 2,         2.321928, 2.5849626, 2.807355, 3,        3.169925,  3.321928,
    3.4594316, 3.5849626, 3.7004397, 3.807355, 3.9068906, 4,        4.087463, 4.169925,  4.2479277,
    4.321928,  4.3923173, 4.4594316, 4.523562, 4.5849624, 4.643856, 4.70044,  4.7548876, 4.807355,
    4.857981,  4.9068904, 4.9541965, 5,        5.044394,  5.087463, 5.129283, 5.169925};

template <typename _Tp, __enable_if_t<is_unsigned<_Tp>::value, int> = 0>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI from_chars_result
__from_chars_integral(const char* __first, const char* __last, _Tp& __value, int __base) {
  if (__base == 10)
    return std::__from_chars_atoi(__first, __last, __value);

  return std::__subject_seq_combinator(
      __first,
      __last,
      __value,
      [](const char* __p, const char* __lastp, _Tp& __val, int __b) -> from_chars_result {
        using __tl = numeric_limits<_Tp>;
        // __base is always between 2 and 36 inclusive.
        auto __digits = __tl::digits / __from_chars_log2f_lut[__b - 2];
        _Tp __x = __in_pattern(*__p++, __b).__val, __y = 0;

        for (int __i = 1; __p != __lastp; ++__i, ++__p) {
          if (auto __c = __in_pattern(*__p, __b)) {
            if (__i < __digits - 1)
              __x = __x * __b + __c.__val;
            else {
              if (!__itoa::__mul_overflowed(__x, __b, __x))
                ++__p;
              __y = __c.__val;
              break;
            }
          } else
            break;
        }

        if (__p == __lastp || !__in_pattern(*__p, __b)) {
          if (__tl::max() - __x >= __y) {
            __val = __x + __y;
            return {__p, {}};
          }
        }
        return {__p, errc::result_out_of_range};
      },
      __base);
}

template <typename _Tp, __enable_if_t<is_signed<_Tp>::value, int> = 0>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI from_chars_result
__from_chars_integral(const char* __first, const char* __last, _Tp& __value, int __base) {
  using __t = decltype(std::__to_unsigned_like(__value));
  return std::__sign_combinator(__first, __last, __value, __from_chars_integral<__t>, __base);
}

template <typename _Tp, __enable_if_t<is_integral<_Tp>::value, int> = 0>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI from_chars_result
from_chars(const char* __first, const char* __last, _Tp& __value) {
  return std::__from_chars_atoi(__first, __last, __value);
}

template <typename _Tp, __enable_if_t<is_integral<_Tp>::value, int> = 0>
inline _LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI from_chars_result
from_chars(const char* __first, const char* __last, _Tp& __value, int __base) {
  _LIBCPP_ASSERT_UNCATEGORIZED(2 <= __base && __base <= 36, "base not in [2, 36]");
  return std::__from_chars_integral(__first, __last, __value, __base);
}
#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___CHARCONV_FROM_CHARS_INTEGRAL_H
