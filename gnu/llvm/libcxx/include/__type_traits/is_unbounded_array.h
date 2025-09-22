//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_UNBOUNDED_ARRAY_H
#define _LIBCPP___TYPE_TRAITS_IS_UNBOUNDED_ARRAY_H

#include <__config>
#include <__type_traits/integral_constant.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class>
struct _LIBCPP_TEMPLATE_VIS __libcpp_is_unbounded_array : false_type {};
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS __libcpp_is_unbounded_array<_Tp[]> : true_type {};

#if _LIBCPP_STD_VER >= 20

template <class>
struct _LIBCPP_TEMPLATE_VIS is_unbounded_array : false_type {};
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_unbounded_array<_Tp[]> : true_type {};

template <class _Tp>
inline constexpr bool is_unbounded_array_v = is_unbounded_array<_Tp>::value;

#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_UNBOUNDED_ARRAY_H
