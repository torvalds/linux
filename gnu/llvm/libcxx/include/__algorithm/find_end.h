// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_FIND_END_OF_H
#define _LIBCPP___ALGORITHM_FIND_END_OF_H

#include <__algorithm/comp.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/search.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/advance.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
#include <__iterator/reverse_iterator.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template < class _AlgPolicy,
           class _Iter1,
           class _Sent1,
           class _Iter2,
           class _Sent2,
           class _Pred,
           class _Proj1,
           class _Proj2>
_LIBCPP_HIDE_FROM_ABI inline _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Iter1, _Iter1> __find_end_impl(
    _Iter1 __first1,
    _Sent1 __last1,
    _Iter2 __first2,
    _Sent2 __last2,
    _Pred& __pred,
    _Proj1& __proj1,
    _Proj2& __proj2,
    forward_iterator_tag,
    forward_iterator_tag) {
  // modeled after search algorithm
  _Iter1 __match_first = _IterOps<_AlgPolicy>::next(__first1, __last1); // __last1 is the "default" answer
  _Iter1 __match_last  = __match_first;
  if (__first2 == __last2)
    return pair<_Iter1, _Iter1>(__match_last, __match_last);
  while (true) {
    while (true) {
      if (__first1 == __last1) // if source exhausted return last correct answer (or __last1 if never found)
        return pair<_Iter1, _Iter1>(__match_first, __match_last);
      if (std::__invoke(__pred, std::__invoke(__proj1, *__first1), std::__invoke(__proj2, *__first2)))
        break;
      ++__first1;
    }
    // *__first1 matches *__first2, now match elements after here
    _Iter1 __m1 = __first1;
    _Iter2 __m2 = __first2;
    while (true) {
      if (++__m2 == __last2) { // Pattern exhaused, record answer and search for another one
        __match_first = __first1;
        __match_last  = ++__m1;
        ++__first1;
        break;
      }
      if (++__m1 == __last1) // Source exhausted, return last answer
        return pair<_Iter1, _Iter1>(__match_first, __match_last);
      // mismatch, restart with a new __first
      if (!std::__invoke(__pred, std::__invoke(__proj1, *__m1), std::__invoke(__proj2, *__m2))) {
        ++__first1;
        break;
      } // else there is a match, check next elements
    }
  }
}

template < class _IterOps,
           class _Pred,
           class _Iter1,
           class _Sent1,
           class _Iter2,
           class _Sent2,
           class _Proj1,
           class _Proj2>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Iter1 __find_end(
    _Iter1 __first1,
    _Sent1 __sent1,
    _Iter2 __first2,
    _Sent2 __sent2,
    _Pred& __pred,
    _Proj1& __proj1,
    _Proj2& __proj2,
    bidirectional_iterator_tag,
    bidirectional_iterator_tag) {
  auto __last1 = _IterOps::next(__first1, __sent1);
  auto __last2 = _IterOps::next(__first2, __sent2);
  // modeled after search algorithm (in reverse)
  if (__first2 == __last2)
    return __last1; // Everything matches an empty sequence
  _Iter1 __l1 = __last1;
  _Iter2 __l2 = __last2;
  --__l2;
  while (true) {
    // Find last element in sequence 1 that matchs *(__last2-1), with a mininum of loop checks
    while (true) {
      if (__first1 == __l1) // return __last1 if no element matches *__first2
        return __last1;
      if (std::__invoke(__pred, std::__invoke(__proj1, *--__l1), std::__invoke(__proj2, *__l2)))
        break;
    }
    // *__l1 matches *__l2, now match elements before here
    _Iter1 __m1 = __l1;
    _Iter2 __m2 = __l2;
    while (true) {
      if (__m2 == __first2) // If pattern exhausted, __m1 is the answer (works for 1 element pattern)
        return __m1;
      if (__m1 == __first1) // Otherwise if source exhaused, pattern not found
        return __last1;

      // if there is a mismatch, restart with a new __l1
      if (!std::__invoke(__pred, std::__invoke(__proj1, *--__m1), std::__invoke(__proj2, *--__m2))) {
        break;
      } // else there is a match, check next elements
    }
  }
}

template < class _AlgPolicy,
           class _Pred,
           class _Iter1,
           class _Sent1,
           class _Iter2,
           class _Sent2,
           class _Proj1,
           class _Proj2>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Iter1 __find_end(
    _Iter1 __first1,
    _Sent1 __sent1,
    _Iter2 __first2,
    _Sent2 __sent2,
    _Pred& __pred,
    _Proj1& __proj1,
    _Proj2& __proj2,
    random_access_iterator_tag,
    random_access_iterator_tag) {
  typedef typename iterator_traits<_Iter1>::difference_type _D1;
  auto __last1 = _IterOps<_AlgPolicy>::next(__first1, __sent1);
  auto __last2 = _IterOps<_AlgPolicy>::next(__first2, __sent2);
  // Take advantage of knowing source and pattern lengths.  Stop short when source is smaller than pattern
  auto __len2 = __last2 - __first2;
  if (__len2 == 0)
    return __last1;
  auto __len1 = __last1 - __first1;
  if (__len1 < __len2)
    return __last1;
  const _Iter1 __s = __first1 + _D1(__len2 - 1); // End of pattern match can't go before here
  _Iter1 __l1      = __last1;
  _Iter2 __l2      = __last2;
  --__l2;
  while (true) {
    while (true) {
      if (__s == __l1)
        return __last1;
      if (std::__invoke(__pred, std::__invoke(__proj1, *--__l1), std::__invoke(__proj2, *__l2)))
        break;
    }
    _Iter1 __m1 = __l1;
    _Iter2 __m2 = __l2;
    while (true) {
      if (__m2 == __first2)
        return __m1;
      // no need to check range on __m1 because __s guarantees we have enough source
      if (!std::__invoke(__pred, std::__invoke(__proj1, *--__m1), std::__invoke(*--__m2))) {
        break;
      }
    }
  }
}

template <class _ForwardIterator1, class _ForwardIterator2, class _BinaryPredicate>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _ForwardIterator1 __find_end_classic(
    _ForwardIterator1 __first1,
    _ForwardIterator1 __last1,
    _ForwardIterator2 __first2,
    _ForwardIterator2 __last2,
    _BinaryPredicate& __pred) {
  auto __proj = __identity();
  return std::__find_end_impl<_ClassicAlgPolicy>(
             __first1,
             __last1,
             __first2,
             __last2,
             __pred,
             __proj,
             __proj,
             typename iterator_traits<_ForwardIterator1>::iterator_category(),
             typename iterator_traits<_ForwardIterator2>::iterator_category())
      .first;
}

template <class _ForwardIterator1, class _ForwardIterator2, class _BinaryPredicate>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator1 find_end(
    _ForwardIterator1 __first1,
    _ForwardIterator1 __last1,
    _ForwardIterator2 __first2,
    _ForwardIterator2 __last2,
    _BinaryPredicate __pred) {
  return std::__find_end_classic(__first1, __last1, __first2, __last2, __pred);
}

template <class _ForwardIterator1, class _ForwardIterator2>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator1
find_end(_ForwardIterator1 __first1, _ForwardIterator1 __last1, _ForwardIterator2 __first2, _ForwardIterator2 __last2) {
  return std::find_end(__first1, __last1, __first2, __last2, __equal_to());
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_FIND_END_OF_H
