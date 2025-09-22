//===-- hwasan_type_test.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
// Compile-time tests of the internal type definitions.
//===----------------------------------------------------------------------===//

#include "interception/interception.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"
#include "hwasan.h"
#include <setjmp.h>

#define CHECK_TYPE_SIZE_FITS(TYPE) \
  COMPILER_CHECK(sizeof(__hw_##TYPE) <= sizeof(TYPE))

#if HWASAN_WITH_INTERCEPTORS
CHECK_TYPE_SIZE_FITS(jmp_buf);
CHECK_TYPE_SIZE_FITS(sigjmp_buf);
#endif
