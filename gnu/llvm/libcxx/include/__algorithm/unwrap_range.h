//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_UNWRAP_RANGE_H
#define _LIBCPP___ALGORITHM_UNWRAP_RANGE_H

#include <__algorithm/unwrap_iter.h>
#include <__concepts/constructible.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/next.h>
#include <__utility/declval.h>
#include <__utility/move.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// __unwrap_range and __rewrap_range are used to unwrap ranges which may have different iterator and sentinel types.
// __unwrap_iter and __rewrap_iter don't work for this, because they assume that the iterator and sentinel have
// the same type. __unwrap_range tries to get two iterators and then forward to __unwrap_iter.

#if _LIBCPP_STD_VER >= 20
template <class _Iter, class _Sent>
struct __unwrap_range_impl {
  _LIBCPP_HIDE_FROM_ABI static constexpr auto __unwrap(_Iter __first, _Sent __sent)
    requires random_access_iterator<_Iter> && sized_sentinel_for<_Sent, _Iter>
  {
    auto __last = ranges::next(__first, __sent);
    return pair{std::__unwrap_iter(std::move(__first)), std::__unwrap_iter(std::move(__last))};
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr auto __unwrap(_Iter __first, _Sent __last) {
    return pair{std::move(__first), std::move(__last)};
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr auto
  __rewrap(_Iter __orig_iter, decltype(std::__unwrap_iter(std::move(__orig_iter))) __iter)
    requires random_access_iterator<_Iter> && sized_sentinel_for<_Sent, _Iter>
  {
    return std::__rewrap_iter(std::move(__orig_iter), std::move(__iter));
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr auto __rewrap(const _Iter&, _Iter __iter)
    requires(!(random_access_iterator<_Iter> && sized_sentinel_for<_Sent, _Iter>))
  {
    return __iter;
  }
};

template <class _Iter>
struct __unwrap_range_impl<_Iter, _Iter> {
  _LIBCPP_HIDE_FROM_ABI static constexpr auto __unwrap(_Iter __first, _Iter __last) {
    return pair{std::__unwrap_iter(std::move(__first)), std::__unwrap_iter(std::move(__last))};
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr auto
  __rewrap(_Iter __orig_iter, decltype(std::__unwrap_iter(__orig_iter)) __iter) {
    return std::__rewrap_iter(std::move(__orig_iter), std::move(__iter));
  }
};

template <class _Iter, class _Sent>
_LIBCPP_HIDE_FROM_ABI constexpr auto __unwrap_range(_Iter __first, _Sent __last) {
  return __unwrap_range_impl<_Iter, _Sent>::__unwrap(std::move(__first), std::move(__last));
}

template < class _Sent, class _Iter, class _Unwrapped>
_LIBCPP_HIDE_FROM_ABI constexpr _Iter __rewrap_range(_Iter __orig_iter, _Unwrapped __iter) {
  return __unwrap_range_impl<_Iter, _Sent>::__rewrap(std::move(__orig_iter), std::move(__iter));
}
#else  // _LIBCPP_STD_VER >= 20
template <class _Iter, class _Unwrapped = decltype(std::__unwrap_iter(std::declval<_Iter>()))>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR pair<_Unwrapped, _Unwrapped> __unwrap_range(_Iter __first, _Iter __last) {
  return std::make_pair(std::__unwrap_iter(std::move(__first)), std::__unwrap_iter(std::move(__last)));
}

template <class _Iter, class _Unwrapped>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _Iter __rewrap_range(_Iter __orig_iter, _Unwrapped __iter) {
  return std::__rewrap_iter(std::move(__orig_iter), std::move(__iter));
}
#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_UNWRAP_RANGE_H
