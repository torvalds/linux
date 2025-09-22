// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_IS_PERMUTATION_H
#define _LIBCPP___ALGORITHM_IS_PERMUTATION_H

#include <__algorithm/comp.h>
#include <__algorithm/iterator_operations.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/concepts.h>
#include <__iterator/distance.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
#include <__type_traits/is_callable.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Iter1, class _Sent1, class _Iter2, class _Sent2, class = void>
struct _ConstTimeDistance : false_type {};

#if _LIBCPP_STD_VER >= 20

template <class _Iter1, class _Sent1, class _Iter2, class _Sent2>
struct _ConstTimeDistance<_Iter1,
                          _Sent1,
                          _Iter2,
                          _Sent2,
                          __enable_if_t< sized_sentinel_for<_Sent1, _Iter1> && sized_sentinel_for<_Sent2, _Iter2> >>
    : true_type {};

#else

template <class _Iter1, class _Iter2>
struct _ConstTimeDistance<
    _Iter1,
    _Iter1,
    _Iter2,
    _Iter2,
    __enable_if_t< is_same<typename iterator_traits<_Iter1>::iterator_category, random_access_iterator_tag>::value &&
                   is_same<typename iterator_traits<_Iter2>::iterator_category, random_access_iterator_tag>::value > >
    : true_type {};

#endif // _LIBCPP_STD_VER >= 20

// Internal functions

// For each element in [f1, l1) see if there are the same number of equal elements in [f2, l2)
template <class _AlgPolicy,
          class _Iter1,
          class _Sent1,
          class _Iter2,
          class _Sent2,
          class _Proj1,
          class _Proj2,
          class _Pred>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool __is_permutation_impl(
    _Iter1 __first1,
    _Sent1 __last1,
    _Iter2 __first2,
    _Sent2 __last2,
    _Pred&& __pred,
    _Proj1&& __proj1,
    _Proj2&& __proj2) {
  using _D1 = __iter_diff_t<_Iter1>;

  for (auto __i = __first1; __i != __last1; ++__i) {
    //  Have we already counted the number of *__i in [f1, l1)?
    auto __match = __first1;
    for (; __match != __i; ++__match) {
      if (std::__invoke(__pred, std::__invoke(__proj1, *__match), std::__invoke(__proj1, *__i)))
        break;
    }

    if (__match == __i) {
      // Count number of *__i in [f2, l2)
      _D1 __c2 = 0;
      for (auto __j = __first2; __j != __last2; ++__j) {
        if (std::__invoke(__pred, std::__invoke(__proj1, *__i), std::__invoke(__proj2, *__j)))
          ++__c2;
      }
      if (__c2 == 0)
        return false;

      // Count number of *__i in [__i, l1) (we can start with 1)
      _D1 __c1 = 1;
      for (auto __j = _IterOps<_AlgPolicy>::next(__i); __j != __last1; ++__j) {
        if (std::__invoke(__pred, std::__invoke(__proj1, *__i), std::__invoke(__proj1, *__j)))
          ++__c1;
      }
      if (__c1 != __c2)
        return false;
    }
  }

  return true;
}

// 2+1 iterators, predicate. Not used by range algorithms.
template <class _AlgPolicy, class _ForwardIterator1, class _Sentinel1, class _ForwardIterator2, class _BinaryPredicate>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool __is_permutation(
    _ForwardIterator1 __first1, _Sentinel1 __last1, _ForwardIterator2 __first2, _BinaryPredicate&& __pred) {
  // Shorten sequences as much as possible by lopping of any equal prefix.
  for (; __first1 != __last1; ++__first1, (void)++__first2) {
    if (!__pred(*__first1, *__first2))
      break;
  }

  if (__first1 == __last1)
    return true;

  //  __first1 != __last1 && *__first1 != *__first2
  using _D1 = __iter_diff_t<_ForwardIterator1>;
  _D1 __l1  = _IterOps<_AlgPolicy>::distance(__first1, __last1);
  if (__l1 == _D1(1))
    return false;
  auto __last2 = _IterOps<_AlgPolicy>::next(__first2, __l1);

  return std::__is_permutation_impl<_AlgPolicy>(
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      __pred,
      __identity(),
      __identity());
}

