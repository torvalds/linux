// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_MOVE_H
#define _LIBCPP___UTILITY_MOVE_H

#include <__config>
#include <__type_traits/conditional.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/remove_reference.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR __libcpp_remove_reference_t<_Tp>&&
move(_LIBCPP_LIFETIMEBOUND _Tp&& __t) _NOEXCEPT {
  typedef _LIBCPP_NODEBUG __libcpp_remove_reference_t<_Tp> _Up;
  return static_cast<_Up&&>(__t);
}

template <class _Tp>
using __move_if_noexcept_result_t =
    __conditional_t<!is_nothrow_move_constructible<_Tp>::value && is_copy_constructible<_Tp>::value, const _Tp&, _Tp&&>;

template <class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 __move_if_noexcept_result_t<_Tp>
move_if_noexcept(_LIBCPP_LIFETIMEBOUND _Tp& __x) _NOEXCEPT {
  return std::move(__x);
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___UTILITY_MOVE_H
