//===-- asan_test_main.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
//===----------------------------------------------------------------------===//
#include "asan_test_utils.h"
#include "sanitizer_common/sanitizer_platform.h"

// Default ASAN_OPTIONS for the unit tests.
extern "C" const char* __asan_default_options() {
#if SANITIZER_APPLE
  // On Darwin, we default to `abort_on_error=1`, which would make tests run
  // much slower. Let's override this and run lit tests with 'abort_on_error=0'
  // and make sure we do not overwhelm the syslog while testing. Also, let's
  // turn symbolization off to speed up testing, especially when not running
  // with llvm-symbolizer but with atos.
  return "symbolize=false:abort_on_error=0:log_to_syslog=0";
#elif SANITIZER_SUPPRESS_LEAK_ON_PTHREAD_EXIT
  // On PowerPC and ARM Thumb, a couple tests involving pthread_exit fail due to
  // leaks detected by LSan. Symbolized leak report is required to apply a
  // suppression for this known problem.
  return "";
#else
  // Let's turn symbolization off to speed up testing (more than 3 times speedup
  // observed).
  return "symbolize=false";
#endif
}

int main(int argc, char **argv) {
  testing::GTEST_FLAG(death_test_style) = "threadsafe";
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
