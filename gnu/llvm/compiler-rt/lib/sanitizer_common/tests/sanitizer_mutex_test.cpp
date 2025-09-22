//===-- sanitizer_mutex_test.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_mutex.h"
#include "sanitizer_common/sanitizer_common.h"

#include "sanitizer_pthread_wrappers.h"

#include "gtest/gtest.h"

#include <string.h>

namespace __sanitizer {

template<typename MutexType>
class TestData {
 public:
  explicit TestData(MutexType *mtx)
      : mtx_(mtx) {
    for (int i = 0; i < kSize; i++)
      data_[i] = 0;
  }

  void Write() {
    Lock l(mtx_);
    T v0 = data_[0];
    for (int i = 0; i < kSize; i++) {
      mtx_->CheckLocked();
      CHECK_EQ(data_[i], v0);
      data_[i]++;
    }
  }

  void TryWrite() {
    if (!mtx_->TryLock())
      return;
    T v0 = data_[0];
    for (int i = 0; i < kSize; i++) {
      mtx_->CheckLocked();
      CHECK_EQ(data_[i], v0);
      data_[i]++;
    }
    mtx_->Unlock();
  }

  void Read() {
    ReadLock l(mtx_);
    T v0 = data_[0];
    for (int i = 0; i < kSize; i++) {
      mtx_->CheckReadLocked();
      CHECK_EQ(data_[i], v0);
    }
  }

  void Backoff() {
    volatile T data[kSize] = {};
    for (int i = 0; i < kSize; i++) {
      data[i]++;
      CHECK_EQ(data[i], 1);
    }
  }

 private:
  typedef GenericScopedLock<MutexType> Lock;
  typedef GenericScopedReadLock<MutexType> ReadLock;
  static const int kSize = 64;
  typedef u64 T;
  MutexType *mtx_;
  char pad_[kCacheLineSize];
  T data_[kSize];
};

const int kThreads = 8;
#if SANITIZER_DEBUG
const int kIters = 16*1024;
#else
const int kIters = 64*1024;
#endif

template<typename MutexType>
static void *lock_thread(void *param) {
  TestData<MutexType> *data = (TestData<MutexType>*)param;
  for (int i = 0; i < kIters; i++) {
    data->Write();
    data->Backoff();
  }
  return 0;
}

template<typename MutexType>
static void *try_thread(void *param) {
  TestData<MutexType> *data = (TestData<MutexType>*)param;
  for (int i = 0; i < kIters; i++) {
    data->TryWrite();
    data->Backoff();
  }
  return 0;
}

template <typename MutexType>
static void *read_write_thread(void *param) {
  TestData<MutexType> *data = (TestData<MutexType> *)param;
  for (int i = 0; i < kIters; i++) {
    if ((i % 10) == 0)
      data->Write();
    else
      data->Read();
    data->Backoff();
  }
  return 0;
}

template<typename MutexType>
static void check_locked(MutexType *mtx) {
  GenericScopedLock<MutexType> l(mtx);
  mtx->CheckLocked();
}

TEST(SanitizerCommon, SpinMutex) {
  SpinMutex mtx;
  mtx.Init();
  TestData<SpinMutex> data(&mtx);
  pthread_t threads[kThreads];
  for (int i = 0; i < kThreads; i++)
    PTHREAD_CREATE(&threads[i], 0, lock_thread<SpinMutex>, &data);
  for (int i = 0; i < kThreads; i++)
    PTHREAD_JOIN(threads[i], 0);
}

TEST(SanitizerCommon, SpinMutexTry) {
  SpinMutex mtx;
  mtx.Init();
  TestData<SpinMutex> data(&mtx);
  pthread_t threads[kThreads];
  for (int i = 0; i < kThreads; i++)
    PTHREAD_CREATE(&threads[i], 0, try_thread<SpinMutex>, &data);
  for (int i = 0; i < kThreads; i++)
    PTHREAD_JOIN(threads[i], 0);
}

TEST(SanitizerCommon, Mutex) {
  Mutex mtx;
  TestData<Mutex> data(&mtx);
  pthread_t threads[kThreads];
  for (int i = 0; i < kThreads; i++)
    PTHREAD_CREATE(&threads[i], 0, read_write_thread<Mutex>, &data);
  for (int i = 0; i < kThreads; i++) PTHREAD_JOIN(threads[i], 0);
}

TEST(SanitizerCommon, MutexTry) {
  Mutex mtx;
  TestData<Mutex> data(&mtx);
  pthread_t threads[kThreads];
  for (int i = 0; i < kThreads; i++)
    PTHREAD_CREATE(&threads[i], 0, try_thread<Mutex>, &data);
  for (int i = 0; i < kThreads; i++) PTHREAD_JOIN(threads[i], 0);
}

struct SemaphoreData {
  Semaphore *sem;
  bool done;
};

void *SemaphoreThread(void *arg) {
  auto data = static_cast<SemaphoreData *>(arg);
  data->sem->Wait();
  data->done = true;
  return nullptr;
}

TEST(SanitizerCommon, Semaphore) {
  Semaphore sem;
  sem.Post(1);
  sem.Wait();
  sem.Post(3);
  sem.Wait();
  sem.Wait();
  sem.Wait();

  SemaphoreData data = {&sem, false};
  pthread_t thread;
  PTHREAD_CREATE(&thread, nullptr, SemaphoreThread, &data);
  internal_sleep(1);
  CHECK(!data.done);
  sem.Post(1);
  PTHREAD_JOIN(thread, nullptr);
}

}  // namespace __sanitizer
