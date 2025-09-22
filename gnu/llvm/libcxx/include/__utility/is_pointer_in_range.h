//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_IS_POINTER_IN_RANGE_H
#define _LIBCPP___UTILITY_IS_POINTER_IN_RANGE_H

#include <__algorithm/comp.h>
#include <__assert>
#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_constant_evaluated.h>
#include <__type_traits/void_t.h>
#include <__utility/declval.h>
#include <__utility/is_valid_range.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, class _Up, class = void>
struct __is_less_than_comparable : false_type {};

template <class _Tp, class _Up>
struct __is_less_than_comparable<_Tp, _Up, __void_t<decltype(std::declval<_Tp>() < std::declval<_Up>())> > : true_type {
};

template <class _Tp, class _Up, __enable_if_t<__is_less_than_comparable<const _Tp*, const _Up*>::value, int> = 0>
_LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_SANITIZE("address") bool
__is_pointer_in_range(const _Tp* __begin, const _Tp* __end, const _Up* __ptr) {
  _LIBCPP_ASSERT_VALID_INPUT_RANGE(std::__is_valid_range(__begin, __end), "[__begin, __end) is not a valid range");

  if (__libcpp_is_constant_evaluated()) {
    // If this is not a constant during constant evaluation we know that __ptr is not part of the allocation where
    // [__begin, __end) is.
    if (!__builtin_constant_p(__begin <= __ptr && __ptr < __end))
      return false;
  }

  return !__less<>()(__ptr, __begin) && __less<>()(__ptr, __end);
}

template <class _Tp, class _Up, __enable_if_t<!__is_less_than_comparable<const _Tp*, const _Up*>::value, int> = 0>
_LIBCPP_CONSTEXPR_SINCE_CXX14 _LIBCPP_HIDE_FROM_ABI _LIBCPP_NO_SANITIZE("address") bool
__is_pointer_in_range(const _Tp* __begin, const _Tp* __end, const _Up* __ptr) {
  if (__libcpp_is_constant_evaluated())
    return false;

  return reinterpret_cast<const char*>(__begin) <= reinterpret_cast<const char*>(__ptr) &&
         reinterpret_cast<const char*>(__ptr) < reinterpret_cast<const char*>(__end);
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_IS_POINTER_IN_RANGE_H
