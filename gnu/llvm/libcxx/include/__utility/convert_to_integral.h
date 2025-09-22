//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_CONVERT_TO_INTEGRAL_H
#define _LIBCPP___UTILITY_CONVERT_TO_INTEGRAL_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_enum.h>
#include <__type_traits/is_floating_point.h>
#include <__type_traits/underlying_type.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int __convert_to_integral(int __val) { return __val; }

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR unsigned __convert_to_integral(unsigned __val) { return __val; }

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR long __convert_to_integral(long __val) { return __val; }

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR unsigned long __convert_to_integral(unsigned long __val) {
  return __val;
}

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR long long __convert_to_integral(long long __val) { return __val; }

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR unsigned long long __convert_to_integral(unsigned long long __val) {
  return __val;
}

template <typename _Fp, __enable_if_t<is_floating_point<_Fp>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR long long __convert_to_integral(_Fp __val) {
  return __val;
}

#ifndef _LIBCPP_HAS_NO_INT128
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR __int128_t __convert_to_integral(__int128_t __val) { return __val; }

inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR __uint128_t __convert_to_integral(__uint128_t __val) { return __val; }
#endif

template <class _Tp, bool = is_enum<_Tp>::value>
struct __sfinae_underlying_type {
  typedef typename underlying_type<_Tp>::type type;
  typedef decltype(((type)1) + 0) __promoted_type;
};

template <class _Tp>
struct __sfinae_underlying_type<_Tp, false> {};

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR typename __sfinae_underlying_type<_Tp>::__promoted_type
__convert_to_integral(_Tp __val) {
  return __val;
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_CONVERT_TO_INTEGRAL_H
