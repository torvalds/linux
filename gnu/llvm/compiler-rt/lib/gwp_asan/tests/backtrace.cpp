//===-- backtrace.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <regex>
#include <string>

#include "gwp_asan/common.h"
#include "gwp_asan/crash_handler.h"
#include "gwp_asan/tests/harness.h"

TEST_P(BacktraceGuardedPoolAllocatorDeathTest, DoubleFree) {
  void *Ptr = AllocateMemory(GPA);
  DeallocateMemory(GPA, Ptr);

  std::string DeathRegex = "Double Free.*DeallocateMemory2.*";
  DeathRegex.append("was deallocated.*DeallocateMemory[^2].*");
  DeathRegex.append("was allocated.*AllocateMemory");
  if (!Recoverable) {
    EXPECT_DEATH(DeallocateMemory2(GPA, Ptr), DeathRegex);
    return;
  }

  // For recoverable, assert that DeallocateMemory2() doesn't crash.
  DeallocateMemory2(GPA, Ptr);
  // Fuchsia's zxtest doesn't have an EXPECT_THAT(testing::MatchesRegex(), ...),
  // so check the regex manually.
  EXPECT_TRUE(std::regex_search(
      GetOutputBuffer(),
      std::basic_regex(DeathRegex, std::regex_constants::extended)))
      << "Regex \"" << DeathRegex
      << "\" was not found in input:\n============\n"
      << GetOutputBuffer() << "\n============";
}

TEST_P(BacktraceGuardedPoolAllocatorDeathTest, UseAfterFree) {
#if defined(__linux__) && __ARM_ARCH == 7
  // Incomplete backtrace on Armv7 Linux
  GTEST_SKIP();
#endif

  void *Ptr = AllocateMemory(GPA);
  DeallocateMemory(GPA, Ptr);

  std::string DeathRegex = "Use After Free.*TouchMemory.*";
  DeathRegex.append("was deallocated.*DeallocateMemory[^2].*");
  DeathRegex.append("was allocated.*AllocateMemory");

  if (!Recoverable) {
    EXPECT_DEATH(TouchMemory(Ptr), DeathRegex);
    return;
  }

  // For recoverable, assert that TouchMemory() doesn't crash.
  TouchMemory(Ptr);
  // Fuchsia's zxtest doesn't have an EXPECT_THAT(testing::MatchesRegex(), ...),
  // so check the regex manually.
  EXPECT_TRUE(std::regex_search(
      GetOutputBuffer(),
      std::basic_regex(DeathRegex, std::regex_constants::extended)))
      << "Regex \"" << DeathRegex
      << "\" was not found in input:\n============\n"
      << GetOutputBuffer() << "\n============";
  ;
}

TEST(Backtrace, Short) {
  gwp_asan::AllocationMetadata Meta;
  Meta.AllocationTrace.RecordBacktrace(
      [](uintptr_t *TraceBuffer, size_t /* Size */) -> size_t {
        TraceBuffer[0] = 123u;
        TraceBuffer[1] = 321u;
        return 2u;
      });
  uintptr_t TraceOutput[2] = {};
  EXPECT_EQ(2u, __gwp_asan_get_allocation_trace(&Meta, TraceOutput, 2));
  EXPECT_EQ(TraceOutput[0], 123u);
  EXPECT_EQ(TraceOutput[1], 321u);
}

TEST(Backtrace, ExceedsStorableLength) {
  gwp_asan::AllocationMetadata Meta;
  Meta.AllocationTrace.RecordBacktrace(
      [](uintptr_t *TraceBuffer, size_t Size) -> size_t {
        // Need to inintialise the elements that will be packed.
        memset(TraceBuffer, 0u, Size * sizeof(*TraceBuffer));

        // Indicate that there were more frames, and we just didn't have enough
        // room to store them.
        return Size * 2;
      });
  // Retrieve a frame from the collected backtrace, make sure it works E2E.
  uintptr_t TraceOutput;
  EXPECT_EQ(gwp_asan::AllocationMetadata::kMaxTraceLengthToCollect,
            __gwp_asan_get_allocation_trace(&Meta, &TraceOutput, 1));
}

TEST(Backtrace, ExceedsRetrievableAllocLength) {
  gwp_asan::AllocationMetadata Meta;
  constexpr size_t kNumFramesToStore = 3u;
  Meta.AllocationTrace.RecordBacktrace(
      [](uintptr_t *TraceBuffer, size_t /* Size */) -> size_t {
        memset(TraceBuffer, kNumFramesToStore,
               kNumFramesToStore * sizeof(*TraceBuffer));
        return kNumFramesToStore;
      });
  uintptr_t TraceOutput;
  // Ask for one element, get told that there's `kNumFramesToStore` available.
  EXPECT_EQ(kNumFramesToStore,
            __gwp_asan_get_allocation_trace(&Meta, &TraceOutput, 1));
}

TEST(Backtrace, ExceedsRetrievableDeallocLength) {
  gwp_asan::AllocationMetadata Meta;
  constexpr size_t kNumFramesToStore = 3u;
  Meta.DeallocationTrace.RecordBacktrace(
      [](uintptr_t *TraceBuffer, size_t /* Size */) -> size_t {
        memset(TraceBuffer, kNumFramesToStore,
               kNumFramesToStore * sizeof(*TraceBuffer));
        return kNumFramesToStore;
      });
  uintptr_t TraceOutput;
  // Ask for one element, get told that there's `kNumFramesToStore` available.
  EXPECT_EQ(kNumFramesToStore,
            __gwp_asan_get_deallocation_trace(&Meta, &TraceOutput, 1));
}
