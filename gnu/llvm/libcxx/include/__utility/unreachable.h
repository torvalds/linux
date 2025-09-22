//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_UNREACHABLE_H
#define _LIBCPP___UTILITY_UNREACHABLE_H

#include <__assert>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

_LIBCPP_NORETURN _LIBCPP_HIDE_FROM_ABI inline void __libcpp_unreachable() {
  _LIBCPP_ASSERT_INTERNAL(false, "std::unreachable() was reached");
  __builtin_unreachable();
}

#if _LIBCPP_STD_VER >= 23

[[noreturn]] _LIBCPP_HIDE_FROM_ABI inline void unreachable() { __libcpp_unreachable(); }

#endif

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___UTILITY_UNREACHABLE_H
