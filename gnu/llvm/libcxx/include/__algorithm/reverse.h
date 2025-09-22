//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_REVERSE_H
#define _LIBCPP___ALGORITHM_REVERSE_H

#include <__algorithm/iter_swap.h>
#include <__algorithm/iterator_operations.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _AlgPolicy, class _BidirectionalIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void
__reverse_impl(_BidirectionalIterator __first, _BidirectionalIterator __last, bidirectional_iterator_tag) {
  while (__first != __last) {
    if (__first == --__last)
      break;
    _IterOps<_AlgPolicy>::iter_swap(__first, __last);
    ++__first;
  }
}

template <class _AlgPolicy, class _RandomAccessIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void
__reverse_impl(_RandomAccessIterator __first, _RandomAccessIterator __last, random_access_iterator_tag) {
  if (__first != __last)
    for (; __first < --__last; ++__first)
      _IterOps<_AlgPolicy>::iter_swap(__first, __last);
}

template <class _AlgPolicy, class _BidirectionalIterator, class _Sentinel>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void __reverse(_BidirectionalIterator __first, _Sentinel __last) {
  using _IterCategory = typename _IterOps<_AlgPolicy>::template __iterator_category<_BidirectionalIterator>;
  std::__reverse_impl<_AlgPolicy>(std::move(__first), std::move(__last), _IterCategory());
}

template <class _BidirectionalIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void
reverse(_BidirectionalIterator __first, _BidirectionalIterator __last) {
  std::__reverse<_ClassicAlgPolicy>(std::move(__first), std::move(__last));
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_REVERSE_H
