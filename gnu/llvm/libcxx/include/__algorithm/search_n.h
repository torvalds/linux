// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_SEARCH_N_H
#define _LIBCPP___ALGORITHM_SEARCH_N_H

#include <__algorithm/comp.h>
#include <__algorithm/iterator_operations.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__iterator/advance.h>
#include <__iterator/concepts.h>
#include <__iterator/distance.h>
#include <__iterator/iterator_traits.h>
#include <__ranges/concepts.h>
#include <__type_traits/is_callable.h>
#include <__utility/convert_to_integral.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _AlgPolicy, class _Pred, class _Iter, class _Sent, class _SizeT, class _Type, class _Proj>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Iter, _Iter> __search_n_forward_impl(
    _Iter __first, _Sent __last, _SizeT __count, const _Type& __value, _Pred& __pred, _Proj& __proj) {
  if (__count <= 0)
    return std::make_pair(__first, __first);
  while (true) {
    // Find first element in sequence that matchs __value, with a mininum of loop checks
    while (true) {
      if (__first == __last) { // return __last if no element matches __value
        _IterOps<_AlgPolicy>::__advance_to(__first, __last);
        return std::make_pair(__first, __first);
      }
      if (std::__invoke(__pred, std::__invoke(__proj, *__first), __value))
        break;
      ++__first;
    }
    // *__first matches __value, now match elements after here
    _Iter __m = __first;
    _SizeT __c(0);
    while (true) {
      if (++__c == __count) // If pattern exhausted, __first is the answer (works for 1 element pattern)
        return std::make_pair(__first, ++__m);
      if (++__m == __last) { // Otherwise if source exhaused, pattern not found
        _IterOps<_AlgPolicy>::__advance_to(__first, __last);
        return std::make_pair(__first, __first);
      }

      // if there is a mismatch, restart with a new __first
      if (!std::__invoke(__pred, std::__invoke(__proj, *__m), __value)) {
        __first = __m;
        ++__first;
        break;
      } // else there is a match, check next elements
    }
  }
}

template <class _AlgPolicy, class _Pred, class _Iter, class _Sent, class _SizeT, class _Type, class _Proj, class _DiffT>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 std::pair<_Iter, _Iter> __search_n_random_access_impl(
    _Iter __first, _Sent __last, _SizeT __count, const _Type& __value, _Pred& __pred, _Proj& __proj, _DiffT __size1) {
  using difference_type = typename iterator_traits<_Iter>::difference_type;
  if (__count == 0)
    return std::make_pair(__first, __first);
  if (__size1 < static_cast<_DiffT>(__count)) {
    _IterOps<_AlgPolicy>::__advance_to(__first, __last);
    return std::make_pair(__first, __first);
  }

  const auto __s = __first + __size1 - difference_type(__count - 1); // Start of pattern match can't go beyond here
  while (true) {
    // Find first element in sequence that matchs __value, with a mininum of loop checks
    while (true) {
      if (__first >= __s) { // return __last if no element matches __value
        _IterOps<_AlgPolicy>::__advance_to(__first, __last);
        return std::make_pair(__first, __first);
      }
      if (std::__invoke(__pred, std::__invoke(__proj, *__first), __value))
        break;
      ++__first;
    }
    // *__first matches __value_, now match elements after here
    auto __m = __first;
    _SizeT __c(0);
    while (true) {
      if (++__c == __count) // If pattern exhausted, __first is the answer (works for 1 element pattern)
        return std::make_pair(__first, __first + _DiffT(__count));
      ++__m; // no need to check range on __m because __s guarantees we have enough source

      // if there is a mismatch, restart with a new __first
      if (!std::__invoke(__pred, std::__invoke(__proj, *__m), __value)) {
        __first = __m;
        ++__first;
        break;
      } // else there is a match, check next elements
    }
  }
}

template <class _Iter,
          class _Sent,
          class _DiffT,
          class _Type,
          class _Pred,
          class _Proj,
          __enable_if_t<__has_random_access_iterator_category<_Iter>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Iter, _Iter>
__search_n_impl(_Iter __first, _Sent __last, _DiffT __count, const _Type& __value, _Pred& __pred, _Proj& __proj) {
  return std::__search_n_random_access_impl<_ClassicAlgPolicy>(
      __first, __last, __count, __value, __pred, __proj, __last - __first);
}

template <class _Iter1,
          class _Sent1,
          class _DiffT,
          class _Type,
          class _Pred,
          class _Proj,
          __enable_if_t<__has_forward_iterator_category<_Iter1>::value &&
                            !__has_random_access_iterator_category<_Iter1>::value,
                        int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Iter1, _Iter1>
__search_n_impl(_Iter1 __first, _Sent1 __last, _DiffT __count, const _Type& __value, _Pred& __pred, _Proj& __proj) {
  return std::__search_n_forward_impl<_ClassicAlgPolicy>(__first, __last, __count, __value, __pred, __proj);
}

template <class _ForwardIterator, class _Size, class _Tp, class _BinaryPredicate>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator search_n(
    _ForwardIterator __first, _ForwardIterator __last, _Size __count, const _Tp& __value, _BinaryPredicate __pred) {
  static_assert(
      __is_callable<_BinaryPredicate, decltype(*__first), const _Tp&>::value, "BinaryPredicate has to be callable");
  auto __proj = __identity();
  return std::__search_n_impl(__first, __last, std::__convert_to_integral(__count), __value, __pred, __proj).first;
}

template <class _ForwardIterator, class _Size, class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator
search_n(_ForwardIterator __first, _ForwardIterator __last, _Size __count, const _Tp& __value) {
  return std::search_n(__first, __last, std::__convert_to_integral(__count), __value, __equal_to());
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ALGORITHM_SEARCH_N_H
