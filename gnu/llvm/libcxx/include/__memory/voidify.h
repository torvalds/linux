// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_VOIDIFY_H
#define _LIBCPP___MEMORY_VOIDIFY_H

#include <__config>
#include <__memory/addressof.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

template <typename _Tp>
_LIBCPP_ALWAYS_INLINE _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void* __voidify(_Tp& __from) {
  // Cast away cv-qualifiers to allow modifying elements of a range through const iterators.
  return const_cast<void*>(static_cast<const volatile void*>(std::addressof(__from)));
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_VOIDIFY_H
