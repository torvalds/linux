// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_IS_TRANSPARENT
#define _LIBCPP___FUNCTIONAL_IS_TRANSPARENT

#include <__config>
#include <__type_traits/void_t.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 14

template <class _Tp, class, class = void>
inline const bool __is_transparent_v = false;

template <class _Tp, class _Up>
inline const bool __is_transparent_v<_Tp, _Up, __void_t<typename _Tp::is_transparent> > = true;

#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_IS_TRANSPARENT
