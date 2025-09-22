// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___NUMERIC_ADJACENT_DIFFERENCE_H
#define _LIBCPP___NUMERIC_ADJACENT_DIFFERENCE_H

#include <__config>
#include <__iterator/iterator_traits.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _InputIterator, class _OutputIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator
adjacent_difference(_InputIterator __first, _InputIterator __last, _OutputIterator __result) {
  if (__first != __last) {
    typename iterator_traits<_InputIterator>::value_type __acc(*__first);
    *__result = __acc;
    for (++__first, (void)++__result; __first != __last; ++__first, (void)++__result) {
      typename iterator_traits<_InputIterator>::value_type __val(*__first);
#if _LIBCPP_STD_VER >= 20
      *__result = __val - std::move(__acc);
#else
      *__result = __val - __acc;
#endif
      __acc = std::move(__val);
    }
  }
  return __result;
}

template <class _InputIterator, class _OutputIterator, class _BinaryOperation>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator adjacent_difference(
    _InputIterator __first, _InputIterator __last, _OutputIterator __result, _BinaryOperation __binary_op) {
  if (__first != __last) {
    typename iterator_traits<_InputIterator>::value_type __acc(*__first);
    *__result = __acc;
    for (++__first, (void)++__result; __first != __last; ++__first, (void)++__result) {
      typename iterator_traits<_InputIterator>::value_type __val(*__first);
#if _LIBCPP_STD_VER >= 20
      *__result = __binary_op(__val, std::move(__acc));
#else
      *__result = __binary_op(__val, __acc);
#endif
      __acc = std::move(__val);
    }
  }
  return __result;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___NUMERIC_ADJACENT_DIFFERENCE_H
