//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_FIND_SEGMENT_IF_H
#define _LIBCPP___ALGORITHM_FIND_SEGMENT_IF_H

#include <__config>
#include <__iterator/segmented_iterator.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// __find_segment_if is a utility function for optimizing iteration over segmented iterators linearly.
// [__first, __last) has to be a segmented range. __pred is expected to take a range of local iterators and the __proj.
// It returns an iterator to the first element that satisfies the predicate, or a one-past-the-end iterator if there was
// no match. __proj may be anything that should be passed to __pred, but is expected to be a projection to support
// ranges algorithms, or __identity for classic algorithms.

template <class _SegmentedIterator, class _Pred, class _Proj>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _SegmentedIterator
__find_segment_if(_SegmentedIterator __first, _SegmentedIterator __last, _Pred __pred, _Proj& __proj) {
  using _Traits = __segmented_iterator_traits<_SegmentedIterator>;

  auto __sfirst = _Traits::__segment(__first);
  auto __slast  = _Traits::__segment(__last);

  // We are in a single segment, so we might not be at the beginning or end
  if (__sfirst == __slast)
    return _Traits::__compose(__sfirst, __pred(_Traits::__local(__first), _Traits::__local(__last), __proj));

  { // We have more than one segment. Iterate over the first segment, since we might not start at the beginning
    auto __llast = _Traits::__end(__sfirst);
    auto __liter = __pred(_Traits::__local(__first), __llast, __proj);
    if (__liter != __llast)
      return _Traits::__compose(__sfirst, __liter);
  }
  ++__sfirst;

  // Iterate over the segments which are guaranteed to be completely in the range
  while (__sfirst != __slast) {
    auto __llast = _Traits::__end(__sfirst);
    auto __liter = __pred(_Traits::__begin(__sfirst), _Traits::__end(__sfirst), __proj);
    if (__liter != __llast)
      return _Traits::__compose(__sfirst, __liter);
    ++__sfirst;
  }

  // Iterate over the last segment
  return _Traits::__compose(__sfirst, __pred(_Traits::__begin(__sfirst), _Traits::__local(__last), __proj));
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_FIND_SEGMENT_IF_H
