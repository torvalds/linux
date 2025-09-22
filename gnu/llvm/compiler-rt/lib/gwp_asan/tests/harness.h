//===-- harness.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_TESTS_HARNESS_H_
#define GWP_ASAN_TESTS_HARNESS_H_

#include <stdarg.h>

#if defined(__Fuchsia__)
#define ZXTEST_USE_STREAMABLE_MACROS
#include <zxtest/zxtest.h>
namespace testing = zxtest;
// zxtest defines a different ASSERT_DEATH, taking a lambda and an error message
// if death didn't occur, versus gtest taking a statement and a string to search
// for in the dying process. zxtest doesn't define an EXPECT_DEATH, so we use
// that in the tests below (which works as intended for gtest), and we define
// EXPECT_DEATH as a wrapper for zxtest's ASSERT_DEATH. Note that zxtest drops
// the functionality for checking for a particular message in death.
#define EXPECT_DEATH(X, Y) ASSERT_DEATH(([&] { X; }), "")
#else
#include "gtest/gtest.h"
#endif

#include "gwp_asan/guarded_pool_allocator.h"
#include "gwp_asan/optional/backtrace.h"
#include "gwp_asan/optional/printf.h"
#include "gwp_asan/optional/segv_handler.h"
#include "gwp_asan/options.h"

namespace gwp_asan {
namespace test {
// This printf-function getter allows other platforms (e.g. Android) to define
// their own signal-safe Printf function. In LLVM, we use
// `optional/printf_sanitizer_common.cpp` which supplies the __sanitizer::Printf
// for this purpose.
Printf_t getPrintfFunction();
}; // namespace test
}; // namespace gwp_asan

char *AllocateMemory(gwp_asan::GuardedPoolAllocator &GPA);
void DeallocateMemory(gwp_asan::GuardedPoolAllocator &GPA, void *Ptr);
void DeallocateMemory2(gwp_asan::GuardedPoolAllocator &GPA, void *Ptr);
void TouchMemory(void *Ptr);

void CheckOnlyOneGwpAsanCrash(const std::string &OutputBuffer);

class DefaultGuardedPoolAllocator : public ::testing::Test {
public:
  void SetUp() override {
    gwp_asan::options::Options Opts;
    MaxSimultaneousAllocations = Opts.MaxSimultaneousAllocations;
    GPA.init(Opts);
  }

  void TearDown() override { GPA.uninitTestOnly(); }

protected:
  gwp_asan::GuardedPoolAllocator GPA;
  decltype(gwp_asan::options::Options::MaxSimultaneousAllocations)
      MaxSimultaneousAllocations;
};

class CustomGuardedPoolAllocator : public ::testing::Test {
public:
  void
  InitNumSlots(decltype(gwp_asan::options::Options::MaxSimultaneousAllocations)
                   MaxSimultaneousAllocationsArg) {
    gwp_asan::options::Options Opts;
    Opts.MaxSimultaneousAllocations = MaxSimultaneousAllocationsArg;
    MaxSimultaneousAllocations = MaxSimultaneousAllocationsArg;
    GPA.init(Opts);
  }

  void TearDown() override { GPA.uninitTestOnly(); }

protected:
  gwp_asan::GuardedPoolAllocator GPA;
  decltype(gwp_asan::options::Options::MaxSimultaneousAllocations)
      MaxSimultaneousAllocations;
};

class BacktraceGuardedPoolAllocator
    : public ::testing::TestWithParam</* Recoverable */ bool> {
public:
  void SetUp() override {
    gwp_asan::options::Options Opts;
    Opts.Backtrace = gwp_asan::backtrace::getBacktraceFunction();
    GPA.init(Opts);

    // In recoverable mode, capture GWP-ASan logs to an internal buffer so that
    // we can search it in unit tests. For non-recoverable tests, the default
    // buffer is fine, as any tests should be EXPECT_DEATH()'d.
    Recoverable = GetParam();
    gwp_asan::Printf_t PrintfFunction = PrintfToBuffer;
    GetOutputBuffer().clear();
    if (!Recoverable)
      PrintfFunction = gwp_asan::test::getPrintfFunction();

    gwp_asan::segv_handler::installSignalHandlers(
        &GPA, PrintfFunction, gwp_asan::backtrace::getPrintBacktraceFunction(),
        gwp_asan::backtrace::getSegvBacktraceFunction(),
        /* Recoverable */ Recoverable);
  }

  void TearDown() override {
    GPA.uninitTestOnly();
    gwp_asan::segv_handler::uninstallSignalHandlers();
  }

protected:
  static std::string &GetOutputBuffer() {
    static std::string Buffer;
    return Buffer;
  }

  __attribute__((format(printf, 1, 2))) static void
  PrintfToBuffer(const char *Format, ...) {
    va_list AP;
    va_start(AP, Format);
    char Buffer[8192];
    vsnprintf(Buffer, sizeof(Buffer), Format, AP);
    GetOutputBuffer() += Buffer;
    va_end(AP);
  }

  gwp_asan::GuardedPoolAllocator GPA;
  bool Recoverable;
};

// https://github.com/google/googletest/blob/master/docs/advanced.md#death-tests-and-threads
using DefaultGuardedPoolAllocatorDeathTest = DefaultGuardedPoolAllocator;
using CustomGuardedPoolAllocatorDeathTest = CustomGuardedPoolAllocator;
using BacktraceGuardedPoolAllocatorDeathTest = BacktraceGuardedPoolAllocator;

#endif // GWP_ASAN_TESTS_HARNESS_H_
