// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_NON_PROPAGATING_CACHE_H
#define _LIBCPP___RANGES_NON_PROPAGATING_CACHE_H

#include <__config>
#include <__iterator/concepts.h>        // indirectly_readable
#include <__iterator/iterator_traits.h> // iter_reference_t
#include <__memory/addressof.h>
#include <__utility/forward.h>
#include <optional>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {
// __non_propagating_cache is a helper type that allows storing an optional value in it,
// but which does not copy the source's value when it is copy constructed/assigned to,
// and which resets the source's value when it is moved-from.
//
// This type is used as an implementation detail of some views that need to cache the
// result of `begin()` in order to provide an amortized O(1) begin() method. Typically,
// we don't want to propagate the value of the cache upon copy because the cached iterator
// may refer to internal details of the source view.
template <class _Tp>
  requires is_object_v<_Tp>
class _LIBCPP_TEMPLATE_VIS __non_propagating_cache {
  struct __from_tag {};
  struct __forward_tag {};

  // This helper class is needed to perform copy and move elision when
  // constructing the contained type from an iterator.
  struct __wrapper {
    template <class... _Args>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __wrapper(__forward_tag, _Args&&... __args)
        : __t_(std::forward<_Args>(__args)...) {}
    template <class _Fn>
    _LIBCPP_HIDE_FROM_ABI constexpr explicit __wrapper(__from_tag, _Fn const& __f) : __t_(__f()) {}
    _Tp __t_;
  };

  optional<__wrapper> __value_ = nullopt;

public:
  _LIBCPP_HIDE_FROM_ABI __non_propagating_cache() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr __non_propagating_cache(__non_propagating_cache const&) noexcept
      : __value_(nullopt) {}

  _LIBCPP_HIDE_FROM_ABI constexpr __non_propagating_cache(__non_propagating_cache&& __other) noexcept
      : __value_(nullopt) {
    __other.__value_.reset();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __non_propagating_cache& operator=(__non_propagating_cache const& __other) noexcept {
    if (this != std::addressof(__other)) {
      __value_.reset();
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __non_propagating_cache& operator=(__non_propagating_cache&& __other) noexcept {
    __value_.reset();
    __other.__value_.reset();
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Tp& operator*() { return __value_->__t_; }
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp const& operator*() const { return __value_->__t_; }

  _LIBCPP_HIDE_FROM_ABI constexpr bool __has_value() const { return __value_.has_value(); }

  template <class _Fn>
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp& __emplace_from(_Fn const& __f) {
    return __value_.emplace(__from_tag{}, __f).__t_;
  }

  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr _Tp& __emplace(_Args&&... __args) {
    return __value_.emplace(__forward_tag{}, std::forward<_Args>(__args)...).__t_;
  }
};

struct __empty_cache {};
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___RANGES_NON_PROPAGATING_CACHE_H
