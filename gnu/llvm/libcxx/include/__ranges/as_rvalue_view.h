//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_AS_RVALUE_H
#define _LIBCPP___RANGES_AS_RVALUE_H

#include <__concepts/constructible.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__iterator/move_iterator.h>
#include <__iterator/move_sentinel.h>
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

#if _LIBCPP_STD_VER >= 23

_LIBCPP_BEGIN_NAMESPACE_STD

namespace ranges {
template <view _View>
  requires input_range<_View>
class as_rvalue_view : public view_interface<as_rvalue_view<_View>> {
  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_ = _View();

public:
  _LIBCPP_HIDE_FROM_ABI as_rvalue_view()
    requires default_initializable<_View>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit as_rvalue_view(_View __base) : __base_(std::move(__base)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin()
    requires(!__simple_view<_View>)
  {
    return move_iterator(ranges::begin(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() const
    requires range<const _View>
  {
    return move_iterator(ranges::begin(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end()
    requires(!__simple_view<_View>)
  {
    if constexpr (common_range<_View>) {
      return move_iterator(ranges::end(__base_));
    } else {
      return move_sentinel(ranges::end(__base_));
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const
    requires range<const _View>
  {
    if constexpr (common_range<const _View>) {
      return move_iterator(ranges::end(__base_));
    } else {
      return move_sentinel(ranges::end(__base_));
    }
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
as_rvalue_view(_Range&&) -> as_rvalue_view<views::all_t<_Range>>;

template <class _View>
inline constexpr bool enable_borrowed_range<as_rvalue_view<_View>> = enable_borrowed_range<_View>;

namespace views {
namespace __as_rvalue {
struct __fn : __range_adaptor_closure<__fn> {
  template <class _Range>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI static constexpr auto
  operator()(_Range&& __range) noexcept(noexcept(as_rvalue_view(std::forward<_Range>(__range))))
      -> decltype(/*--------------------------*/ as_rvalue_view(std::forward<_Range>(__range))) {
    return /*---------------------------------*/ as_rvalue_view(std::forward<_Range>(__range));
  }

  template <class _Range>
    requires same_as<range_rvalue_reference_t<_Range>, range_reference_t<_Range>>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI static constexpr auto
  operator()(_Range&& __range) noexcept(noexcept(views::all(std::forward<_Range>(__range))))
      -> decltype(/*--------------------------*/ views::all(std::forward<_Range>(__range))) {
    return /*---------------------------------*/ views::all(std::forward<_Range>(__range));
  }
};
} // namespace __as_rvalue

inline namespace __cpo {
inline constexpr auto as_rvalue = __as_rvalue::__fn{};
} // namespace __cpo
} // namespace views
} // namespace ranges

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_AS_RVALUE_H
