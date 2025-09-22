//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// TODO: __builtin_clzg is available since Clang 19 and GCC 14. When support for older versions is dropped, we can
//  refactor this code to exclusively use __builtin_clzg.

#ifndef _LIBCPP___BIT_COUNTL_H
#define _LIBCPP___BIT_COUNTL_H

#include <__bit/rotate.h>
#include <__concepts/arithmetic.h>
#include <__config>
#include <__type_traits/is_unsigned_integer.h>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int __libcpp_clz(unsigned __x) _NOEXCEPT {
  return __builtin_clz(__x);
}

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int __libcpp_clz(unsigned long __x) _NOEXCEPT {
  return __builtin_clzl(__x);
}

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int __libcpp_clz(unsigned long long __x) _NOEXCEPT {
  return __builtin_clzll(__x);
}

#ifndef _LIBCPP_HAS_NO_INT128
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR int __libcpp_clz(__uint128_t __x) _NOEXCEPT {
#  if __has_builtin(__builtin_clzg)
  return __builtin_clzg(__x);
#  else
  // The function is written in this form due to C++ constexpr limitations.
  // The algorithm:
  // - Test whether any bit in the high 64-bits is set
  // - No bits set:
  //   - The high 64-bits contain 64 leading zeros,
  //   - Add the result of the low 64-bits.
  // - Any bits set:
  //   - The number of leading zeros of the input is the number of leading
  //     zeros in the high 64-bits.
  return ((__x >> 64) == 0) ? (64 + __builtin_clzll(static_cast<unsigned long long>(__x)))
                            : __builtin_clzll(static_cast<unsigned long long>(__x >> 64));
#  endif
}
#endif // _LIBCPP_HAS_NO_INT128

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 int __countl_zero(_Tp __t) _NOEXCEPT {
  static_assert(__libcpp_is_unsigned_integer<_Tp>::value, "__countl_zero requires an unsigned integer type");
#if __has_builtin(__builtin_clzg)
  return __builtin_clzg(__t, numeric_limits<_Tp>::digits);
#else  // __has_builtin(__builtin_clzg)
  if (__t == 0)
    return numeric_limits<_Tp>::digits;

  if (sizeof(_Tp) <= sizeof(unsigned int))
    return std::__libcpp_clz(static_cast<unsigned int>(__t)) -
           (numeric_limits<unsigned int>::digits - numeric_limits<_Tp>::digits);
  else if (sizeof(_Tp) <= sizeof(unsigned long))
    return std::__libcpp_clz(static_cast<unsigned long>(__t)) -
           (numeric_limits<unsigned long>::digits - numeric_limits<_Tp>::digits);
  else if (sizeof(_Tp) <= sizeof(unsigned long long))
    return std::__libcpp_clz(static_cast<unsigned long long>(__t)) -
           (numeric_limits<unsigned long long>::digits - numeric_limits<_Tp>::digits);
  else {
    int __ret                      = 0;
    int __iter                     = 0;
    const unsigned int __ulldigits = numeric_limits<unsigned long long>::digits;
    while (true) {
      __t = std::__rotl(__t, __ulldigits);
      if ((__iter = std::__countl_zero(static_cast<unsigned long long>(__t))) != __ulldigits)
        break;
      __ret += __iter;
    }
    return __ret + __iter;
  }
#endif // __has_builtin(__builtin_clzg)
}

#if _LIBCPP_STD_VER >= 20

template <__libcpp_unsigned_integer _Tp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr int countl_zero(_Tp __t) noexcept {
  return std::__countl_zero(__t);
}

template <__libcpp_unsigned_integer _Tp>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr int countl_one(_Tp __t) noexcept {
  return __t != numeric_limits<_Tp>::max() ? std::countl_zero(static_cast<_Tp>(~__t)) : numeric_limits<_Tp>::digits;
}

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___BIT_COUNTL_H
