//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_CONFIG_ELAST
#define _LIBCPP_CONFIG_ELAST

#include <__config>

#if defined(_LIBCPP_MSVCRT_LIKE)
#  include <stdlib.h>
#else
#  include <errno.h>
#endif

// Note: _LIBCPP_ELAST needs to be defined only on platforms
// where strerror/strerror_r can't handle out-of-range errno values.
#if defined(ELAST)
#  define _LIBCPP_ELAST ELAST
#elif defined(_NEWLIB_VERSION)
#  define _LIBCPP_ELAST __ELASTERROR
#elif defined(__NuttX__)
// No _LIBCPP_ELAST needed on NuttX
#elif defined(__Fuchsia__)
// No _LIBCPP_ELAST needed on Fuchsia
#elif defined(__wasi__)
// No _LIBCPP_ELAST needed on WASI
#elif defined(__EMSCRIPTEN__)
// No _LIBCPP_ELAST needed on Emscripten
#elif defined(__linux__) || defined(_LIBCPP_HAS_MUSL_LIBC)
#  define _LIBCPP_ELAST 4095
#elif defined(__APPLE__)
// No _LIBCPP_ELAST needed on Apple
#elif defined(__MVS__)
#  define _LIBCPP_ELAST 1160
#elif defined(_LIBCPP_MSVCRT_LIKE)
#  define _LIBCPP_ELAST (_sys_nerr - 1)
#elif defined(_AIX)
#  define _LIBCPP_ELAST 127
#else
// Warn here so that the person doing the libcxx port has an easier time:
#  warning ELAST for this platform not yet implemented
#endif

#endif // _LIBCPP_CONFIG_ELAST
