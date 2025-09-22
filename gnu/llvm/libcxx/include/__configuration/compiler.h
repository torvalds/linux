// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONFIGURATION_COMPILER_H
#define _LIBCPP___CONFIGURATION_COMPILER_H

#include <__config_site>

#ifndef _LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER
#  pragma GCC system_header
#endif

#if defined(__apple_build_version__)
// Given AppleClang XX.Y.Z, _LIBCPP_APPLE_CLANG_VER is XXYZ (e.g. AppleClang 14.0.3 => 1403)
#  define _LIBCPP_COMPILER_CLANG_BASED
#  define _LIBCPP_APPLE_CLANG_VER (__apple_build_version__ / 10000)
#elif defined(__clang__)
#  define _LIBCPP_COMPILER_CLANG_BASED
#  define _LIBCPP_CLANG_VER (__clang_major__ * 100 + __clang_minor__)
#elif defined(__GNUC__)
#  define _LIBCPP_COMPILER_GCC
#  define _LIBCPP_GCC_VER (__GNUC__ * 100 + __GNUC_MINOR__)
#endif

#ifdef __cplusplus

// Warn if a compiler version is used that is not supported anymore
// LLVM RELEASE Update the minimum compiler versions
#  if defined(_LIBCPP_CLANG_VER)
#    if _LIBCPP_CLANG_VER < 1700
#      warning "Libc++ only supports Clang 17 and later"
#    endif
#  elif defined(_LIBCPP_APPLE_CLANG_VER)
#    if _LIBCPP_APPLE_CLANG_VER < 1500
#      warning "Libc++ only supports AppleClang 15 and later"
#    endif
#  elif defined(_LIBCPP_GCC_VER)
#    if _LIBCPP_GCC_VER < 1400
#      warning "Libc++ only supports GCC 14 and later"
#    endif
#  endif

#endif

#endif // _LIBCPP___CONFIGURATION_COMPILER_H
