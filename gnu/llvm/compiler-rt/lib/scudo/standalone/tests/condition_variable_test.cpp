//===-- condition_variable_test.cpp -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "common.h"
#include "condition_variable.h"
#include "mutex.h"

#include <thread>

template <typename ConditionVariableT> void simpleWaitAndNotifyAll() {
  constexpr scudo::u32 NumThreads = 2;
  constexpr scudo::u32 CounterMax = 1024;
  std::thread Threads[NumThreads];

  scudo::HybridMutex M;
  ConditionVariableT CV;
  CV.bindTestOnly(M);
  scudo::u32 Counter = 0;

  for (scudo::u32 I = 0; I < NumThreads; ++I) {
    Threads[I] = std::thread(
        [&](scudo::u32 Id) {
          do {
            scudo::ScopedLock L(M);
            if (Counter % NumThreads != Id && Counter < CounterMax)
              CV.wait(M);
            if (Counter >= CounterMax) {
              break;
            } else {
              ++Counter;
              CV.notifyAll(M);
            }
          } while (true);
        },
        I);
  }

  for (std::thread &T : Threads)
    T.join();

  EXPECT_EQ(Counter, CounterMax);
}

TEST(ScudoConditionVariableTest, DummyCVWaitAndNotifyAll) {
  simpleWaitAndNotifyAll<scudo::ConditionVariableDummy>();
}

#ifdef SCUDO_LINUX
TEST(ScudoConditionVariableTest, LinuxCVWaitAndNotifyAll) {
  simpleWaitAndNotifyAll<scudo::ConditionVariableLinux>();
}
#endif
