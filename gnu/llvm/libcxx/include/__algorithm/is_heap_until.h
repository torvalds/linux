//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_IS_HEAP_UNTIL_H
#define _LIBCPP___ALGORITHM_IS_HEAP_UNTIL_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__config>
#include <__iterator/iterator_traits.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Compare, class _RandomAccessIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _RandomAccessIterator
__is_heap_until(_RandomAccessIterator __first, _RandomAccessIterator __last, _Compare&& __comp) {
  typedef typename iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  difference_type __len      = __last - __first;
  difference_type __p        = 0;
  difference_type __c        = 1;
  _RandomAccessIterator __pp = __first;
  while (__c < __len) {
    _RandomAccessIterator __cp = __first + __c;
    if (__comp(*__pp, *__cp))
      return __cp;
    ++__c;
    ++__cp;
    if (__c == __len)
      return __last;
    if (__comp(*__pp, *__cp))
      return __cp;
    ++__p;
    ++__pp;
    __c = 2 * __p + 1;
  }
  return __last;
}

template <class _RandomAccessIterator, class _Compare>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _RandomAccessIterator
is_heap_until(_RandomAccessIterator __first, _RandomAccessIterator __last, _Compare __comp) {
  return std::__is_heap_until(__first, __last, static_cast<__comp_ref_type<_Compare> >(__comp));
}

template <class _RandomAccessIterator>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _RandomAccessIterator
is_heap_until(_RandomAccessIterator __first, _RandomAccessIterator __last) {
  return std::__is_heap_until(__first, __last, __less<>());
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_IS_HEAP_UNTIL_H
