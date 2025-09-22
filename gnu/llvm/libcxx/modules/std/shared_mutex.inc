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
  // [thread.sharedmutex.class], class shared_­mutex
  using std::shared_mutex;
  // [thread.sharedtimedmutex.class], class shared_­timed_­mutex
  using std::shared_timed_mutex;
  // [thread.lock.shared], class template shared_­lock
  using std::shared_lock;
  using std::swap;
#endif // _LIBCPP_HAS_NO_THREADS
} // namespace std
