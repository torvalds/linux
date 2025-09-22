//===-- interception_type_test.cpp ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Compile-time tests of the internal type definitions.
//===----------------------------------------------------------------------===//

#include "interception.h"

#if SANITIZER_LINUX || SANITIZER_APPLE

#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>

COMPILER_CHECK(sizeof(::SIZE_T) == sizeof(size_t));
COMPILER_CHECK(sizeof(::SSIZE_T) == sizeof(ssize_t));
COMPILER_CHECK(sizeof(::PTRDIFF_T) == sizeof(ptrdiff_t));
COMPILER_CHECK(sizeof(::INTMAX_T) == sizeof(intmax_t));

#  if SANITIZER_GLIBC || SANITIZER_ANDROID
COMPILER_CHECK(sizeof(::OFF64_T) == sizeof(off64_t));
#  endif

// The following are the cases when pread (and friends) is used instead of
// pread64. In those cases we need OFF_T to match off_t. We don't care about the
// rest (they depend on _FILE_OFFSET_BITS setting when building an application).
# if SANITIZER_ANDROID || !defined _FILE_OFFSET_BITS || \
  _FILE_OFFSET_BITS != 64
COMPILER_CHECK(sizeof(::OFF_T) == sizeof(off_t));
# endif

#endif
