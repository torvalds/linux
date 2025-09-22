// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___NUMERIC_TRANSFORM_INCLUSIVE_SCAN_H
#define _LIBCPP___NUMERIC_TRANSFORM_INCLUSIVE_SCAN_H

#include <__config>
#include <__iterator/iterator_traits.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

template <class _InputIterator, class _OutputIterator, class _Tp, class _BinaryOp, class _UnaryOp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator transform_inclusive_scan(
    _InputIterator __first, _InputIterator __last, _OutputIterator __result, _BinaryOp __b, _UnaryOp __u, _Tp __init) {
  for (; __first != __last; ++__first, (void)++__result) {
    __init    = __b(__init, __u(*__first));
    *__result = __init;
  }

  return __result;
}

template <class _InputIterator, class _OutputIterator, class _BinaryOp, class _UnaryOp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator transform_inclusive_scan(
    _InputIterator __first, _InputIterator __last, _OutputIterator __result, _BinaryOp __b, _UnaryOp __u) {
  if (__first != __last) {
    typename iterator_traits<_InputIterator>::value_type __init = __u(*__first);
    *__result++                                                 = __init;
    if (++__first != __last)
      return std::transform_inclusive_scan(__first, __last, __result, __b, __u, __init);
  }

  return __result;
}

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___NUMERIC_TRANSFORM_INCLUSIVE_SCAN_H
