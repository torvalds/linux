//===-- recoverable.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <atomic>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "gwp_asan/common.h"
#include "gwp_asan/crash_handler.h"
#include "gwp_asan/tests/harness.h"

TEST_P(BacktraceGuardedPoolAllocator, MultipleDoubleFreeOnlyOneOutput) {
  SCOPED_TRACE("");
  void *Ptr = AllocateMemory(GPA);
  DeallocateMemory(GPA, Ptr);
  // First time should generate a crash report.
  DeallocateMemory(GPA, Ptr);
  CheckOnlyOneGwpAsanCrash(GetOutputBuffer());
  ASSERT_NE(std::string::npos, GetOutputBuffer().find("Double Free"));

  // Ensure the crash is only reported once.
  GetOutputBuffer().clear();
  for (size_t i = 0; i < 100; ++i) {
    DeallocateMemory(GPA, Ptr);
    ASSERT_TRUE(GetOutputBuffer().empty());
  }
}

TEST_P(BacktraceGuardedPoolAllocator, MultipleInvalidFreeOnlyOneOutput) {
  SCOPED_TRACE("");
  char *Ptr = static_cast<char *>(AllocateMemory(GPA));
  // First time should generate a crash report.
  DeallocateMemory(GPA, Ptr + 1);
  CheckOnlyOneGwpAsanCrash(GetOutputBuffer());
  ASSERT_NE(std::string::npos, GetOutputBuffer().find("Invalid (Wild) Free"));

  // Ensure the crash is only reported once.
  GetOutputBuffer().clear();
  for (size_t i = 0; i < 100; ++i) {
    DeallocateMemory(GPA, Ptr + 1);
    ASSERT_TRUE(GetOutputBuffer().empty());
  }
}

TEST_P(BacktraceGuardedPoolAllocator, MultipleUseAfterFreeOnlyOneOutput) {
  SCOPED_TRACE("");
  void *Ptr = AllocateMemory(GPA);
  DeallocateMemory(GPA, Ptr);
  // First time should generate a crash report.
  TouchMemory(Ptr);
  ASSERT_NE(std::string::npos, GetOutputBuffer().find("Use After Free"));

  // Ensure the crash is only reported once.
  GetOutputBuffer().clear();
  for (size_t i = 0; i < 100; ++i) {
    TouchMemory(Ptr);
    ASSERT_TRUE(GetOutputBuffer().empty());
  }
}

TEST_P(BacktraceGuardedPoolAllocator, MultipleBufferOverflowOnlyOneOutput) {
  SCOPED_TRACE("");
  char *Ptr = static_cast<char *>(AllocateMemory(GPA));
  // First time should generate a crash report.
  TouchMemory(Ptr - 16);
  TouchMemory(Ptr + 16);
  CheckOnlyOneGwpAsanCrash(GetOutputBuffer());
  if (GetOutputBuffer().find("Buffer Overflow") == std::string::npos &&
      GetOutputBuffer().find("Buffer Underflow") == std::string::npos)
    FAIL() << "Failed to detect buffer underflow/overflow:\n"
           << GetOutputBuffer();

  // Ensure the crash is only reported once.
  GetOutputBuffer().clear();
  for (size_t i = 0; i < 100; ++i) {
    TouchMemory(Ptr - 16);
    TouchMemory(Ptr + 16);
    ASSERT_TRUE(GetOutputBuffer().empty()) << GetOutputBuffer();
  }
}

TEST_P(BacktraceGuardedPoolAllocator, OneDoubleFreeOneUseAfterFree) {
  SCOPED_TRACE("");
  void *Ptr = AllocateMemory(GPA);
  DeallocateMemory(GPA, Ptr);
  // First time should generate a crash report.
  DeallocateMemory(GPA, Ptr);
  CheckOnlyOneGwpAsanCrash(GetOutputBuffer());
  ASSERT_NE(std::string::npos, GetOutputBuffer().find("Double Free"));

  // Ensure the crash is only reported once.
  GetOutputBuffer().clear();
  for (size_t i = 0; i < 100; ++i) {
    DeallocateMemory(GPA, Ptr);
    ASSERT_TRUE(GetOutputBuffer().empty());
  }
}

// We use double-free to detect that each slot can generate as single error.
// Use-after-free would also be acceptable, but buffer-overflow wouldn't be, as
// the random left/right alignment means that one right-overflow can disable
// page protections, and a subsequent left-overflow of a slot that's on the
// right hand side may not trap.
TEST_P(BacktraceGuardedPoolAllocator, OneErrorReportPerSlot) {
  SCOPED_TRACE("");
  std::vector<void *> Ptrs;
  for (size_t i = 0; i < GPA.getAllocatorState()->MaxSimultaneousAllocations;
       ++i) {
    void *Ptr = AllocateMemory(GPA);
    ASSERT_NE(Ptr, nullptr);
    Ptrs.push_back(Ptr);
    DeallocateMemory(GPA, Ptr);
    DeallocateMemory(GPA, Ptr);
    CheckOnlyOneGwpAsanCrash(GetOutputBuffer());
    ASSERT_NE(std::string::npos, GetOutputBuffer().find("Double Free"));
    // Ensure the crash from this slot is only reported once.
    GetOutputBuffer().clear();
    DeallocateMemory(GPA, Ptr);
    ASSERT_TRUE(GetOutputBuffer().empty());
    // Reset the buffer, as we're gonna move to the next allocation.
    GetOutputBuffer().clear();
  }

  // All slots should have been used. No further errors should occur.
  for (size_t i = 0; i < 100; ++i)
    ASSERT_EQ(AllocateMemory(GPA), nullptr);
  for (void *Ptr : Ptrs) {
    DeallocateMemory(GPA, Ptr);
    TouchMemory(Ptr);
  }
  ASSERT_TRUE(GetOutputBuffer().empty());
}

void singleAllocThrashTask(gwp_asan::GuardedPoolAllocator *GPA,
                           std::atomic<bool> *StartingGun,
                           unsigned NumIterations, unsigned Job, char *Ptr) {
  while (!*StartingGun) {
    // Wait for starting gun.
  }

  for (unsigned i = 0; i < NumIterations; ++i) {
    switch (Job) {
    case 0:
      DeallocateMemory(*GPA, Ptr);
      break;
    case 1:
      DeallocateMemory(*GPA, Ptr + 1);
      break;
    case 2:
      TouchMemory(Ptr);
      break;
    case 3:
      TouchMemory(Ptr - 16);
      TouchMemory(Ptr + 16);
      break;
    default:
      __builtin_trap();
    }
  }
}

void runInterThreadThrashingSingleAlloc(unsigned NumIterations,
                                        gwp_asan::GuardedPoolAllocator *GPA) {
  std::atomic<bool> StartingGun{false};
  std::vector<std::thread> Threads;
  constexpr unsigned kNumThreads = 4;

  char *Ptr = static_cast<char *>(AllocateMemory(*GPA));

  for (unsigned i = 0; i < kNumThreads; ++i) {
    Threads.emplace_back(singleAllocThrashTask, GPA, &StartingGun,
                         NumIterations, i, Ptr);
  }

  StartingGun = true;

  for (auto &T : Threads)
    T.join();
}

TEST_P(BacktraceGuardedPoolAllocator, InterThreadThrashingSingleAlloc) {
  SCOPED_TRACE("");
  constexpr unsigned kNumIterations = 100000;
  runInterThreadThrashingSingleAlloc(kNumIterations, &GPA);
  CheckOnlyOneGwpAsanCrash(GetOutputBuffer());
}
