// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_VIEW_INTERFACE_H
#define _LIBCPP___RANGES_VIEW_INTERFACE_H

#include <__assert>
#include <__concepts/derived_from.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/prev.h>
#include <__memory/pointer_traits.h>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/empty.h>
#include <__ranges/size.h>
#include <__type_traits/is_class.h>
#include <__type_traits/make_unsigned.h>
#include <__type_traits/remove_cv.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {

template <class _Derived>
  requires is_class_v<_Derived> && same_as<_Derived, remove_cv_t<_Derived>>
class view_interface {
  _LIBCPP_HIDE_FROM_ABI constexpr _Derived& __derived() noexcept {
    static_assert(sizeof(_Derived) && derived_from<_Derived, view_interface> && view<_Derived>);
    return static_cast<_Derived&>(*this);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Derived const& __derived() const noexcept {
    static_assert(sizeof(_Derived) && derived_from<_Derived, view_interface> && view<_Derived>);
    return static_cast<_Derived const&>(*this);
  }

public:
  template <class _D2 = _Derived>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool empty()
    requires sized_range<_D2> || forward_range<_D2>
  {
    if constexpr (sized_range<_D2>) {
      return ranges::size(__derived()) == 0;
    } else {
      return ranges::begin(__derived()) == ranges::end(__derived());
    }
  }

  template <class _D2 = _Derived>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr bool empty() const
    requires sized_range<const _D2> || forward_range<const _D2>
  {
    if constexpr (sized_range<const _D2>) {
      return ranges::size(__derived()) == 0;
    } else {
      return ranges::begin(__derived()) == ranges::end(__derived());
    }
  }

  template <class _D2 = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit operator bool()
    requires requires(_D2& __t) { ranges::empty(__t); }
  {
    return !ranges::empty(__derived());
  }

  template <class _D2 = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr explicit operator bool() const
    requires requires(const _D2& __t) { ranges::empty(__t); }
  {
    return !ranges::empty(__derived());
  }

  template <class _D2 = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr auto data()
    requires contiguous_iterator<iterator_t<_D2>>
  {
    return std::to_address(ranges::begin(__derived()));
  }

  template <class _D2 = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr auto data() const
    requires range<const _D2> && contiguous_iterator<iterator_t<const _D2>>
  {
    return std::to_address(ranges::begin(__derived()));
  }

  template <class _D2 = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr auto size()
    requires forward_range<_D2> && sized_sentinel_for<sentinel_t<_D2>, iterator_t<_D2>>
  {
    return std::__to_unsigned_like(ranges::end(__derived()) - ranges::begin(__derived()));
  }

  template <class _D2 = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr auto size() const
    requires forward_range<const _D2> && sized_sentinel_for<sentinel_t<const _D2>, iterator_t<const _D2>>
  {
    return std::__to_unsigned_like(ranges::end(__derived()) - ranges::begin(__derived()));
  }

  template <class _D2 = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) front()
    requires forward_range<_D2>
  {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !empty(), "Precondition `!empty()` not satisfied. `.front()` called on an empty view.");
    return *ranges::begin(__derived());
  }

  template <class _D2 = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) front() const
    requires forward_range<const _D2>
  {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !empty(), "Precondition `!empty()` not satisfied. `.front()` called on an empty view.");
    return *ranges::begin(__derived());
  }

  template <class _D2 = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) back()
    requires bidirectional_range<_D2> && common_range<_D2>
  {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !empty(), "Precondition `!empty()` not satisfied. `.back()` called on an empty view.");
    return *ranges::prev(ranges::end(__derived()));
  }

  template <class _D2 = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) back() const
    requires bidirectional_range<const _D2> && common_range<const _D2>
  {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        !empty(), "Precondition `!empty()` not satisfied. `.back()` called on an empty view.");
    return *ranges::prev(ranges::end(__derived()));
  }

  template <random_access_range _RARange = _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator[](range_difference_t<_RARange> __index) {
    return ranges::begin(__derived())[__index];
  }

  template <random_access_range _RARange = const _Derived>
  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator[](range_difference_t<_RARange> __index) const {
    return ranges::begin(__derived())[__index];
  }
};

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_VIEW_INTERFACE_H
