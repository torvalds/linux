// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_OWNING_VIEW_H
#define _LIBCPP___RANGES_OWNING_VIEW_H

#include <__concepts/constructible.h>
#include <__concepts/movable.h>
#include <__config>
#include <__ranges/access.h>
#include <__ranges/concepts.h>
#include <__ranges/data.h>
#include <__ranges/empty.h>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/size.h>
#include <__ranges/view_interface.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {
template <range _Rp>
  requires movable<_Rp> && (!__is_std_initializer_list<remove_cvref_t<_Rp>>)
class owning_view : public view_interface<owning_view<_Rp>> {
  _Rp __r_ = _Rp();

public:
  _LIBCPP_HIDE_FROM_ABI owning_view()
    requires default_initializable<_Rp>
  = default;
  _LIBCPP_HIDE_FROM_ABI constexpr owning_view(_Rp&& __r) : __r_(std::move(__r)) {}

  _LIBCPP_HIDE_FROM_ABI owning_view(owning_view&&)            = default;
  _LIBCPP_HIDE_FROM_ABI owning_view& operator=(owning_view&&) = default;

  _LIBCPP_HIDE_FROM_ABI constexpr _Rp& base() & noexcept { return __r_; }
  _LIBCPP_HIDE_FROM_ABI constexpr const _Rp& base() const& noexcept { return __r_; }
  _LIBCPP_HIDE_FROM_ABI constexpr _Rp&& base() && noexcept { return std::move(__r_); }
  _LIBCPP_HIDE_FROM_ABI constexpr const _Rp&& base() const&& noexcept { return std::move(__r_); }

  _LIBCPP_HIDE_FROM_ABI constexpr iterator_t<_Rp> begin() { return ranges::begin(__r_); }
  _LIBCPP_HIDE_FROM_ABI constexpr sentinel_t<_Rp> end() { return ranges::end(__r_); }
  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() const
    requires range<const _Rp>
  {
    return ranges::begin(__r_);
  }
  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const
    requires range<const _Rp>
  {
    return ranges::end(__r_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr bool empty()
    requires requires { ranges::empty(__r_); }
  {
    return ranges::empty(__r_);
  }
  _LIBCPP_HIDE_FROM_ABI constexpr bool empty() const
    requires requires { ranges::empty(__r_); }
  {
    return ranges::empty(__r_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size()
    requires sized_range<_Rp>
  {
    return ranges::size(__r_);
  }
  _LIBCPP_HIDE_FROM_ABI constexpr auto size() const
    requires sized_range<const _Rp>
  {
    return ranges::size(__r_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto data()
    requires contiguous_range<_Rp>
  {
    return ranges::data(__r_);
  }
  _LIBCPP_HIDE_FROM_ABI constexpr auto data() const
    requires contiguous_range<const _Rp>
  {
    return ranges::data(__r_);
  }
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(owning_view);

template <class _Tp>
inline constexpr bool enable_borrowed_range<owning_view<_Tp>> = enable_borrowed_range<_Tp>;

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_OWNING_VIEW_H
