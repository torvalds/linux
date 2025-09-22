//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_TRIVIALLY_DESTRUCTIBLE_H
#define _LIBCPP___TYPE_TRAITS_IS_TRIVIALLY_DESTRUCTIBLE_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_destructible.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__is_trivially_destructible)

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_trivially_destructible
    : public integral_constant<bool, __is_trivially_destructible(_Tp)> {};

#elif __has_builtin(__has_trivial_destructor)

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_trivially_destructible
    : public integral_constant<bool, is_destructible<_Tp>::value&& __has_trivial_destructor(_Tp)> {};

#else

#  error is_trivially_destructible is not implemented

#endif // __has_builtin(__is_trivially_destructible)

#if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_trivially_destructible_v = is_trivially_destructible<_Tp>::value;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_TRIVIALLY_DESTRUCTIBLE_H
