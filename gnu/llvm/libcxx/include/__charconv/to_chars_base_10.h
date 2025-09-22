// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHARCONV_TO_CHARS_BASE_10_H
#define _LIBCPP___CHARCONV_TO_CHARS_BASE_10_H

#include <__algorithm/copy_n.h>
#include <__assert>
#include <__charconv/tables.h>
#include <__config>
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

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char* __append1(char* __first, uint32_t __value) noexcept {
  *__first = '0' + static_cast<char>(__value);
  return __first + 1;
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char* __append2(char* __first, uint32_t __value) noexcept {
  return std::copy_n(&__digits_base_10[__value * 2], 2, __first);
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char* __append3(char* __first, uint32_t __value) noexcept {
  return __itoa::__append2(__itoa::__append1(__first, __value / 100), __value % 100);
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char* __append4(char* __first, uint32_t __value) noexcept {
  return __itoa::__append2(__itoa::__append2(__first, __value / 100), __value % 100);
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char* __append5(char* __first, uint32_t __value) noexcept {
  return __itoa::__append4(__itoa::__append1(__first, __value / 10000), __value % 10000);
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char* __append6(char* __first, uint32_t __value) noexcept {
  return __itoa::__append4(__itoa::__append2(__first, __value / 10000), __value % 10000);
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char* __append7(char* __first, uint32_t __value) noexcept {
  return __itoa::__append6(__itoa::__append1(__first, __value / 1000000), __value % 1000000);
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char* __append8(char* __first, uint32_t __value) noexcept {
  return __itoa::__append6(__itoa::__append2(__first, __value / 1000000), __value % 1000000);
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char* __append9(char* __first, uint32_t __value) noexcept {
  return __itoa::__append8(__itoa::__append1(__first, __value / 100000000), __value % 100000000);
}

template <class _Tp>
_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI char* __append10(char* __first, _Tp __value) noexcept {
  return __itoa::__append8(__itoa::__append2(__first, static_cast<uint32_t>(__value / 100000000)),
                           static_cast<uint32_t>(__value % 100000000));
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char*
__base_10_u32(char* __first, uint32_t __value) noexcept {
  if (__value < 1000000) {
    if (__value < 10000) {
      if (__value < 100) {
        // 0 <= __value < 100
        if (__value < 10)
          return __itoa::__append1(__first, __value);
        return __itoa::__append2(__first, __value);
      }
      // 100 <= __value < 10'000
      if (__value < 1000)
        return __itoa::__append3(__first, __value);
      return __itoa::__append4(__first, __value);
    }

    // 10'000 <= __value < 1'000'000
    if (__value < 100000)
      return __itoa::__append5(__first, __value);
    return __itoa::__append6(__first, __value);
  }

  // __value => 1'000'000
  if (__value < 100000000) {
    // 1'000'000 <= __value < 100'000'000
    if (__value < 10000000)
      return __itoa::__append7(__first, __value);
    return __itoa::__append8(__first, __value);
  }

  // 100'000'000 <= __value < max
  if (__value < 1000000000)
    return __itoa::__append9(__first, __value);
  return __itoa::__append10(__first, __value);
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char*
__base_10_u64(char* __buffer, uint64_t __value) noexcept {
  if (__value <= UINT32_MAX)
    return __itoa::__base_10_u32(__buffer, static_cast<uint32_t>(__value));

  // Numbers in the range UINT32_MAX <= val < 10'000'000'000 always contain 10
  // digits and are outputted after this if statement.
  if (__value >= 10000000000) {
    // This function properly deterimines the first non-zero leading digit.
    __buffer = __itoa::__base_10_u32(__buffer, static_cast<uint32_t>(__value / 10000000000));
    __value %= 10000000000;
  }
  return __itoa::__append10(__buffer, __value);
}

#  ifndef _LIBCPP_HAS_NO_INT128
/// \returns 10^\a exp
///
/// \pre \a exp [19, 39]
///
/// \note The lookup table contains a partial set of exponents limiting the
/// range that can be used. However the range is sufficient for
/// \ref __base_10_u128.
_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline __uint128_t __pow_10(int __exp) noexcept {
  _LIBCPP_ASSERT_INTERNAL(__exp >= __pow10_128_offset, "Index out of bounds");
  return __pow10_128[__exp - __pow10_128_offset];
}

_LIBCPP_CONSTEXPR_SINCE_CXX23 _LIBCPP_HIDE_FROM_ABI inline char*
__base_10_u128(char* __buffer, __uint128_t __value) noexcept {
  _LIBCPP_ASSERT_INTERNAL(
      __value > numeric_limits<uint64_t>::max(), "The optimizations for this algorithm fails when this isn't true.");

  // Unlike the 64 to 32 bit case the 128 bit case the "upper half" can't be
  // stored in the "lower half". Instead we first need to handle the top most
  // digits separately.
  //
  // Maximum unsigned values
  // 64  bit                             18'446'744'073'709'551'615 (20 digits)
  // 128 bit    340'282'366'920'938'463'463'374'607'431'768'211'455 (39 digits)
  // step 1     ^                                                   ([0-1] digits)
  // step 2      ^^^^^^^^^^^^^^^^^^^^^^^^^                          ([0-19] digits)
  // step 3                               ^^^^^^^^^^^^^^^^^^^^^^^^^ (19 digits)
  if (__value >= __itoa::__pow_10(38)) {
    // step 1
    __buffer = __itoa::__append1(__buffer, static_cast<uint32_t>(__value / __itoa::__pow_10(38)));
    __value %= __itoa::__pow_10(38);

    // step 2 always 19 digits.
    // They are handled here since leading zeros need to be appended to the buffer,
    __buffer = __itoa::__append9(__buffer, static_cast<uint32_t>(__value / __itoa::__pow_10(29)));
    __value %= __itoa::__pow_10(29);
    __buffer = __itoa::__append10(__buffer, static_cast<uint64_t>(__value / __itoa::__pow_10(19)));
    __value %= __itoa::__pow_10(19);
  } else {
    // step 2
    // This version needs to determine the position of the leading non-zero digit.
    __buffer = __base_10_u64(__buffer, static_cast<uint64_t>(__value / __itoa::__pow_10(19)));
    __value %= __itoa::__pow_10(19);
  }

  // Step 3
  __buffer = __itoa::__append9(__buffer, static_cast<uint32_t>(__value / 10000000000));
  __buffer = __itoa::__append10(__buffer, static_cast<uint64_t>(__value % 10000000000));

  return __buffer;
}
#  endif
} // namespace __itoa

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___CHARCONV_TO_CHARS_BASE_10_H
