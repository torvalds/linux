// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_SEARCH_H
#define _LIBCPP___ALGORITHM_SEARCH_H

#include <__algorithm/comp.h>
#include <__algorithm/iterator_operations.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/advance.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_callable.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _AlgPolicy,
          class _Iter1,
          class _Sent1,
          class _Iter2,
          class _Sent2,
          class _Pred,
          class _Proj1,
          class _Proj2>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Iter1, _Iter1> __search_forward_impl(
    _Iter1 __first1, _Sent1 __last1, _Iter2 __first2, _Sent2 __last2, _Pred& __pred, _Proj1& __proj1, _Proj2& __proj2) {
  if (__first2 == __last2)
    return std::make_pair(__first1, __first1); // Everything matches an empty sequence
  while (true) {
    // Find first element in sequence 1 that matchs *__first2, with a mininum of loop checks
    while (true) {
      if (__first1 == __last1) { // return __last1 if no element matches *__first2
        _IterOps<_AlgPolicy>::__advance_to(__first1, __last1);
        return std::make_pair(__first1, __first1);
      }
      if (std::__invoke(__pred, std::__invoke(__proj1, *__first1), std::__invoke(__proj2, *__first2)))
        break;
      ++__first1;
    }
    // *__first1 matches *__first2, now match elements after here
    _Iter1 __m1 = __first1;
    _Iter2 __m2 = __first2;
    while (true) {
      if (++__m2 == __last2) // If pattern exhausted, __first1 is the answer (works for 1 element pattern)
        return std::make_pair(__first1, ++__m1);
      if (++__m1 == __last1) { // Otherwise if source exhaused, pattern not found
        return std::make_pair(__m1, __m1);
      }

      // if there is a mismatch, restart with a new __first1
      if (!std::__invoke(__pred, std::__invoke(__proj1, *__m1), std::__invoke(__proj2, *__m2))) {
        ++__first1;
        break;
      } // else there is a match, check next elements
    }
  }
}

template <class _AlgPolicy,
          class _Iter1,
          class _Sent1,
          class _Iter2,
          class _Sent2,
          class _Pred,
          class _Proj1,
          class _Proj2,
          class _DiffT1,
          class _DiffT2>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Iter1, _Iter1> __search_random_access_impl(
    _Iter1 __first1,
    _Sent1 __last1,
    _Iter2 __first2,
    _Sent2 __last2,
    _Pred& __pred,
    _Proj1& __proj1,
    _Proj2& __proj2,
    _DiffT1 __size1,
    _DiffT2 __size2) {
  const _Iter1 __s = __first1 + __size1 - _DiffT1(__size2 - 1); // Start of pattern match can't go beyond here

  while (true) {
    while (true) {
      if (__first1 == __s) {
        _IterOps<_AlgPolicy>::__advance_to(__first1, __last1);
        return std::make_pair(__first1, __first1);
      }
      if (std::__invoke(__pred, std::__invoke(__proj1, *__first1), std::__invoke(__proj2, *__first2)))
        break;
      ++__first1;
    }

    _Iter1 __m1 = __first1;
    _Iter2 __m2 = __first2;
    while (true) {
      if (++__m2 == __last2)
        return std::make_pair(__first1, __first1 + _DiffT1(__size2));
      ++__m1; // no need to check range on __m1 because __s guarantees we have enough source
      if (!std::__invoke(__pred, std::__invoke(__proj1, *__m1), std::__invoke(__proj2, *__m2))) {
        ++__first1;
        break;
      }
    }
  }
}

template <class _Iter1,
          class _Sent1,
          class _Iter2,
          class _Sent2,
          class _Pred,
          class _Proj1,
          class _Proj2,
          __enable_if_t<__has_random_access_iterator_category<_Iter1>::value &&
                            __has_random_access_iterator_category<_Iter2>::value,
                        int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Iter1, _Iter1> __search_impl(
    _Iter1 __first1, _Sent1 __last1, _Iter2 __first2, _Sent2 __last2, _Pred& __pred, _Proj1& __proj1, _Proj2& __proj2) {
  auto __size2 = __last2 - __first2;
  if (__size2 == 0)
    return std::make_pair(__first1, __first1);

  auto __size1 = __last1 - __first1;
  if (__size1 < __size2) {
    return std::make_pair(__last1, __last1);
  }

  return std::__search_random_access_impl<_ClassicAlgPolicy>(
      __first1, __last1, __first2, __last2, __pred, __proj1, __proj2, __size1, __size2);
}

template <
    class _Iter1,
    class _Sent1,
    class _Iter2,
    class _Sent2,
    class _Pred,
    class _Proj1,
    class _Proj2,
    __enable_if_t<__has_forward_iterator_category<_Iter1>::value && __has_forward_iterator_category<_Iter2>::value &&
                      !(__has_random_access_iterator_category<_Iter1>::value &&
                        __has_random_access_iterator_category<_Iter2>::value),
                  int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Iter1, _Iter1> __search_impl(
    _Iter1 __first1, _Sent1 __last1, _Iter2 __first2, _Sent2 __last2, _Pred& __pred, _Proj1& __proj1, _Proj2& __proj2) {
  return std::__search_forward_impl<_ClassicAlgPolicy>(__first1, __last1, __first2, __last2, __pred, __proj1, __proj2);
}

template <class _ForwardIterator1, class _ForwardIterator2, class _BinaryPredicate>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator1
search(_ForwardIterator1 __first1,
       _ForwardIterator1 __last1,
       _ForwardIterator2 __first2,
       _ForwardIterator2 __last2,
       _BinaryPredicate __pred) {
  static_assert(__is_callable<_BinaryPredicate, decltype(*__first1), decltype(*__first2)>::value,
                "BinaryPredicate has to be callable");
  auto __proj = __identity();
  return std::__search_impl(__first1, __last1, __first2, __last2, __pred, __proj, __proj).first;
}

template <class _ForwardIterator1, class _ForwardIterator2>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator1
search(_ForwardIterator1 __first1, _ForwardIterator1 __last1, _ForwardIterator2 __first2, _ForwardIterator2 __last2) {
  return std::search(__first1, __last1, __first2, __last2, __equal_to());
}

#if _LIBCPP_STD_VER >= 17
template <class _ForwardIterator, class _Searcher>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator
search(_ForwardIterator __f, _ForwardIterator __l, const _Searcher& __s) {
  return __s(__f, __l).first;
}

#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_SEARCH_H
