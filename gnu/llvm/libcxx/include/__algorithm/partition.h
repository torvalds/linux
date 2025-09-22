//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_PARTITION_H
#define _LIBCPP___ALGORITHM_PARTITION_H

#include <__algorithm/iterator_operations.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__utility/move.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Predicate, class _AlgPolicy, class _ForwardIterator, class _Sentinel>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_ForwardIterator, _ForwardIterator>
__partition_impl(_ForwardIterator __first, _Sentinel __last, _Predicate __pred, forward_iterator_tag) {
  while (true) {
    if (__first == __last)
      return std::make_pair(std::move(__first), std::move(__first));
    if (!__pred(*__first))
      break;
    ++__first;
  }

  _ForwardIterator __p = __first;
  while (++__p != __last) {
    if (__pred(*__p)) {
      _IterOps<_AlgPolicy>::iter_swap(__first, __p);
      ++__first;
    }
  }
  return std::make_pair(std::move(__first), std::move(__p));
}

template <class _Predicate, class _AlgPolicy, class _BidirectionalIterator, class _Sentinel>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_BidirectionalIterator, _BidirectionalIterator>
__partition_impl(_BidirectionalIterator __first, _Sentinel __sentinel, _Predicate __pred, bidirectional_iterator_tag) {
  _BidirectionalIterator __original_last = _IterOps<_AlgPolicy>::next(__first, __sentinel);
  _BidirectionalIterator __last          = __original_last;

  while (true) {
    while (true) {
      if (__first == __last)
        return std::make_pair(std::move(__first), std::move(__original_last));
      if (!__pred(*__first))
        break;
      ++__first;
    }
    do {
      if (__first == --__last)
        return std::make_pair(std::move(__first), std::move(__original_last));
    } while (!__pred(*__last));
    _IterOps<_AlgPolicy>::iter_swap(__first, __last);
    ++__first;
  }
}

template <class _AlgPolicy, class _ForwardIterator, class _Sentinel, class _Predicate, class _IterCategory>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_ForwardIterator, _ForwardIterator>
__partition(_ForwardIterator __first, _Sentinel __last, _Predicate&& __pred, _IterCategory __iter_category) {
  return std::__partition_impl<__remove_cvref_t<_Predicate>&, _AlgPolicy>(
      std::move(__first), std::move(__last), __pred, __iter_category);
}

template <class _ForwardIterator, class _Predicate>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator
partition(_ForwardIterator __first, _ForwardIterator __last, _Predicate __pred) {
  using _IterCategory = typename iterator_traits<_ForwardIterator>::iterator_category;
  auto __result = std::__partition<_ClassicAlgPolicy>(std::move(__first), std::move(__last), __pred, _IterCategory());
  return __result.first;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_PARTITION_H