// 2+2 iterators, predicate, non-constant time `distance`.
template <class _AlgPolicy,
          class _Iter1,
          class _Sent1,
          class _Iter2,
          class _Sent2,
          class _Proj1,
          class _Proj2,
          class _Pred>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool __is_permutation(
    _Iter1 __first1,
    _Sent1 __last1,
    _Iter2 __first2,
    _Sent2 __last2,
    _Pred&& __pred,
    _Proj1&& __proj1,
    _Proj2&& __proj2,
    /*_ConstTimeDistance=*/false_type) {
  // Shorten sequences as much as possible by lopping of any equal prefix.
  while (__first1 != __last1 && __first2 != __last2) {
    if (!std::__invoke(__pred, std::__invoke(__proj1, *__first1), std::__invoke(__proj2, *__first2)))
      break;
    ++__first1;
    ++__first2;
  }

  if (__first1 == __last1)
    return __first2 == __last2;
  if (__first2 == __last2) // Second range is shorter
    return false;

  using _D1 = __iter_diff_t<_Iter1>;
  _D1 __l1  = _IterOps<_AlgPolicy>::distance(__first1, __last1);

  using _D2 = __iter_diff_t<_Iter2>;
  _D2 __l2  = _IterOps<_AlgPolicy>::distance(__first2, __last2);
  if (__l1 != __l2)
    return false;

  return std::__is_permutation_impl<_AlgPolicy>(
      std::move(__first1), std::move(__last1), std::move(__first2), std::move(__last2), __pred, __proj1, __proj2);
}

// 2+2 iterators, predicate, specialization for constant-time `distance` call.
template <class _AlgPolicy,
          class _Iter1,
          class _Sent1,
          class _Iter2,
          class _Sent2,
          class _Proj1,
          class _Proj2,
          class _Pred>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool __is_permutation(
    _Iter1 __first1,
    _Sent1 __last1,
    _Iter2 __first2,
    _Sent2 __last2,
    _Pred&& __pred,
    _Proj1&& __proj1,
    _Proj2&& __proj2,
    /*_ConstTimeDistance=*/true_type) {
  if (std::distance(__first1, __last1) != std::distance(__first2, __last2))
    return false;
  return std::__is_permutation<_AlgPolicy>(
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      __pred,
      __proj1,
      __proj2,
      /*_ConstTimeDistance=*/false_type());
}

// 2+2 iterators, predicate
template <class _AlgPolicy,
          class _Iter1,
          class _Sent1,
          class _Iter2,
          class _Sent2,
          class _Proj1,
          class _Proj2,
          class _Pred>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool __is_permutation(
    _Iter1 __first1,
    _Sent1 __last1,
    _Iter2 __first2,
    _Sent2 __last2,
    _Pred&& __pred,
    _Proj1&& __proj1,
    _Proj2&& __proj2) {
  return std::__is_permutation<_AlgPolicy>(
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      __pred,
      __proj1,
      __proj2,
      _ConstTimeDistance<_Iter1, _Sent1, _Iter2, _Sent2>());
}

// Public interface

// 2+1 iterators, predicate
template <class _ForwardIterator1, class _ForwardIterator2, class _BinaryPredicate>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool is_permutation(
    _ForwardIterator1 __first1, _ForwardIterator1 __last1, _ForwardIterator2 __first2, _BinaryPredicate __pred) {
  static_assert(__is_callable<_BinaryPredicate, decltype(*__first1), decltype(*__first2)>::value,
                "The predicate has to be callable");

  return std::__is_permutation<_ClassicAlgPolicy>(std::move(__first1), std::move(__last1), std::move(__first2), __pred);
}

// 2+1 iterators
template <class _ForwardIterator1, class _ForwardIterator2>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool
is_permutation(_ForwardIterator1 __first1, _ForwardIterator1 __last1, _ForwardIterator2 __first2) {
  return std::is_permutation(__first1, __last1, __first2, __equal_to());
}

#if _LIBCPP_STD_VER >= 14

// 2+2 iterators
template <class _ForwardIterator1, class _ForwardIterator2>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool is_permutation(
    _ForwardIterator1 __first1, _ForwardIterator1 __last1, _ForwardIterator2 __first2, _ForwardIterator2 __last2) {
  return std::__is_permutation<_ClassicAlgPolicy>(
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      __equal_to(),
      __identity(),
      __identity());
}

// 2+2 iterators, predicate
template <class _ForwardIterator1, class _ForwardIterator2, class _BinaryPredicate>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 bool is_permutation(
    _ForwardIterator1 __first1,
    _ForwardIterator1 __last1,
    _ForwardIterator2 __first2,
    _ForwardIterator2 __last2,
    _BinaryPredicate __pred) {
  static_assert(__is_callable<_BinaryPredicate, decltype(*__first1), decltype(*__first2)>::value,
                "The predicate has to be callable");

  return std::__is_permutation<_ClassicAlgPolicy>(
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      __pred,
      __identity(),
      __identity());
}

#endif // _LIBCPP_STD_VER >= 14

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_IS_PERMUTATION_H
