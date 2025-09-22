//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_INCLUDES_H
#define _LIBCPP___ALGORITHM_INCLUDES_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/iterator_traits.h>
#include <__type_traits/is_callable.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Iter1, class _Sent1, class _Iter2, class _Sent2, class _Comp, class _Proj1, class _Proj2>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool __includes(
    _Iter1 __first1,
    _Sent1 __last1,
    _Iter2 __first2,
    _Sent2 __last2,
    _Comp&& __comp,
    _Proj1&& __proj1,
    _Proj2&& __proj2) {
  for (; __first2 != __last2; ++__first1) {
    if (__first1 == __last1 ||
        std::__invoke(__comp, std::__invoke(__proj2, *__first2), std::__invoke(__proj1, *__first1)))
      return false;
    if (!std::__invoke(__comp, std::__invoke(__proj1, *__first1), std::__invoke(__proj2, *__first2)))
      ++__first2;
  }
  return true;
}

template <class _InputIterator1, class _InputIterator2, class _Compare>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool
includes(_InputIterator1 __first1,
         _InputIterator1 __last1,
         _InputIterator2 __first2,
         _InputIterator2 __last2,
         _Compare __comp) {
  static_assert(
      __is_callable<_Compare, decltype(*__first1), decltype(*__first2)>::value, "Comparator has to be callable");

  return std::__includes(
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      static_cast<__comp_ref_type<_Compare> >(__comp),
      __identity(),
      __identity());
}

template <class _InputIterator1, class _InputIterator2>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool
includes(_InputIterator1 __first1, _InputIterator1 __last1, _InputIterator2 __first2, _InputIterator2 __last2) {
  return std::includes(std::move(__first1), std::move(__last1), std::move(__first2), std::move(__last2), __less<>());
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_INCLUDES_H
