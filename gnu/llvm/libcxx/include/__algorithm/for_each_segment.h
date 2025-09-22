//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_FOR_EACH_SEGMENT_H
#define _LIBCPP___ALGORITHM_FOR_EACH_SEGMENT_H

#include <__config>
#include <__iterator/segmented_iterator.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// __for_each_segment is a utility function for optimizing iterating over segmented iterators linearly.
// __first and __last are expected to be a segmented range. __func is expected to take a range of local iterators.
// Anything that is returned from __func is ignored.

template <class _SegmentedIterator, class _Functor>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 void
__for_each_segment(_SegmentedIterator __first, _SegmentedIterator __last, _Functor __func) {
  using _Traits = __segmented_iterator_traits<_SegmentedIterator>;

  auto __sfirst = _Traits::__segment(__first);
  auto __slast  = _Traits::__segment(__last);

  // We are in a single segment, so we might not be at the beginning or end
  if (__sfirst == __slast) {
    __func(_Traits::__local(__first), _Traits::__local(__last));
    return;
  }

  // We have more than one segment. Iterate over the first segment, since we might not start at the beginning
  __func(_Traits::__local(__first), _Traits::__end(__sfirst));
  ++__sfirst;
  // iterate over the segments which are guaranteed to be completely in the range
  while (__sfirst != __slast) {
    __func(_Traits::__begin(__sfirst), _Traits::__end(__sfirst));
    ++__sfirst;
  }
  // iterate over the last segment
  __func(_Traits::__begin(__sfirst), _Traits::__local(__last));
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_FOR_EACH_SEGMENT_H
