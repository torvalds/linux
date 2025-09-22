//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_ARITHMETIC_H
#define _LIBCPP___TYPE_TRAITS_IS_ARITHMETIC_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_floating_point.h>
#include <__type_traits/is_integral.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_arithmetic
    : public integral_constant<bool, is_integral<_Tp>::value || is_floating_point<_Tp>::value> {};

#if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_arithmetic_v = is_arithmetic<_Tp>::value;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_ARITHMETIC_H
