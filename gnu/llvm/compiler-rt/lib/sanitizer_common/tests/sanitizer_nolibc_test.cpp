//===-- sanitizer_nolibc_test.cpp -----------------------------------------===//
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

#include "sanitizer_common/sanitizer_platform.h"

#include "gtest/gtest.h"

#include <stdlib.h>

extern const char *argv0;

#if SANITIZER_LINUX && defined(__x86_64__)
TEST(SanitizerCommon, NolibcMain) {
  std::string NolibcTestPath = argv0;
  NolibcTestPath += "-Nolibc";
  int status = system(NolibcTestPath.c_str());
  EXPECT_EQ(true, WIFEXITED(status));
  EXPECT_EQ(0, WEXITSTATUS(status));
}
#endif
