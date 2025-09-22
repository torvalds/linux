//===-- mutex.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_MUTEX_H_
#define GWP_ASAN_MUTEX_H_

#include "gwp_asan/platform_specific/mutex_fuchsia.h" // IWYU pragma: keep
#include "gwp_asan/platform_specific/mutex_posix.h"   // IWYU pragma: keep

namespace gwp_asan {
class Mutex final : PlatformMutex {
public:
  constexpr Mutex() = default;
  ~Mutex() = default;
  Mutex(const Mutex &) = delete;
  Mutex &operator=(const Mutex &) = delete;
  // Lock the mutex.
  void lock();
  // Nonblocking trylock of the mutex. Returns true if the lock was acquired.
  bool tryLock();
  // Unlock the mutex.
  void unlock();
};

class ScopedLock {
public:
  explicit ScopedLock(Mutex &Mx) : Mu(Mx) { Mu.lock(); }
  ~ScopedLock() { Mu.unlock(); }
  ScopedLock(const ScopedLock &) = delete;
  ScopedLock &operator=(const ScopedLock &) = delete;

private:
  Mutex &Mu;
};
} // namespace gwp_asan

#endif // GWP_ASAN_MUTEX_H_
