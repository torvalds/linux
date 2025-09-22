//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_IS_VALID_RANGE_H
#define _LIBCPP___UTILITY_IS_VALID_RANGE_H

#include <__algorithm/comp.h>
#include <__config>
#include <__type_traits/is_constant_evaluated.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
_LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_SANITIZE("address") bool
__is_valid_range(const _Tp* __first, const _Tp* __last) {
  if (__libcpp_is_constant_evaluated()) {
    // If this is not a constant during constant evaluation, that is because __first and __last are not
    // part of the same allocation. If they are part of the same allocation, we must still make sure they
    // are ordered properly.
    return __builtin_constant_p(__first <= __last) && __first <= __last;
  }

  return !__less<>()(__last, __first);
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_IS_VALID_RANGE_H
