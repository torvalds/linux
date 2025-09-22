// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_FORWARD_LIKE_H
#define _LIBCPP___UTILITY_FORWARD_LIKE_H

#include <__config>
#include <__type_traits/conditional.h>
#include <__type_traits/is_const.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/remove_reference.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

template <class _Ap, class _Bp>
using _CopyConst = _If<is_const_v<_Ap>, const _Bp, _Bp>;

template <class _Ap, class _Bp>
using _OverrideRef = _If<is_rvalue_reference_v<_Ap>, remove_reference_t<_Bp>&&, _Bp&>;

template <class _Ap, class _Bp>
using _ForwardLike = _OverrideRef<_Ap&&, _CopyConst<remove_reference_t<_Ap>, remove_reference_t<_Bp>>>;

template <class _Tp, class _Up>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto
forward_like(_LIBCPP_LIFETIMEBOUND _Up&& __ux) noexcept -> _ForwardLike<_Tp, _Up> {
  return static_cast<_ForwardLike<_Tp, _Up>>(__ux);
}

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_FORWARD_LIKE_H
