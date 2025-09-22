//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_FUNDAMENTAL_H
#define _LIBCPP___TYPE_TRAITS_IS_FUNDAMENTAL_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_null_pointer.h>
#include <__type_traits/is_void.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__is_fundamental)

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_fundamental : _BoolConstant<__is_fundamental(_Tp)> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_fundamental_v = __is_fundamental(_Tp);
#  endif

#else // __has_builtin(__is_fundamental)

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_fundamental
    : public integral_constant<bool, is_void<_Tp>::value || __is_null_pointer_v<_Tp> || is_arithmetic<_Tp>::value> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_fundamental_v = is_fundamental<_Tp>::value;
#  endif

#endif // __has_builtin(__is_fundamental)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_FUNDAMENTAL_H
