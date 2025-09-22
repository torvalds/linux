//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_MERGE_H
#define _LIBCPP___ALGORITHM_MERGE_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__algorithm/copy.h>
#include <__config>
#include <__iterator/iterator_traits.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Compare, class _InputIterator1, class _InputIterator2, class _OutputIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator __merge(
    _InputIterator1 __first1,
    _InputIterator1 __last1,
    _InputIterator2 __first2,
    _InputIterator2 __last2,
    _OutputIterator __result,
    _Compare __comp) {
  for (; __first1 != __last1; ++__result) {
    if (__first2 == __last2)
      return std::copy(__first1, __last1, __result);
    if (__comp(*__first2, *__first1)) {
      *__result = *__first2;
      ++__first2;
    } else {
      *__result = *__first1;
      ++__first1;
    }
  }
  return std::copy(__first2, __last2, __result);
}

template <class _InputIterator1, class _InputIterator2, class _OutputIterator, class _Compare>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator
merge(_InputIterator1 __first1,
      _InputIterator1 __last1,
      _InputIterator2 __first2,
      _InputIterator2 __last2,
      _OutputIterator __result,
      _Compare __comp) {
  return std::__merge<__comp_ref_type<_Compare> >(__first1, __last1, __first2, __last2, __result, __comp);
}

template <class _InputIterator1, class _InputIterator2, class _OutputIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator
merge(_InputIterator1 __first1,
      _InputIterator1 __last1,
      _InputIterator2 __first2,
      _InputIterator2 __last2,
      _OutputIterator __result) {
  return std::merge(__first1, __last1, __first2, __last2, __result, __less<>());
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_MERGE_H
