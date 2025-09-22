//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_SIGNED_H
#define _LIBCPP___TYPE_TRAITS_IS_SIGNED_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_arithmetic.h>
#include <__type_traits/is_integral.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__is_signed)

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_signed : _BoolConstant<__is_signed(_Tp)> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_signed_v = __is_signed(_Tp);
#  endif

#else // __has_builtin(__is_signed)

template <class _Tp, bool = is_integral<_Tp>::value>
struct __libcpp_is_signed_impl : public _BoolConstant<(_Tp(-1) < _Tp(0))> {};

template <class _Tp>
struct __libcpp_is_signed_impl<_Tp, false> : public true_type {}; // floating point

template <class _Tp, bool = is_arithmetic<_Tp>::value>
struct __libcpp_is_signed : public __libcpp_is_signed_impl<_Tp> {};

template <class _Tp>
struct __libcpp_is_signed<_Tp, false> : public false_type {};

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_signed : public __libcpp_is_signed<_Tp> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_signed_v = is_signed<_Tp>::value;
#  endif

#endif // __has_builtin(__is_signed)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_SIGNED_H
