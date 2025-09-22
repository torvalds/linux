//===-- tsan_flags_test.cpp -----------------------------------------------===//
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
#include "tsan_flags.h"
#include "tsan_rtl.h"
#include "gtest/gtest.h"
#include <string>

namespace __tsan {

TEST(Flags, Basic) {
  // At least should not crash.
  Flags f;
  InitializeFlags(&f, 0);
  InitializeFlags(&f, "");
}

TEST(Flags, DefaultValues) {
  Flags f;

  f.enable_annotations = false;
  InitializeFlags(&f, "");
  EXPECT_EQ(true, f.enable_annotations);
}

static const char *options1 =
  " enable_annotations=0"
  " suppress_equal_stacks=0"
  " report_bugs=0"
  " report_thread_leaks=0"
  " report_destroy_locked=0"
  " report_mutex_bugs=0"
  " report_signal_unsafe=0"
  " report_atomic_races=0"
  " force_seq_cst_atomics=0"
  " halt_on_error=0"
  " atexit_sleep_ms=222"
  " profile_memory=qqq"
  " flush_memory_ms=444"
  " flush_symbolizer_ms=555"
  " memory_limit_mb=666"
  " stop_on_start=0"
  " running_on_valgrind=0"
  " history_size=5"
  " io_sync=1"
  " die_after_fork=true"
  "";

static const char *options2 =
  " enable_annotations=true"
  " suppress_equal_stacks=true"
  " report_bugs=true"
  " report_thread_leaks=true"
  " report_destroy_locked=true"
  " report_mutex_bugs=true"
  " report_signal_unsafe=true"
  " report_atomic_races=true"
  " force_seq_cst_atomics=true"
  " halt_on_error=true"
  " atexit_sleep_ms=123"
  " profile_memory=bbbbb"
  " flush_memory_ms=234"
  " flush_symbolizer_ms=345"
  " memory_limit_mb=456"
  " stop_on_start=true"
  " running_on_valgrind=true"
  " history_size=6"
  " io_sync=2"
  " die_after_fork=false"
  "";

void VerifyOptions1(Flags *f) {
  EXPECT_EQ(f->enable_annotations, 0);
  EXPECT_EQ(f->suppress_equal_stacks, 0);
  EXPECT_EQ(f->report_bugs, 0);
  EXPECT_EQ(f->report_thread_leaks, 0);
  EXPECT_EQ(f->report_destroy_locked, 0);
  EXPECT_EQ(f->report_mutex_bugs, 0);
  EXPECT_EQ(f->report_signal_unsafe, 0);
  EXPECT_EQ(f->report_atomic_races, 0);
  EXPECT_EQ(f->force_seq_cst_atomics, 0);
  EXPECT_EQ(f->halt_on_error, 0);
  EXPECT_EQ(f->atexit_sleep_ms, 222);
  EXPECT_EQ(f->profile_memory, std::string("qqq"));
  EXPECT_EQ(f->flush_memory_ms, 444);
  EXPECT_EQ(f->flush_symbolizer_ms, 555);
  EXPECT_EQ(f->memory_limit_mb, 666);
  EXPECT_EQ(f->stop_on_start, 0);
  EXPECT_EQ(f->running_on_valgrind, 0);
  EXPECT_EQ(f->history_size, (uptr)5);
  EXPECT_EQ(f->io_sync, 1);
  EXPECT_EQ(f->die_after_fork, true);
}

void VerifyOptions2(Flags *f) {
  EXPECT_EQ(f->enable_annotations, true);
  EXPECT_EQ(f->suppress_equal_stacks, true);
  EXPECT_EQ(f->report_bugs, true);
  EXPECT_EQ(f->report_thread_leaks, true);
  EXPECT_EQ(f->report_destroy_locked, true);
  EXPECT_EQ(f->report_mutex_bugs, true);
  EXPECT_EQ(f->report_signal_unsafe, true);
  EXPECT_EQ(f->report_atomic_races, true);
  EXPECT_EQ(f->force_seq_cst_atomics, true);
  EXPECT_EQ(f->halt_on_error, true);
  EXPECT_EQ(f->atexit_sleep_ms, 123);
  EXPECT_EQ(f->profile_memory, std::string("bbbbb"));
  EXPECT_EQ(f->flush_memory_ms, 234);
  EXPECT_EQ(f->flush_symbolizer_ms, 345);
  EXPECT_EQ(f->memory_limit_mb, 456);
  EXPECT_EQ(f->stop_on_start, true);
  EXPECT_EQ(f->running_on_valgrind, true);
  EXPECT_EQ(f->history_size, 6ul);
  EXPECT_EQ(f->io_sync, 2);
  EXPECT_EQ(f->die_after_fork, false);
}

static const char *test_default_options;
extern "C" const char *__tsan_default_options() {
  return test_default_options;
}

TEST(Flags, ParseDefaultOptions) {
  Flags f;

  test_default_options = options1;
  InitializeFlags(&f, "");
  VerifyOptions1(&f);

  test_default_options = options2;
  InitializeFlags(&f, "");
  VerifyOptions2(&f);
}

TEST(Flags, ParseEnvOptions) {
  Flags f;

  InitializeFlags(&f, options1);
  VerifyOptions1(&f);

  InitializeFlags(&f, options2);
  VerifyOptions2(&f);
}

TEST(Flags, ParsePriority) {
  Flags f;

  test_default_options = options2;
  InitializeFlags(&f, options1);
  VerifyOptions1(&f);

  test_default_options = options1;
  InitializeFlags(&f, options2);
  VerifyOptions2(&f);
}

}  // namespace __tsan
