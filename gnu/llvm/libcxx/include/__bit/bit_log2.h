//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___BIT_BIT_LOG2_H
#define _LIBCPP___BIT_BIT_LOG2_H

#include <__bit/countl.h>
#include <__concepts/arithmetic.h>
#include <__config>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <__libcpp_unsigned_integer _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp __bit_log2(_Tp __t) noexcept {
  return numeric_limits<_Tp>::digits - 1 - std::countl_zero(__t);
}

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___BIT_BIT_LOG2_H
