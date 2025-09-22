//===-- sanitizer_nolibc_test_main.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
// Tests for libc independence of sanitizer_common.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_libc.h"

extern "C" void _start() {
  __sanitizer::internal__exit(0);
}
