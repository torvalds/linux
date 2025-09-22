//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_IS_SCALAR_H
#define _LIBCPP___TYPE_TRAITS_IS_SCALAR_H

#include <__config>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_arithmetic.h>
#include <__type_traits/is_enum.h>
#include <__type_traits/is_member_pointer.h>
#include <__type_traits/is_null_pointer.h>
#include <__type_traits/is_pointer.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if __has_builtin(__is_scalar)

template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_scalar : _BoolConstant<__is_scalar(_Tp)> {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_scalar_v = __is_scalar(_Tp);
#  endif

#else // __has_builtin(__is_scalar)

template <class _Tp>
struct __is_block : false_type {};
#  if defined(_LIBCPP_HAS_EXTENSION_BLOCKS)
template <class _Rp, class... _Args>
struct __is_block<_Rp (^)(_Args...)> : true_type {};
#  endif

// clang-format off
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS is_scalar
    : public integral_constant<
          bool, is_arithmetic<_Tp>::value ||
                is_member_pointer<_Tp>::value ||
                is_pointer<_Tp>::value ||
                __is_null_pointer_v<_Tp> ||
                __is_block<_Tp>::value ||
                is_enum<_Tp>::value> {};
// clang-format on

template <>
struct _LIBCPP_TEMPLATE_VIS is_scalar<nullptr_t> : public true_type {};

#  if _LIBCPP_STD_VER >= 17
template <class _Tp>
inline constexpr bool is_scalar_v = is_scalar<_Tp>::value;
#  endif

#endif // __has_builtin(__is_scalar)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_IS_SCALAR_H
