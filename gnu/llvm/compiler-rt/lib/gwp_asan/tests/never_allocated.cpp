//===-- never_allocated.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <string>

#include "gwp_asan/common.h"
#include "gwp_asan/crash_handler.h"
#include "gwp_asan/tests/harness.h"

TEST_P(BacktraceGuardedPoolAllocatorDeathTest, NeverAllocated) {
  SCOPED_TRACE("");
  void *Ptr = GPA.allocate(0x1000);
  GPA.deallocate(Ptr);

  std::string DeathNeedle =
      "GWP-ASan cannot provide any more information about this error";

  // Trigger a guard page in a completely different slot that's never allocated.
  // Previously, there was a bug that this would result in nullptr-dereference
  // in the posix crash handler.
  char *volatile NeverAllocatedPtr = static_cast<char *>(Ptr) + 0x3000;
  if (!Recoverable) {
    EXPECT_DEATH(*NeverAllocatedPtr = 0, DeathNeedle);
    return;
  }

  *NeverAllocatedPtr = 0;
  CheckOnlyOneGwpAsanCrash(GetOutputBuffer());
  ASSERT_NE(std::string::npos, GetOutputBuffer().find(DeathNeedle));

  // Check that subsequent invalid touches of the pool don't print a report.
  GetOutputBuffer().clear();
  for (size_t i = 0; i < 100; ++i) {
    *NeverAllocatedPtr = 0;
    *(NeverAllocatedPtr + 0x2000) = 0;
    *(NeverAllocatedPtr + 0x3000) = 0;
    ASSERT_TRUE(GetOutputBuffer().empty());
  }

  // Check that reports on the other slots still report a double-free, but only
  // once.
  GetOutputBuffer().clear();
  GPA.deallocate(Ptr);
  ASSERT_NE(std::string::npos, GetOutputBuffer().find("Double Free"));
  GetOutputBuffer().clear();
  for (size_t i = 0; i < 100; ++i) {
    DeallocateMemory(GPA, Ptr);
    ASSERT_TRUE(GetOutputBuffer().empty());
  }
}
