// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_AS_LVALUE_H
#define _LIBCPP___UTILITY_AS_LVALUE_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#ifndef _LIBCPP_CXX03_LANG

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp& __as_lvalue(_LIBCPP_LIFETIMEBOUND _Tp&& __t) {
  return static_cast<_Tp&>(__t);
}

#endif // !_LIBCPP_CXX03_LANG

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___UTILITY_AS_LVALUE_H
