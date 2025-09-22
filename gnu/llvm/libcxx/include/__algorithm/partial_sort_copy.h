//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_PARTIAL_SORT_COPY_H
#define _LIBCPP___ALGORITHM_PARTIAL_SORT_COPY_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/make_heap.h>
#include <__algorithm/make_projected.h>
#include <__algorithm/sift_down.h>
#include <__algorithm/sort_heap.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/iterator_traits.h>
#include <__type_traits/is_callable.h>
#include <__utility/move.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _AlgPolicy,
          class _Compare,
          class _InputIterator,
          class _Sentinel1,
          class _RandomAccessIterator,
          class _Sentinel2,
          class _Proj1,
          class _Proj2>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 pair<_InputIterator, _RandomAccessIterator> __partial_sort_copy(
    _InputIterator __first,
    _Sentinel1 __last,
    _RandomAccessIterator __result_first,
    _Sentinel2 __result_last,
    _Compare&& __comp,
    _Proj1&& __proj1,
    _Proj2&& __proj2) {
  _RandomAccessIterator __r = __result_first;
  auto&& __projected_comp   = std::__make_projected(__comp, __proj2);

  if (__r != __result_last) {
    for (; __first != __last && __r != __result_last; ++__first, (void)++__r)
      *__r = *__first;
    std::__make_heap<_AlgPolicy>(__result_first, __r, __projected_comp);
    typename iterator_traits<_RandomAccessIterator>::difference_type __len = __r - __result_first;
    for (; __first != __last; ++__first)
      if (std::__invoke(__comp, std::__invoke(__proj1, *__first), std::__invoke(__proj2, *__result_first))) {
        *__result_first = *__first;
        std::__sift_down<_AlgPolicy>(__result_first, __projected_comp, __len, __result_first);
      }
    std::__sort_heap<_AlgPolicy>(__result_first, __r, __projected_comp);
  }

  return pair<_InputIterator, _RandomAccessIterator>(
      _IterOps<_AlgPolicy>::next(std::move(__first), std::move(__last)), std::move(__r));
}

template <class _InputIterator, class _RandomAccessIterator, class _Compare>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _RandomAccessIterator partial_sort_copy(
    _InputIterator __first,
    _InputIterator __last,
    _RandomAccessIterator __result_first,
    _RandomAccessIterator __result_last,
    _Compare __comp) {
  static_assert(
      __is_callable<_Compare, decltype(*__first), decltype(*__result_first)>::value, "Comparator has to be callable");

  auto __result = std::__partial_sort_copy<_ClassicAlgPolicy>(
      __first,
      __last,
      __result_first,
      __result_last,
      static_cast<__comp_ref_type<_Compare> >(__comp),
      __identity(),
      __identity());
  return __result.second;
}

template <class _InputIterator, class _RandomAccessIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _RandomAccessIterator partial_sort_copy(
    _InputIterator __first,
    _InputIterator __last,
    _RandomAccessIterator __result_first,
    _RandomAccessIterator __result_last) {
  return std::partial_sort_copy(__first, __last, __result_first, __result_last, __less<>());
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_PARTIAL_SORT_COPY_H
