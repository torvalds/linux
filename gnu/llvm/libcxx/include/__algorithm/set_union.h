//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_SET_UNION_H
#define _LIBCPP___ALGORITHM_SET_UNION_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__algorithm/copy.h>
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

template <class _InIter1, class _InIter2, class _OutIter>
struct __set_union_result {
  _InIter1 __in1_;
  _InIter2 __in2_;
  _OutIter __out_;

  // need a constructor as C++03 aggregate init is hard
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20
  __set_union_result(_InIter1&& __in_iter1, _InIter2&& __in_iter2, _OutIter&& __out_iter)
      : __in1_(std::move(__in_iter1)), __in2_(std::move(__in_iter2)), __out_(std::move(__out_iter)) {}
};

template <class _AlgPolicy, class _Compare, class _InIter1, class _Sent1, class _InIter2, class _Sent2, class _OutIter>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 __set_union_result<_InIter1, _InIter2, _OutIter> __set_union(
    _InIter1 __first1, _Sent1 __last1, _InIter2 __first2, _Sent2 __last2, _OutIter __result, _Compare&& __comp) {
  for (; __first1 != __last1; ++__result) {
    if (__first2 == __last2) {
      auto __ret1 = std::__copy<_AlgPolicy>(std::move(__first1), std::move(__last1), std::move(__result));
      return __set_union_result<_InIter1, _InIter2, _OutIter>(
          std::move(__ret1.first), std::move(__first2), std::move((__ret1.second)));
    }
    if (__comp(*__first2, *__first1)) {
      *__result = *__first2;
      ++__first2;
    } else {
      if (!__comp(*__first1, *__first2)) {
        ++__first2;
      }
      *__result = *__first1;
      ++__first1;
    }
  }
  auto __ret2 = std::__copy<_AlgPolicy>(std::move(__first2), std::move(__last2), std::move(__result));
  return __set_union_result<_InIter1, _InIter2, _OutIter>(
      std::move(__first1), std::move(__ret2.first), std::move((__ret2.second)));
}

template <class _InputIterator1, class _InputIterator2, class _OutputIterator, class _Compare>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator set_union(
    _InputIterator1 __first1,
    _InputIterator1 __last1,
    _InputIterator2 __first2,
    _InputIterator2 __last2,
    _OutputIterator __result,
    _Compare __comp) {
  return std::__set_union<_ClassicAlgPolicy, __comp_ref_type<_Compare> >(
             std::move(__first1),
             std::move(__last1),
             std::move(__first2),
             std::move(__last2),
             std::move(__result),
             __comp)
      .__out_;
}

template <class _InputIterator1, class _InputIterator2, class _OutputIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator set_union(
    _InputIterator1 __first1,
    _InputIterator1 __last1,
    _InputIterator2 __first2,
    _InputIterator2 __last2,
    _OutputIterator __result) {
  return std::set_union(
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      std::move(__result),
      __less<>());
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_SET_UNION_H
