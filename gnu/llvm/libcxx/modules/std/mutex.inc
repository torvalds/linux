// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
#ifndef _LIBCPP_HAS_NO_THREADS
  // [thread.mutex.class], class mutex
  using std::mutex;
  // [thread.mutex.recursive], class recursive_mutex
  using std::recursive_mutex;
  // [thread.timedmutex.class] class timed_mutex
  using std::timed_mutex;
  // [thread.timedmutex.recursive], class recursive_timed_mutex
  using std::recursive_timed_mutex;

  using std::adopt_lock_t;
  using std::defer_lock_t;
  using std::try_to_lock_t;

  using std::adopt_lock;
  using std::defer_lock;
  using std::try_to_lock;

  // [thread.lock], locks
  using std::lock_guard;
  using std::scoped_lock;
  using std::unique_lock;

  using std::swap;

  // [thread.lock.algorithm], generic locking algorithms
  using std::lock;
  using std::try_lock;
#endif // _LIBCPP_HAS_NO_THREADS

  using std::once_flag;

  using std::call_once;
} // namespace std
