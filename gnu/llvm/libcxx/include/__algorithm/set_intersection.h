//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_SET_INTERSECTION_H
#define _LIBCPP___ALGORITHM_SET_INTERSECTION_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/lower_bound.h>
#include <__config>
#include <__functional/identity.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
#include <__type_traits/is_same.h>
#include <__utility/exchange.h>
#include <__utility/move.h>
#include <__utility/swap.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _InIter1, class _InIter2, class _OutIter>
struct __set_intersection_result {
  _InIter1 __in1_;
  _InIter2 __in2_;
  _OutIter __out_;

  // need a constructor as C++03 aggregate init is hard
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20
  __set_intersection_result(_InIter1&& __in_iter1, _InIter2&& __in_iter2, _OutIter&& __out_iter)
      : __in1_(std::move(__in_iter1)), __in2_(std::move(__in_iter2)), __out_(std::move(__out_iter)) {}
};

// Helper for __set_intersection() with one-sided binary search: populate result and advance input iterators if they
// are found to potentially contain the same value in two consecutive calls. This function is very intimately related to
// the way it is used and doesn't attempt to abstract that, it's not appropriate for general usage outside of its
// context.
template <class _InForwardIter1, class _InForwardIter2, class _OutIter>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void __set_intersection_add_output_if_equal(
    bool __may_be_equal,
    _InForwardIter1& __first1,
    _InForwardIter2& __first2,
    _OutIter& __result,
    bool& __prev_may_be_equal) {
  if (__may_be_equal && __prev_may_be_equal) {
    *__result = *__first1;
    ++__result;
    ++__first1;
    ++__first2;
    __prev_may_be_equal = false;
  } else {
    __prev_may_be_equal = __may_be_equal;
  }
}

// With forward iterators we can make multiple passes over the data, allowing the use of one-sided binary search to
// reduce best-case complexity to log(N). Understanding how we can use binary search and still respect complexity
// guarantees is _not_ straightforward: the guarantee is "at most 2*(N+M)-1 comparisons", and one-sided binary search
// will necessarily overshoot depending on the position of the needle in the haystack -- for instance, if we're
// searching for 3 in (1, 2, 3, 4), we'll check if 3<1, then 3<2, then 3<4, and, finally, 3<3, for a total of 4
// comparisons, when linear search would have yielded 3. However, because we won't need to perform the intervening
// reciprocal comparisons (ie 1<3, 2<3, 4<3), that extra comparison doesn't run afoul of the guarantee. Additionally,
// this type of scenario can only happen for match distances of up to 5 elements, because 2*log2(8) is 6, and we'll
// still be worse-off at position 5 of an 8-element set. From then onwards these scenarios can't happen. TL;DR: we'll be
// 1 comparison worse-off compared to the classic linear-searching algorithm if matching position 3 of a set with 4
// elements, or position 5 if the set has 7 or 8 elements, but we'll never exceed the complexity guarantees from the
// standard.
template <class _AlgPolicy,
          class _Compare,
          class _InForwardIter1,
          class _Sent1,
          class _InForwardIter2,
          class _Sent2,
          class _OutIter>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI
_LIBCPP_CONSTEXPR_SINCE_CXX20 __set_intersection_result<_InForwardIter1, _InForwardIter2, _OutIter>
__set_intersection(
    _InForwardIter1 __first1,
    _Sent1 __last1,
    _InForwardIter2 __first2,
    _Sent2 __last2,
    _OutIter __result,
    _Compare&& __comp,
    std::forward_iterator_tag,
    std::forward_iterator_tag) {
  _LIBCPP_CONSTEXPR std::__identity __proj;
  bool __prev_may_be_equal = false;

  while (__first2 != __last2) {
    _InForwardIter1 __first1_next =
        std::__lower_bound_onesided<_AlgPolicy>(__first1, __last1, *__first2, __comp, __proj);
    std::swap(__first1_next, __first1);
    // keeping in mind that a==b iff !(a<b) && !(b<a):
    // if we can't advance __first1, that means !(*__first1 < *_first2), therefore __may_be_equal==true
    std::__set_intersection_add_output_if_equal(
        __first1 == __first1_next, __first1, __first2, __result, __prev_may_be_equal);
    if (__first1 == __last1)
      break;

    _InForwardIter2 __first2_next =
        std::__lower_bound_onesided<_AlgPolicy>(__first2, __last2, *__first1, __comp, __proj);
    std::swap(__first2_next, __first2);
    std::__set_intersection_add_output_if_equal(
        __first2 == __first2_next, __first1, __first2, __result, __prev_may_be_equal);
  }
  return __set_intersection_result<_InForwardIter1, _InForwardIter2, _OutIter>(
      _IterOps<_AlgPolicy>::next(std::move(__first1), std::move(__last1)),
      _IterOps<_AlgPolicy>::next(std::move(__first2), std::move(__last2)),
      std::move(__result));
}

