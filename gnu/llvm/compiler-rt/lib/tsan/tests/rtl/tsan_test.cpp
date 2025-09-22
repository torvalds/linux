//===-- tsan_test.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_interface.h"
#include "tsan_test_util.h"
#include "gtest/gtest.h"

static void foo() {}
static void bar() {}

TEST_F(ThreadSanitizer, FuncCall) {
  ScopedThread t1, t2;
  MemLoc l;
  t1.Write1(l);
  t2.Call(foo);
  t2.Call(bar);
  t2.Write1(l, true);
  t2.Return();
  t2.Return();
}

// We use this function instead of main, as ISO C++ forbids taking the address
// of main, which we need to pass inside __tsan_func_entry.
int run_tests(int argc, char **argv) {
  TestMutexBeforeInit();  // Mutexes must be usable before __tsan_init();
  __tsan_init();
  __tsan_func_entry(__builtin_return_address(0));
  __tsan_func_entry((void*)((intptr_t)&run_tests + 1));

  testing::GTEST_FLAG(death_test_style) = "threadsafe";
  testing::InitGoogleTest(&argc, argv);
  int res = RUN_ALL_TESTS();

  __tsan_func_exit();
  __tsan_func_exit();
  return res;
}

const char *argv0;

#ifdef __APPLE__
// On Darwin, turns off symbolication and crash logs to make tests faster.
extern "C" const char* __tsan_default_options() {
  return "symbolize=false:abort_on_error=0";
}
#endif

int main(int argc, char **argv) {
  argv0 = argv[0];
  return run_tests(argc, argv);
}
