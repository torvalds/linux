//===-- definitions.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_DEFINITIONS_H_
#define GWP_ASAN_DEFINITIONS_H_

#define GWP_ASAN_TLS_INITIAL_EXEC                                              \
  __thread __attribute__((tls_model("initial-exec")))

#define GWP_ASAN_UNLIKELY(X) __builtin_expect(!!(X), 0)
#define GWP_ASAN_ALWAYS_INLINE inline __attribute__((always_inline))

#define GWP_ASAN_WEAK __attribute__((weak))

#endif // GWP_ASAN_DEFINITIONS_H_