// input iterators are not suitable for multipass algorithms, so we stick to the classic single-pass version
template <class _AlgPolicy,
          class _Compare,
          class _InInputIter1,
          class _Sent1,
          class _InInputIter2,
          class _Sent2,
          class _OutIter>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI
_LIBCPP_CONSTEXPR_SINCE_CXX20 __set_intersection_result<_InInputIter1, _InInputIter2, _OutIter>
__set_intersection(
    _InInputIter1 __first1,
    _Sent1 __last1,
    _InInputIter2 __first2,
    _Sent2 __last2,
    _OutIter __result,
    _Compare&& __comp,
    std::input_iterator_tag,
    std::input_iterator_tag) {
  while (__first1 != __last1 && __first2 != __last2) {
    if (__comp(*__first1, *__first2))
      ++__first1;
    else {
      if (!__comp(*__first2, *__first1)) {
        *__result = *__first1;
        ++__result;
        ++__first1;
      }
      ++__first2;
    }
  }

  return __set_intersection_result<_InInputIter1, _InInputIter2, _OutIter>(
      _IterOps<_AlgPolicy>::next(std::move(__first1), std::move(__last1)),
      _IterOps<_AlgPolicy>::next(std::move(__first2), std::move(__last2)),
      std::move(__result));
}

template <class _AlgPolicy, class _Compare, class _InIter1, class _Sent1, class _InIter2, class _Sent2, class _OutIter>
_LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI
_LIBCPP_CONSTEXPR_SINCE_CXX20 __set_intersection_result<_InIter1, _InIter2, _OutIter>
__set_intersection(
    _InIter1 __first1, _Sent1 __last1, _InIter2 __first2, _Sent2 __last2, _OutIter __result, _Compare&& __comp) {
  return std::__set_intersection<_AlgPolicy>(
      std::move(__first1),
      std::move(__last1),
      std::move(__first2),
      std::move(__last2),
      std::move(__result),
      std::forward<_Compare>(__comp),
      typename std::_IterOps<_AlgPolicy>::template __iterator_category<_InIter1>(),
      typename std::_IterOps<_AlgPolicy>::template __iterator_category<_InIter2>());
}

template <class _InputIterator1, class _InputIterator2, class _OutputIterator, class _Compare>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator set_intersection(
    _InputIterator1 __first1,
    _InputIterator1 __last1,
    _InputIterator2 __first2,
    _InputIterator2 __last2,
    _OutputIterator __result,
    _Compare __comp) {
  return std::__set_intersection<_ClassicAlgPolicy, __comp_ref_type<_Compare> >(
             std::move(__first1),
             std::move(__last1),
             std::move(__first2),
             std::move(__last2),
             std::move(__result),
             __comp)
      .__out_;
}

template <class _InputIterator1, class _InputIterator2, class _OutputIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator set_intersection(
    _InputIterator1 __first1,
    _InputIterator1 __last1,
    _InputIterator2 __first2,
    _InputIterator2 __last2,
    _OutputIterator __result) {
  return std::__set_intersection<_ClassicAlgPolicy>(
             std::move(__first1),
             std::move(__last1),
             std::move(__first2),
             std::move(__last2),
             std::move(__result),
             __less<>())
      .__out_;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_SET_INTERSECTION_H
