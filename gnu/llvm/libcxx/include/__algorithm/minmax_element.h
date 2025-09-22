//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_MINMAX_ELEMENT_H
#define _LIBCPP___ALGORITHM_MINMAX_ELEMENT_H

#include <__algorithm/comp.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/iterator_traits.h>
#include <__type_traits/is_callable.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Comp, class _Proj>
class _MinmaxElementLessFunc {
  _Comp& __comp_;
  _Proj& __proj_;

public:
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _MinmaxElementLessFunc(_Comp& __comp, _Proj& __proj)
      : __comp_(__comp), __proj_(__proj) {}

  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 bool operator()(_Iter& __it1, _Iter& __it2) {
    return std::__invoke(__comp_, std::__invoke(__proj_, *__it1), std::__invoke(__proj_, *__it2));
  }
};

template <class _Iter, class _Sent, class _Proj, class _Comp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Iter, _Iter>
__minmax_element_impl(_Iter __first, _Sent __last, _Comp& __comp, _Proj& __proj) {
  auto __less = _MinmaxElementLessFunc<_Comp, _Proj>(__comp, __proj);

  pair<_Iter, _Iter> __result(__first, __first);
  if (__first == __last || ++__first == __last)
    return __result;

  if (__less(__first, __result.first))
    __result.first = __first;
  else
    __result.second = __first;

  while (++__first != __last) {
    _Iter __i = __first;
    if (++__first == __last) {
      if (__less(__i, __result.first))
        __result.first = __i;
      else if (!__less(__i, __result.second))
        __result.second = __i;
      return __result;
    }

    if (__less(__first, __i)) {
      if (__less(__first, __result.first))
        __result.first = __first;
      if (!__less(__i, __result.second))
        __result.second = __i;
    } else {
      if (__less(__i, __result.first))
        __result.first = __i;
      if (!__less(__first, __result.second))
        __result.second = __first;
    }
  }

  return __result;
}

template <class _ForwardIterator, class _Compare>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_ForwardIterator, _ForwardIterator>
minmax_element(_ForwardIterator __first, _ForwardIterator __last, _Compare __comp) {
  static_assert(
      __has_forward_iterator_category<_ForwardIterator>::value, "std::minmax_element requires a ForwardIterator");
  static_assert(
      __is_callable<_Compare, decltype(*__first), decltype(*__first)>::value, "The comparator has to be callable");
  auto __proj = __identity();
  return std::__minmax_element_impl(__first, __last, __comp, __proj);
}

template <class _ForwardIterator>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_ForwardIterator, _ForwardIterator>
minmax_element(_ForwardIterator __first, _ForwardIterator __last) {
  return std::minmax_element(__first, __last, __less<>());
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_MINMAX_ELEMENT_H
