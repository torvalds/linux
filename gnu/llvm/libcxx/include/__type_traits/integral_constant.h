//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_INTEGRAL_CONSTANT_H
#define _LIBCPP___TYPE_TRAITS_INTEGRAL_CONSTANT_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp, _Tp __v>
struct _LIBCPP_TEMPLATE_VIS integral_constant {
  static _LIBCPP_CONSTEXPR const _Tp value = __v;
  typedef _Tp value_type;
  typedef integral_constant type;
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR operator value_type() const _NOEXCEPT { return value; }
#if _LIBCPP_STD_VER >= 14
  _LIBCPP_HIDE_FROM_ABI constexpr value_type operator()() const _NOEXCEPT { return value; }
#endif
};

template <class _Tp, _Tp __v>
_LIBCPP_CONSTEXPR const _Tp integral_constant<_Tp, __v>::value;

typedef integral_constant<bool, true> true_type;
typedef integral_constant<bool, false> false_type;

template <bool _Val>
using _BoolConstant _LIBCPP_NODEBUG = integral_constant<bool, _Val>;

#if _LIBCPP_STD_VER >= 17
template <bool __b>
using bool_constant = integral_constant<bool, __b>;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_INTEGRAL_CONSTANT_H
