//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___TYPE_TRAITS_ENABLE_IF_H
#define _LIBCPP___TYPE_TRAITS_ENABLE_IF_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <bool, class _Tp = void>
struct _LIBCPP_TEMPLATE_VIS enable_if {};
template <class _Tp>
struct _LIBCPP_TEMPLATE_VIS enable_if<true, _Tp> {
  typedef _Tp type;
};

template <bool _Bp, class _Tp = void>
using __enable_if_t _LIBCPP_NODEBUG = typename enable_if<_Bp, _Tp>::type;

#if _LIBCPP_STD_VER >= 14
template <bool _Bp, class _Tp = void>
using enable_if_t = typename enable_if<_Bp, _Tp>::type;
#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___TYPE_TRAITS_ENABLE_IF_H
