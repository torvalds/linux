//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___SEGMENTED_ITERATOR_H
#define _LIBCPP___SEGMENTED_ITERATOR_H

// Segmented iterators are iterators over (not necessarily contiguous) sub-ranges.
//
// For example, std::deque stores its data into multiple blocks of contiguous memory,
// which are not stored contiguously themselves. The concept of segmented iterators
// allows algorithms to operate over these multi-level iterators natively, opening the
// door to various optimizations. See http://lafstern.org/matt/segmented.pdf for details.
//
// If __segmented_iterator_traits can be instantiated, the following functions and associated types must be provided:
// - Traits::__local_iterator
//   The type of iterators used to iterate inside a segment.
//
// - Traits::__segment_iterator
//   The type of iterators used to iterate over segments.
//   Segment iterators can be forward iterators or bidirectional iterators, depending on the
//   underlying data structure.
//
// - static __segment_iterator Traits::__segment(It __it)
//   Returns an iterator to the segment that the provided iterator is in.
//
// - static __local_iterator Traits::__local(It __it)
//   Returns the local iterator pointing to the element that the provided iterator points to.
//
// - static __local_iterator Traits::__begin(__segment_iterator __it)
//   Returns the local iterator to the beginning of the segment that the provided iterator is pointing into.
//
// - static __local_iterator Traits::__end(__segment_iterator __it)
//   Returns the one-past-the-end local iterator to the segment that the provided iterator is pointing into.
//
// - static It Traits::__compose(__segment_iterator, __local_iterator)
//   Returns the iterator composed of the segment iterator and local iterator.

#include <__config>
#include <__type_traits/integral_constant.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Iterator>
struct __segmented_iterator_traits;
/* exposition-only:
{
  using __segment_iterator = ...;
  using __local_iterator   = ...;

  static __segment_iterator __segment(_Iterator);
  static __local_iterator __local(_Iterator);
  static __local_iterator __begin(__segment_iterator);
  static __local_iterator __end(__segment_iterator);
  static _Iterator __compose(__segment_iterator, __local_iterator);
};
*/

template <class _Tp, size_t = 0>
struct __has_specialization : false_type {};

template <class _Tp>
struct __has_specialization<_Tp, sizeof(_Tp) * 0> : true_type {};

template <class _Iterator>
using __is_segmented_iterator = __has_specialization<__segmented_iterator_traits<_Iterator> >;

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___SEGMENTED_ITERATOR_H
