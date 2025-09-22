//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ATOMIC_ATOMIC_INIT_H
#define _LIBCPP___ATOMIC_ATOMIC_INIT_H

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#define ATOMIC_FLAG_INIT {false}
#define ATOMIC_VAR_INIT(__v) {__v}

#if _LIBCPP_STD_VER >= 20 && defined(_LIBCPP_COMPILER_CLANG_BASED) && !defined(_LIBCPP_DISABLE_DEPRECATION_WARNINGS)
#  pragma clang deprecated(ATOMIC_VAR_INIT)
#endif

#endif // _LIBCPP___ATOMIC_ATOMIC_INIT_H
