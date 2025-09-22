//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_MAX_ELEMENT_H
#define _LIBCPP___ALGORITHM_MAX_ELEMENT_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__config>
#include <__iterator/iterator_traits.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Compare, class _ForwardIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _ForwardIterator
__max_element(_ForwardIterator __first, _ForwardIterator __last, _Compare __comp) {
  static_assert(
      __has_forward_iterator_category<_ForwardIterator>::value, "std::max_element requires a ForwardIterator");
  if (__first != __last) {
    _ForwardIterator __i = __first;
    while (++__i != __last)
      if (__comp(*__first, *__i))
        __first = __i;
  }
  return __first;
}

template <class _ForwardIterator, class _Compare>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _ForwardIterator
max_element(_ForwardIterator __first, _ForwardIterator __last, _Compare __comp) {
  return std::__max_element<__comp_ref_type<_Compare> >(__first, __last, __comp);
}

template <class _ForwardIterator>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _ForwardIterator
max_element(_ForwardIterator __first, _ForwardIterator __last) {
  return std::max_element(__first, __last, __less<>());
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_MAX_ELEMENT_H
