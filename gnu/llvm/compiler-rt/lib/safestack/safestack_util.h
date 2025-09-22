//===-- safestack_util.h --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains utility code for SafeStack implementation.
//
//===----------------------------------------------------------------------===//

#ifndef SAFESTACK_UTIL_H
#define SAFESTACK_UTIL_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

namespace safestack {

#define SFS_CHECK(a)                                                  \
  do {                                                                \
    if (!(a)) {                                                       \
      fprintf(stderr, "safestack CHECK failed: %s:%d %s\n", __FILE__, \
              __LINE__, #a);                                          \
      abort();                                                        \
    };                                                                \
  } while (false)

inline size_t RoundUpTo(size_t size, size_t boundary) {
  SFS_CHECK((boundary & (boundary - 1)) == 0);
  return (size + boundary - 1) & ~(boundary - 1);
}

inline constexpr bool IsAligned(size_t a, size_t alignment) {
  return (a & (alignment - 1)) == 0;
}

class MutexLock {
 public:
  explicit MutexLock(pthread_mutex_t &mutex) : mutex_(&mutex) {
    pthread_mutex_lock(mutex_);
  }
  ~MutexLock() { pthread_mutex_unlock(mutex_); }

 private:
  pthread_mutex_t *mutex_ = nullptr;
};

}  // namespace safestack

#endif  // SAFESTACK_UTIL_H
