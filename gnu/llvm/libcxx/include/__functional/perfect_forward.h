// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_PERFECT_FORWARD_H
#define _LIBCPP___FUNCTIONAL_PERFECT_FORWARD_H

#include <__config>
#include <__type_traits/enable_if.h>
#include <__type_traits/invoke.h>
#include <__type_traits/is_constructible.h>
#include <__utility/declval.h>
#include <__utility/forward.h>
#include <__utility/integer_sequence.h>
#include <__utility/move.h>
#include <tuple>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

template <class _Op, class _Indices, class... _BoundArgs>
struct __perfect_forward_impl;

template <class _Op, size_t... _Idx, class... _BoundArgs>
struct __perfect_forward_impl<_Op, index_sequence<_Idx...>, _BoundArgs...> {
private:
  tuple<_BoundArgs...> __bound_args_;

public:
  template <class... _Args, class = enable_if_t< is_constructible_v<tuple<_BoundArgs...>, _Args&&...> >>
  _LIBCPP_HIDE_FROM_ABI explicit constexpr __perfect_forward_impl(_Args&&... __bound_args)
      : __bound_args_(std::forward<_Args>(__bound_args)...) {}

  _LIBCPP_HIDE_FROM_ABI __perfect_forward_impl(__perfect_forward_impl const&) = default;
  _LIBCPP_HIDE_FROM_ABI __perfect_forward_impl(__perfect_forward_impl&&)      = default;

  _LIBCPP_HIDE_FROM_ABI __perfect_forward_impl& operator=(__perfect_forward_impl const&) = default;
  _LIBCPP_HIDE_FROM_ABI __perfect_forward_impl& operator=(__perfect_forward_impl&&)      = default;

  template <class... _Args, class = enable_if_t<is_invocable_v<_Op, _BoundArgs&..., _Args...>>>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Args&&... __args) & noexcept(
      noexcept(_Op()(std::get<_Idx>(__bound_args_)..., std::forward<_Args>(__args)...)))
      -> decltype(_Op()(std::get<_Idx>(__bound_args_)..., std::forward<_Args>(__args)...)) {
    return _Op()(std::get<_Idx>(__bound_args_)..., std::forward<_Args>(__args)...);
  }

  template <class... _Args, class = enable_if_t<!is_invocable_v<_Op, _BoundArgs&..., _Args...>>>
  auto operator()(_Args&&...) & = delete;

  template <class... _Args, class = enable_if_t<is_invocable_v<_Op, _BoundArgs const&..., _Args...>>>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Args&&... __args) const& noexcept(
      noexcept(_Op()(std::get<_Idx>(__bound_args_)..., std::forward<_Args>(__args)...)))
      -> decltype(_Op()(std::get<_Idx>(__bound_args_)..., std::forward<_Args>(__args)...)) {
    return _Op()(std::get<_Idx>(__bound_args_)..., std::forward<_Args>(__args)...);
  }

  template <class... _Args, class = enable_if_t<!is_invocable_v<_Op, _BoundArgs const&..., _Args...>>>
  auto operator()(_Args&&...) const& = delete;

  template <class... _Args, class = enable_if_t<is_invocable_v<_Op, _BoundArgs..., _Args...>>>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Args&&... __args) && noexcept(
      noexcept(_Op()(std::get<_Idx>(std::move(__bound_args_))..., std::forward<_Args>(__args)...)))
      -> decltype(_Op()(std::get<_Idx>(std::move(__bound_args_))..., std::forward<_Args>(__args)...)) {
    return _Op()(std::get<_Idx>(std::move(__bound_args_))..., std::forward<_Args>(__args)...);
  }

  template <class... _Args, class = enable_if_t<!is_invocable_v<_Op, _BoundArgs..., _Args...>>>
  auto operator()(_Args&&...) && = delete;

  template <class... _Args, class = enable_if_t<is_invocable_v<_Op, _BoundArgs const..., _Args...>>>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Args&&... __args) const&& noexcept(
      noexcept(_Op()(std::get<_Idx>(std::move(__bound_args_))..., std::forward<_Args>(__args)...)))
      -> decltype(_Op()(std::get<_Idx>(std::move(__bound_args_))..., std::forward<_Args>(__args)...)) {
    return _Op()(std::get<_Idx>(std::move(__bound_args_))..., std::forward<_Args>(__args)...);
  }

  template <class... _Args, class = enable_if_t<!is_invocable_v<_Op, _BoundArgs const..., _Args...>>>
  auto operator()(_Args&&...) const&& = delete;
};

// __perfect_forward implements a perfect-forwarding call wrapper as explained in [func.require].
template <class _Op, class... _Args>
using __perfect_forward = __perfect_forward_impl<_Op, index_sequence_for<_Args...>, _Args...>;

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___FUNCTIONAL_PERFECT_FORWARD_H
