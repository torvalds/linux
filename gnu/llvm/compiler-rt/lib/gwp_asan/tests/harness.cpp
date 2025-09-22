//===-- harness.cpp ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/tests/harness.h"

#include <string>

// Optnone to ensure that the calls to these functions are not optimized away,
// as we're looking for them in the backtraces.
__attribute__((optnone)) char *
AllocateMemory(gwp_asan::GuardedPoolAllocator &GPA) {
  return static_cast<char *>(GPA.allocate(1));
}
__attribute__((optnone)) void
DeallocateMemory(gwp_asan::GuardedPoolAllocator &GPA, void *Ptr) {
  GPA.deallocate(Ptr);
}
__attribute__((optnone)) void
DeallocateMemory2(gwp_asan::GuardedPoolAllocator &GPA, void *Ptr) {
  GPA.deallocate(Ptr);
}
__attribute__((optnone)) void TouchMemory(void *Ptr) {
  *(reinterpret_cast<volatile char *>(Ptr)) = 7;
}

void CheckOnlyOneGwpAsanCrash(const std::string &OutputBuffer) {
  const char *kGwpAsanErrorString = "GWP-ASan detected a memory error";
  size_t FirstIndex = OutputBuffer.find(kGwpAsanErrorString);
  ASSERT_NE(FirstIndex, std::string::npos) << "Didn't detect a GWP-ASan crash";
  ASSERT_EQ(OutputBuffer.find(kGwpAsanErrorString, FirstIndex + 1),
            std::string::npos)
      << "Detected more than one GWP-ASan crash:\n"
      << OutputBuffer;
}

// Fuchsia does not support recoverable GWP-ASan.
#if defined(__Fuchsia__)
INSTANTIATE_TEST_SUITE_P(RecoverableAndNonRecoverableTests,
                         BacktraceGuardedPoolAllocatorDeathTest,
                         /* Recoverable */ testing::Values(false));
#else
INSTANTIATE_TEST_SUITE_P(RecoverableTests, BacktraceGuardedPoolAllocator,
                         /* Recoverable */ testing::Values(true));
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(BacktraceGuardedPoolAllocator);
INSTANTIATE_TEST_SUITE_P(RecoverableAndNonRecoverableTests,
                         BacktraceGuardedPoolAllocatorDeathTest,
                         /* Recoverable */ testing::Bool());
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(BacktraceGuardedPoolAllocatorDeathTest);
#endif
