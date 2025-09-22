// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_BIND_BACK_H
#define _LIBCPP___FUNCTIONAL_BIND_BACK_H

#include <__config>
#include <__functional/invoke.h>
#include <__functional/perfect_forward.h>
#include <__type_traits/decay.h>
#include <__utility/forward.h>
#include <__utility/integer_sequence.h>
#include <tuple>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <size_t _NBound, class = make_index_sequence<_NBound>>
struct __bind_back_op;

template <size_t _NBound, size_t... _Ip>
struct __bind_back_op<_NBound, index_sequence<_Ip...>> {
  template <class _Fn, class _BoundArgs, class... _Args>
  _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Fn&& __f, _BoundArgs&& __bound_args, _Args&&... __args) const
      noexcept(noexcept(std::invoke(std::forward<_Fn>(__f),
                                    std::forward<_Args>(__args)...,
                                    std::get<_Ip>(std::forward<_BoundArgs>(__bound_args))...)))
          -> decltype(std::invoke(std::forward<_Fn>(__f),
                                  std::forward<_Args>(__args)...,
                                  std::get<_Ip>(std::forward<_BoundArgs>(__bound_args))...)) {
    return std::invoke(std::forward<_Fn>(__f),
                       std::forward<_Args>(__args)...,
                       std::get<_Ip>(std::forward<_BoundArgs>(__bound_args))...);
  }
};

template <class _Fn, class _BoundArgs>
struct __bind_back_t : __perfect_forward<__bind_back_op<tuple_size_v<_BoundArgs>>, _Fn, _BoundArgs> {
  using __perfect_forward<__bind_back_op<tuple_size_v<_BoundArgs>>, _Fn, _BoundArgs>::__perfect_forward;
};

template <class _Fn, class... _Args>
  requires is_constructible_v<decay_t<_Fn>, _Fn> && is_move_constructible_v<decay_t<_Fn>> &&
               (is_constructible_v<decay_t<_Args>, _Args> && ...) && (is_move_constructible_v<decay_t<_Args>> && ...)
_LIBCPP_HIDE_FROM_ABI constexpr auto __bind_back(_Fn&& __f, _Args&&... __args) noexcept(
    noexcept(__bind_back_t<decay_t<_Fn>, tuple<decay_t<_Args>...>>(
        std::forward<_Fn>(__f), std::forward_as_tuple(std::forward<_Args>(__args)...))))
    -> decltype(__bind_back_t<decay_t<_Fn>, tuple<decay_t<_Args>...>>(
        std::forward<_Fn>(__f), std::forward_as_tuple(std::forward<_Args>(__args)...))) {
  return __bind_back_t<decay_t<_Fn>, tuple<decay_t<_Args>...>>(
      std::forward<_Fn>(__f), std::forward_as_tuple(std::forward<_Args>(__args)...));
}

#  if _LIBCPP_STD_VER >= 23
template <class _Fn, class... _Args>
_LIBCPP_HIDE_FROM_ABI constexpr auto bind_back(_Fn&& __f, _Args&&... __args) {
  static_assert(is_constructible_v<decay_t<_Fn>, _Fn>, "bind_back requires decay_t<F> to be constructible from F");
  static_assert(is_move_constructible_v<decay_t<_Fn>>, "bind_back requires decay_t<F> to be move constructible");
  static_assert((is_constructible_v<decay_t<_Args>, _Args> && ...),
                "bind_back requires all decay_t<Args> to be constructible from respective Args");
  static_assert((is_move_constructible_v<decay_t<_Args>> && ...),
                "bind_back requires all decay_t<Args> to be move constructible");
  return __bind_back_t<decay_t<_Fn>, tuple<decay_t<_Args>...>>(
      std::forward<_Fn>(__f), std::forward_as_tuple(std::forward<_Args>(__args)...));
}
#  endif // _LIBCPP_STD_VER >= 23

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_BIND_BACK_H
