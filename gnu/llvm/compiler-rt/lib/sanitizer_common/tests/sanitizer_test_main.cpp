//===-- sanitizer_test_main.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//
#include "gtest/gtest.h"
#include "sanitizer_common/sanitizer_flags.h"

const char *argv0;

int main(int argc, char **argv) {
  argv0 = argv[0];
  testing::GTEST_FLAG(death_test_style) = "threadsafe";
  testing::InitGoogleTest(&argc, argv);
  __sanitizer::SetCommonFlagsDefaults();
  return RUN_ALL_TESTS();
}
