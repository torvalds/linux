// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_DISTANCE_H
#define _LIBCPP___ITERATOR_DISTANCE_H

#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iterator_traits.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/size.h>
#include <__type_traits/decay.h>
#include <__type_traits/remove_cvref.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _InputIter>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 typename iterator_traits<_InputIter>::difference_type
__distance(_InputIter __first, _InputIter __last, input_iterator_tag) {
  typename iterator_traits<_InputIter>::difference_type __r(0);
  for (; __first != __last; ++__first)
    ++__r;
  return __r;
}

template <class _RandIter>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 typename iterator_traits<_RandIter>::difference_type
__distance(_RandIter __first, _RandIter __last, random_access_iterator_tag) {
  return __last - __first;
}

template <class _InputIter>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 typename iterator_traits<_InputIter>::difference_type
distance(_InputIter __first, _InputIter __last) {
  return std::__distance(__first, __last, typename iterator_traits<_InputIter>::iterator_category());
}

#if _LIBCPP_STD_VER >= 20

// [range.iter.op.distance]

namespace ranges {
namespace __distance {

struct __fn {
  template <class _Ip, sentinel_for<_Ip> _Sp>
    requires(!sized_sentinel_for<_Sp, _Ip>)
  _LIBCPP_HIDE_FROM_ABI constexpr iter_difference_t<_Ip> operator()(_Ip __first, _Sp __last) const {
    iter_difference_t<_Ip> __n = 0;
    while (__first != __last) {
      ++__first;
      ++__n;
    }
    return __n;
  }

  template <class _Ip, sized_sentinel_for<decay_t<_Ip>> _Sp>
  _LIBCPP_HIDE_FROM_ABI constexpr iter_difference_t<_Ip> operator()(_Ip&& __first, _Sp __last) const {
    if constexpr (sized_sentinel_for<_Sp, __remove_cvref_t<_Ip>>) {
      return __last - __first;
    } else {
      return __last - decay_t<_Ip>(__first);
    }
  }

  template <range _Rp>
  _LIBCPP_HIDE_FROM_ABI constexpr range_difference_t<_Rp> operator()(_Rp&& __r) const {
    if constexpr (sized_range<_Rp>) {
      return static_cast<range_difference_t<_Rp>>(ranges::size(__r));
    } else {
      return operator()(ranges::begin(__r), ranges::end(__r));
    }
  }
};

} // namespace __distance

inline namespace __cpo {
inline constexpr auto distance = __distance::__fn{};
} // namespace __cpo
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___ITERATOR_DISTANCE_H
