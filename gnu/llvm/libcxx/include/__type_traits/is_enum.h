//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_ENUM_H
#define _LIBCPP___TYPE_TRAITS_IS_ENUM_H

#include <__config>
#include <__type_traits/integral_constant.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_enum : public integral_constant<bool, __is_enum(_Tp)> {};

#if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_enum_v = __is_enum(_Tp);
#endif

#if _LIBCPP_STD_VER >= 23

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_scoped_enum : bool_constant<__is_scoped_enum(_Tp)> {};

template <class _Tp>
inline constexpr bool is_scoped_enum_v = __is_scoped_enum(_Tp);

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_ENUM_H
