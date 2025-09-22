//===-- mutex_test.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "mutex.h"

#include <pthread.h>
#include <string.h>

class TestData {
public:
  explicit TestData(scudo::HybridMutex &M) : Mutex(M) {
    for (scudo::u32 I = 0; I < Size; I++)
      Data[I] = 0;
  }

  void write() {
    scudo::ScopedLock L(Mutex);
    T V0 = Data[0];
    for (scudo::u32 I = 0; I < Size; I++) {
      EXPECT_EQ(Data[I], V0);
      Data[I]++;
    }
  }

  void tryWrite() {
    if (!Mutex.tryLock())
      return;
    T V0 = Data[0];
    for (scudo::u32 I = 0; I < Size; I++) {
      EXPECT_EQ(Data[I], V0);
      Data[I]++;
    }
    Mutex.unlock();
  }

  void backoff() {
    volatile T LocalData[Size] = {};
    for (scudo::u32 I = 0; I < Size; I++) {
      LocalData[I] = LocalData[I] + 1;
      EXPECT_EQ(LocalData[I], 1U);
    }
  }

private:
  static const scudo::u32 Size = 64U;
  typedef scudo::u64 T;
  scudo::HybridMutex &Mutex;
  alignas(SCUDO_CACHE_LINE_SIZE) T Data[Size];
};

const scudo::u32 NumberOfThreads = 8;
#if SCUDO_DEBUG
const scudo::u32 NumberOfIterations = 4 * 1024;
#else
const scudo::u32 NumberOfIterations = 16 * 1024;
#endif

static void *lockThread(void *Param) {
  TestData *Data = reinterpret_cast<TestData *>(Param);
  for (scudo::u32 I = 0; I < NumberOfIterations; I++) {
    Data->write();
    Data->backoff();
  }
  return 0;
}

static void *tryThread(void *Param) {
  TestData *Data = reinterpret_cast<TestData *>(Param);
  for (scudo::u32 I = 0; I < NumberOfIterations; I++) {
    Data->tryWrite();
    Data->backoff();
  }
  return 0;
}

TEST(ScudoMutexTest, Mutex) {
  scudo::HybridMutex M;
  TestData Data(M);
  pthread_t Threads[NumberOfThreads];
  for (scudo::u32 I = 0; I < NumberOfThreads; I++)
    pthread_create(&Threads[I], 0, lockThread, &Data);
  for (scudo::u32 I = 0; I < NumberOfThreads; I++)
    pthread_join(Threads[I], 0);
}

TEST(ScudoMutexTest, MutexTry) {
  scudo::HybridMutex M;
  TestData Data(M);
  pthread_t Threads[NumberOfThreads];
  for (scudo::u32 I = 0; I < NumberOfThreads; I++)
    pthread_create(&Threads[I], 0, tryThread, &Data);
  for (scudo::u32 I = 0; I < NumberOfThreads; I++)
    pthread_join(Threads[I], 0);
}

TEST(ScudoMutexTest, MutexAssertHeld) {
  scudo::HybridMutex M;
  M.lock();
  M.assertHeld();
  M.unlock();
}
