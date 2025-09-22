//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_ITERATOR_OPERATIONS_H
#define _LIBCPP___ALGORITHM_ITERATOR_OPERATIONS_H

#include <__algorithm/iter_swap.h>
#include <__algorithm/ranges_iterator_concept.h>
#include <__assert>
#include <__config>
#include <__iterator/advance.h>
#include <__iterator/distance.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iter_move.h>
#include <__iterator/iter_swap.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
#include <__iterator/prev.h>
#include <__iterator/readable_traits.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/is_same.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/declval.h>
#include <__utility/forward.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _AlgPolicy>
struct _IterOps;

#if _LIBCPP_STD_VER >= 20
struct _RangeAlgPolicy {};

template <>
struct _IterOps<_RangeAlgPolicy> {
  template <class _Iter>
  using __value_type = iter_value_t<_Iter>;

  template <class _Iter>
  using __iterator_category = ranges::__iterator_concept<_Iter>;

  template <class _Iter>
  using __difference_type = iter_difference_t<_Iter>;

  static constexpr auto advance      = ranges::advance;
  static constexpr auto distance     = ranges::distance;
  static constexpr auto __iter_move  = ranges::iter_move;
  static constexpr auto iter_swap    = ranges::iter_swap;
  static constexpr auto next         = ranges::next;
  static constexpr auto prev         = ranges::prev;
  static constexpr auto __advance_to = ranges::advance;
};

#endif

struct _ClassicAlgPolicy {};

template <>
struct _IterOps<_ClassicAlgPolicy> {
  template <class _Iter>
  using __value_type = typename iterator_traits<_Iter>::value_type;

  template <class _Iter>
  using __iterator_category = typename iterator_traits<_Iter>::iterator_category;

  template <class _Iter>
  using __difference_type = typename iterator_traits<_Iter>::difference_type;

  // advance
  template <class _Iter, class _Distance>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 static void advance(_Iter& __iter, _Distance __count) {
    std::advance(__iter, __count);
  }

  // distance
  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 static typename iterator_traits<_Iter>::difference_type
  distance(_Iter __first, _Iter __last) {
    return std::distance(__first, __last);
  }

  template <class _Iter>
  using __deref_t = decltype(*std::declval<_Iter&>());

  template <class _Iter>
  using __move_t = decltype(std::move(*std::declval<_Iter&>()));

  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 static void __validate_iter_reference() {
    static_assert(
        is_same<__deref_t<_Iter>, typename iterator_traits<__remove_cvref_t<_Iter> >::reference>::value,
        "It looks like your iterator's `iterator_traits<It>::reference` does not match the return type of "
        "dereferencing the iterator, i.e., calling `*it`. This is undefined behavior according to [input.iterators] "
        "and can lead to dangling reference issues at runtime, so we are flagging this.");
  }

  // iter_move
  template <class _Iter, __enable_if_t<is_reference<__deref_t<_Iter> >::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 static
      // If the result of dereferencing `_Iter` is a reference type, deduce the result of calling `std::move` on it.
      // Note that the C++03 mode doesn't support `decltype(auto)` as the return type.
      __move_t<_Iter>
      __iter_move(_Iter&& __i) {
    __validate_iter_reference<_Iter>();

    return std::move(*std::forward<_Iter>(__i));
  }

  template <class _Iter, __enable_if_t<!is_reference<__deref_t<_Iter> >::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 static
      // If the result of dereferencing `_Iter` is a value type, deduce the return value of this function to also be a
      // value -- otherwise, after `operator*` returns a temporary, this function would return a dangling reference to
      // that temporary. Note that the C++03 mode doesn't support `auto` as the return type.
      __deref_t<_Iter>
      __iter_move(_Iter&& __i) {
    __validate_iter_reference<_Iter>();

    return *std::forward<_Iter>(__i);
  }

  // iter_swap
  template <class _Iter1, class _Iter2>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 static void iter_swap(_Iter1&& __a, _Iter2&& __b) {
    std::iter_swap(std::forward<_Iter1>(__a), std::forward<_Iter2>(__b));
  }

  // next
  template <class _Iterator>
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX14 _Iterator next(_Iterator, _Iterator __last) {
    return __last;
  }

  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX14 __remove_cvref_t<_Iter>
  next(_Iter&& __it, typename iterator_traits<__remove_cvref_t<_Iter> >::difference_type __n = 1) {
    return std::next(std::forward<_Iter>(__it), __n);
  }

  // prev
  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX14 __remove_cvref_t<_Iter>
  prev(_Iter&& __iter, typename iterator_traits<__remove_cvref_t<_Iter> >::difference_type __n = 1) {
    return std::prev(std::forward<_Iter>(__iter), __n);
  }

  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX14 void __advance_to(_Iter& __first, _Iter __last) {
    __first = __last;
  }

  // advance with sentinel, a la std::ranges::advance
  template <class _Iter>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 static __difference_type<_Iter>
  __advance_to(_Iter& __iter, __difference_type<_Iter> __count, const _Iter& __sentinel) {
    return _IterOps::__advance_to(__iter, __count, __sentinel, typename iterator_traits<_Iter>::iterator_category());
  }

private:
  // advance with sentinel, a la std::ranges::advance -- InputIterator specialization
  template <class _InputIter>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 static __difference_type<_InputIter> __advance_to(
      _InputIter& __iter, __difference_type<_InputIter> __count, const _InputIter& __sentinel, input_iterator_tag) {
    __difference_type<_InputIter> __dist = 0;
    for (; __dist < __count && __iter != __sentinel; ++__dist)
      ++__iter;
    return __count - __dist;
  }

  // advance with sentinel, a la std::ranges::advance -- BidirectionalIterator specialization
  template <class _BiDirIter>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 static __difference_type<_BiDirIter>
  __advance_to(_BiDirIter& __iter,
               __difference_type<_BiDirIter> __count,
               const _BiDirIter& __sentinel,
               bidirectional_iterator_tag) {
    __difference_type<_BiDirIter> __dist = 0;
    if (__count >= 0)
      for (; __dist < __count && __iter != __sentinel; ++__dist)
        ++__iter;
    else
      for (__count = -__count; __dist < __count && __iter != __sentinel; ++__dist)
        --__iter;
    return __count - __dist;
  }

  // advance with sentinel, a la std::ranges::advance -- RandomIterator specialization
  template <class _RandIter>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 static __difference_type<_RandIter>
  __advance_to(_RandIter& __iter,
               __difference_type<_RandIter> __count,
               const _RandIter& __sentinel,
               random_access_iterator_tag) {
    auto __dist = _IterOps::distance(__iter, __sentinel);
    _LIBCPP_ASSERT_VALID_INPUT_RANGE(
        __count == 0 || (__dist < 0) == (__count < 0), "__sentinel must precede __iter when __count < 0");
    if (__count < 0)
      __dist = __dist > __count ? __dist : __count;
    else
      __dist = __dist < __count ? __dist : __count;
    __iter += __dist;
    return __count - __dist;
  }
};

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_ITERATOR_OPERATIONS_H
