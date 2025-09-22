// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_INVOKE_H
#define _LIBCPP___FUNCTIONAL_INVOKE_H

#include <__config>
#include <__type_traits/invoke.h>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 17

template <class _Fn, class... _Args>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 invoke_result_t<_Fn, _Args...>
invoke(_Fn&& __f, _Args&&... __args) noexcept(is_nothrow_invocable_v<_Fn, _Args...>) {
  return std::__invoke(std::forward<_Fn>(__f), std::forward<_Args>(__args)...);
}

#endif // _LIBCPP_STD_VER >= 17

#if _LIBCPP_STD_VER >= 23
template <class _Result, class _Fn, class... _Args>
  requires is_invocable_r_v<_Result, _Fn, _Args...>
_LIBCPP_HIDE_FROM_ABI constexpr _Result
invoke_r(_Fn&& __f, _Args&&... __args) noexcept(is_nothrow_invocable_r_v<_Result, _Fn, _Args...>) {
  if constexpr (is_void_v<_Result>) {
    static_cast<void>(std::invoke(std::forward<_Fn>(__f), std::forward<_Args>(__args)...));
  } else {
    // TODO: Use reference_converts_from_temporary_v once implemented
    // using _ImplicitInvokeResult = invoke_result_t<_Fn, _Args...>;
    // static_assert(!reference_converts_from_temporary_v<_Result, _ImplicitInvokeResult>,
    static_assert(true,
                  "Returning from invoke_r would bind a temporary object to the reference return type, "
                  "which would result in a dangling reference.");
    return std::invoke(std::forward<_Fn>(__f), std::forward<_Args>(__args)...);
  }
}
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_INVOKE_H
