//===-- int_math.h - internal math inlines --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is not part of the interface of this library.
//
// This file defines substitutes for the libm functions used in some of the
// compiler-rt implementations, defined in such a way that there is not a direct
// dependency on libm or math.h. Instead, we use the compiler builtin versions
// where available. This reduces our dependencies on the system SDK by foisting
// the responsibility onto the compiler.
//
//===----------------------------------------------------------------------===//

#ifndef INT_MATH_H
#define INT_MATH_H

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#include <math.h>
#include <stdlib.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define CRT_INFINITY INFINITY
#else
#define CRT_INFINITY __builtin_huge_valf()
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_isfinite(x) _finite((x))
#define crt_isinf(x) !_finite((x))
#define crt_isnan(x) _isnan((x))
#else
// Define crt_isfinite in terms of the builtin if available, otherwise provide
// an alternate version in terms of our other functions. This supports some
// versions of GCC which didn't have __builtin_isfinite.
#if __has_builtin(__builtin_isfinite)
#define crt_isfinite(x) __builtin_isfinite((x))
#elif defined(__GNUC__)
#define crt_isfinite(x)                                                        \
  __extension__(({                                                             \
    __typeof((x)) x_ = (x);                                                    \
    !crt_isinf(x_) && !crt_isnan(x_);                                          \
  }))
#else
#error "Do not know how to check for infinity"
#endif // __has_builtin(__builtin_isfinite)
#define crt_isinf(x) __builtin_isinf((x))
#define crt_isnan(x) __builtin_isnan((x))
#endif // _MSC_VER

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_copysign(x, y) copysign((x), (y))
#define crt_copysignf(x, y) copysignf((x), (y))
#define crt_copysignl(x, y) copysignl((x), (y))
#else
#define crt_copysign(x, y) __builtin_copysign((x), (y))
#define crt_copysignf(x, y) __builtin_copysignf((x), (y))
#define crt_copysignl(x, y) __builtin_copysignl((x), (y))
// We define __has_builtin to always return 0 for GCC versions below 10,
// but __builtin_copysignf128 is available since version 7.
#if __has_builtin(__builtin_copysignf128) ||                                   \
    (defined(__GNUC__) && __GNUC__ >= 7)
#define crt_copysignf128(x, y) __builtin_copysignf128((x), (y))
#elif __has_builtin(__builtin_copysignq)
#define crt_copysignf128(x, y) __builtin_copysignq((x), (y))
#endif
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_fabs(x) fabs((x))
#define crt_fabsf(x) fabsf((x))
#define crt_fabsl(x) fabs((x))
#else
#define crt_fabs(x) __builtin_fabs((x))
#define crt_fabsf(x) __builtin_fabsf((x))
#define crt_fabsl(x) __builtin_fabsl((x))
// We define __has_builtin to always return 0 for GCC versions below 10,
// but __builtin_fabsf128 is available since version 7.
#if __has_builtin(__builtin_fabsf128) || (defined(__GNUC__) && __GNUC__ >= 7)
#define crt_fabsf128(x) __builtin_fabsf128((x))
#elif __has_builtin(__builtin_fabsq)
#define crt_fabsf128(x) __builtin_fabsq((x))
#endif
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_fmaxl(x, y) __max((x), (y))
#else
#define crt_fmaxl(x, y) __builtin_fmaxl((x), (y))
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_logbl(x) logbl((x))
#else
#define crt_logbl(x) __builtin_logbl((x))
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define crt_scalbnl(x, y) scalbnl((x), (y))
#else
#define crt_scalbnl(x, y) __builtin_scalbnl((x), (y))
#endif

#endif // INT_MATH_H
