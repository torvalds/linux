//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONCEPTS_ARITHMETIC_H
#define _LIBCPP___CONCEPTS_ARITHMETIC_H

#include <__config>
#include <__type_traits/is_floating_point.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_signed.h>
#include <__type_traits/is_signed_integer.h>
#include <__type_traits/is_unsigned_integer.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// [concepts.arithmetic], arithmetic concepts

template <class _Tp>
concept integral = is_integral_v<_Tp>;

template <class _Tp>
concept signed_integral = integral<_Tp> && is_signed_v<_Tp>;

template <class _Tp>
concept unsigned_integral = integral<_Tp> && !signed_integral<_Tp>;

template <class _Tp>
concept floating_point = is_floating_point_v<_Tp>;

// Concept helpers for the internal type traits for the fundamental types.

template <class _Tp>
concept __libcpp_unsigned_integer = __libcpp_is_unsigned_integer<_Tp>::value;

template <class _Tp>
concept __libcpp_signed_integer = __libcpp_is_signed_integer<_Tp>::value;

template <class _Tp>
concept __libcpp_integer = __libcpp_unsigned_integer<_Tp> || __libcpp_signed_integer<_Tp>;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CONCEPTS_ARITHMETIC_H
