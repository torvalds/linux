// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_COMMON_VIEW_H
#define _LIBCPP___RANGES_COMMON_VIEW_H

#include <__concepts/constructible.h>
#include <__concepts/copyable.h>
#include <__config>
#include <__iterator/common_iterator.h>
#include <__iterator/iterator_traits.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/size.h>
#include <__ranges/view_interface.h>
#include <__utility/forward.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {

template <view _View>
  requires(!common_range<_View> && copyable<iterator_t<_View>>)
class common_view : public view_interface<common_view<_View>> {
  _View __base_ = _View();

public:
  _LIBCPP_HIDE_FROM_ABI common_view()
    requires default_initializable<_View>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit common_view(_View __v) : __base_(std::move(__v)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() {
    if constexpr (random_access_range<_View> && sized_range<_View>)
      return ranges::begin(__base_);
    else
      return common_iterator<iterator_t<_View>, sentinel_t<_View>>(ranges::begin(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() const
    requires range<const _View>
  {
    if constexpr (random_access_range<const _View> && sized_range<const _View>)
      return ranges::begin(__base_);
    else
      return common_iterator<iterator_t<const _View>, sentinel_t<const _View>>(ranges::begin(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() {
    if constexpr (random_access_range<_View> && sized_range<_View>)
      return ranges::begin(__base_) + ranges::size(__base_);
    else
      return common_iterator<iterator_t<_View>, sentinel_t<_View>>(ranges::end(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const
    requires range<const _View>
  {
    if constexpr (random_access_range<const _View> && sized_range<const _View>)
      return ranges::begin(__base_) + ranges::size(__base_);
    else
      return common_iterator<iterator_t<const _View>, sentinel_t<const _View>>(ranges::end(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size()
    requires sized_range<_View>
  {
    return ranges::size(__base_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size() const
    requires sized_range<const _View>
  {
    return ranges::size(__base_);
  }
};

template <class _Range>
common_view(_Range&&) -> common_view<views::all_t<_Range>>;

template <class _View>
inline constexpr bool enable_borrowed_range<common_view<_View>> = enable_borrowed_range<_View>;

namespace views {
namespace __common {
struct __fn : __range_adaptor_closure<__fn> {
  template <class _Range>
    requires common_range<_Range>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range) const noexcept(
      noexcept(views::all(std::forward<_Range>(__range)))) -> decltype(views::all(std::forward<_Range>(__range))) {
    return views::all(std::forward<_Range>(__range));
  }

  template <class _Range>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range) const noexcept(noexcept(common_view{
      std::forward<_Range>(__range)})) -> decltype(common_view{std::forward<_Range>(__range)}) {
    return common_view{std::forward<_Range>(__range)};
  }
};
} // namespace __common

inline namespace __cpo {
inline constexpr auto common = __common::__fn{};
} // namespace __cpo
} // namespace views
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_COMMON_VIEW_H
