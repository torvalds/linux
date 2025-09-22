// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CONFIGURATION_PLATFORM_H
#define _LIBCPP___CONFIGURATION_PLATFORM_H

#include <__config_site>

#ifndef _LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER
#  pragma GCC system_header
#endif

#if defined(__ELF__)
#  define _LIBCPP_OBJECT_FORMAT_ELF 1
#elif defined(__MACH__)
#  define _LIBCPP_OBJECT_FORMAT_MACHO 1
#elif defined(_WIN32)
#  define _LIBCPP_OBJECT_FORMAT_COFF 1
#elif defined(__wasm__)
#  define _LIBCPP_OBJECT_FORMAT_WASM 1
#elif defined(_AIX)
#  define _LIBCPP_OBJECT_FORMAT_XCOFF 1
#else
// ... add new file formats here ...
#endif

// Need to detect which libc we're using if we're on Linux.
#if defined(__linux__)
#  include <features.h>
#  if defined(__GLIBC_PREREQ)
#    define _LIBCPP_GLIBC_PREREQ(a, b) __GLIBC_PREREQ(a, b)
#  else
#    define _LIBCPP_GLIBC_PREREQ(a, b) 0
#  endif // defined(__GLIBC_PREREQ)
#endif   // defined(__linux__)

#ifndef __BYTE_ORDER__
#  error                                                                                                               \
      "Your compiler doesn't seem to define __BYTE_ORDER__, which is required by libc++ to know the endianness of your target platform"
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#  define _LIBCPP_LITTLE_ENDIAN
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define _LIBCPP_BIG_ENDIAN
#endif // __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

#endif // _LIBCPP___CONFIGURATION_PLATFORM_H
