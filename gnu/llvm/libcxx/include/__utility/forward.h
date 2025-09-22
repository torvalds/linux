// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_FORWARD_H
#define _LIBCPP___UTILITY_FORWARD_H

#include <__config>
#include <__type_traits/is_reference.h>
#include <__type_traits/remove_reference.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _Tp&&
forward(_LIBCPP_LIFETIMEBOUND __libcpp_remove_reference_t<_Tp>& __t) _NOEXCEPT {
  return static_cast<_Tp&&>(__t);
}

template <class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _Tp&&
forward(_LIBCPP_LIFETIMEBOUND __libcpp_remove_reference_t<_Tp>&& __t) _NOEXCEPT {
  static_assert(!is_lvalue_reference<_Tp>::value, "cannot forward an rvalue as an lvalue");
  return static_cast<_Tp&&>(__t);
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_FORWARD_H
