//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_BINARY_SEARCH_H
#define _LIBCPP___ALGORITHM_BINARY_SEARCH_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__algorithm/lower_bound.h>
#include <__config>
#include <__iterator/iterator_traits.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _ForwardIterator, class _Tp, class _Compare>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool
binary_search(_ForwardIterator __first, _ForwardIterator __last, const _Tp& __value, _Compare __comp) {
  __first = std::lower_bound<_ForwardIterator, _Tp, __comp_ref_type<_Compare> >(__first, __last, __value, __comp);
  return __first != __last && !__comp(__value, *__first);
}

template <class _ForwardIterator, class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool
binary_search(_ForwardIterator __first, _ForwardIterator __last, const _Tp& __value) {
  return std::binary_search(__first, __last, __value, __less<>());
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_BINARY_SEARCH_H
