//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_COMPOUND_H
#define _LIBCPP___TYPE_TRAITS_IS_COMPOUND_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_fundamental.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__is_compound)

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_compound : _BoolConstant<__is_compound(_Tp)> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_compound_v = __is_compound(_Tp);
#  endif

#else // __has_builtin(__is_compound)

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_compound : public integral_constant<bool, !is_fundamental<_Tp>::value> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_compound_v = is_compound<_Tp>::value;
#  endif

#endif // __has_builtin(__is_compound)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_COMPOUND_H
