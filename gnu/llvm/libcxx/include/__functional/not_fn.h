// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_NOT_FN_H
#define _LIBCPP___FUNCTIONAL_NOT_FN_H

#include <__config>
#include <__functional/invoke.h>
#include <__functional/perfect_forward.h>
#include <__type_traits/decay.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_constructible.h>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

struct __not_fn_op {
  template <class... _Args>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 auto operator()(_Args&&... __args) const
      noexcept(noexcept(!std::invoke(std::forward<_Args>(__args)...)))
          -> decltype(!std::invoke(std::forward<_Args>(__args)...)) {
    return !std::invoke(std::forward<_Args>(__args)...);
  }
};

template <class _Fn>
struct __not_fn_t : __perfect_forward<__not_fn_op, _Fn> {
  using __perfect_forward<__not_fn_op, _Fn>::__perfect_forward;
};

template <class _Fn,
          class = enable_if_t< is_constructible_v<decay_t<_Fn>, _Fn> && is_move_constructible_v<decay_t<_Fn>> >>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 auto not_fn(_Fn&& __f) {
  return __not_fn_t<decay_t<_Fn>>(std::forward<_Fn>(__f));
}

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_NOT_FN_H
