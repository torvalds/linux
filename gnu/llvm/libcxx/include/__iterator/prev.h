// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_PREV_H
#define _LIBCPP___ITERATOR_PREV_H

#include <__assert>
#include <__config>
#include <__iterator/advance.h>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iterator_traits.h>
#include <__type_traits/enable_if.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _InputIter, __enable_if_t<__has_input_iterator_category<_InputIter>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 _InputIter
prev(_InputIter __x, typename iterator_traits<_InputIter>::difference_type __n = 1) {
  // Calling `advance` with a negative value on a non-bidirectional iterator is a no-op in the current implementation.
  // Note that this check duplicates the similar check in `std::advance`.
  _LIBCPP_ASSERT_PEDANTIC(__n <= 0 || __has_bidirectional_iterator_category<_InputIter>::value,
                          "Attempt to prev(it, n) with a positive n on a non-bidirectional iterator");
  std::advance(__x, -__n);
  return __x;
}

#if _LIBCPP_STD_VER >= 20

// [range.iter.op.prev]

namespace ranges {
namespace __prev {

struct __fn {
  template <bidirectional_iterator _Ip>
  _LIBCPP_HIDE_FROM_ABI constexpr _Ip operator()(_Ip __x) const {
    --__x;
    return __x;
  }

  template <bidirectional_iterator _Ip>
  _LIBCPP_HIDE_FROM_ABI constexpr _Ip operator()(_Ip __x, iter_difference_t<_Ip> __n) const {
    ranges::advance(__x, -__n);
    return __x;
  }

  template <bidirectional_iterator _Ip>
  _LIBCPP_HIDE_FROM_ABI constexpr _Ip operator()(_Ip __x, iter_difference_t<_Ip> __n, _Ip __bound_iter) const {
    ranges::advance(__x, -__n, __bound_iter);
    return __x;
  }
};

} // namespace __prev

inline namespace __cpo {
inline constexpr auto prev = __prev::__fn{};
} // namespace __cpo
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_PREV_H
