//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <mutex>
#include <shared_mutex>
#if defined(__ELF__) && defined(_LIBCPP_LINK_PTHREAD_LIB)
#  pragma comment(lib, "pthread")
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// Shared Mutex Base
__shared_mutex_base::__shared_mutex_base() : __state_(0) {}

// Exclusive ownership

void __shared_mutex_base::lock() {
  unique_lock<mutex> lk(__mut_);
  while (__state_ & __write_entered_)
    __gate1_.wait(lk);
  __state_ |= __write_entered_;
  while (__state_ & __n_readers_)
    __gate2_.wait(lk);
}

bool __shared_mutex_base::try_lock() {
  unique_lock<mutex> lk(__mut_);
  if (__state_ == 0) {
    __state_ = __write_entered_;
    return true;
  }
  return false;
}

void __shared_mutex_base::unlock() {
  lock_guard<mutex> _(__mut_);
  __state_ = 0;
  __gate1_.notify_all();
}

// Shared ownership

void __shared_mutex_base::lock_shared() {
  unique_lock<mutex> lk(__mut_);
  while ((__state_ & __write_entered_) || (__state_ & __n_readers_) == __n_readers_)
    __gate1_.wait(lk);
  unsigned num_readers = (__state_ & __n_readers_) + 1;
  __state_ &= ~__n_readers_;
  __state_ |= num_readers;
}

bool __shared_mutex_base::try_lock_shared() {
  unique_lock<mutex> lk(__mut_);
  unsigned num_readers = __state_ & __n_readers_;
  if (!(__state_ & __write_entered_) && num_readers != __n_readers_) {
    ++num_readers;
    __state_ &= ~__n_readers_;
    __state_ |= num_readers;
    return true;
  }
  return false;
}

void __shared_mutex_base::unlock_shared() {
  lock_guard<mutex> _(__mut_);
  unsigned num_readers = (__state_ & __n_readers_) - 1;
  __state_ &= ~__n_readers_;
  __state_ |= num_readers;
  if (__state_ & __write_entered_) {
    if (num_readers == 0)
      __gate2_.notify_one();
  } else {
    if (num_readers == __n_readers_ - 1)
      __gate1_.notify_one();
  }
}

// Shared Timed Mutex
// These routines are here for ABI stability
shared_timed_mutex::shared_timed_mutex() : __base_() {}
void shared_timed_mutex::lock() { return __base_.lock(); }
bool shared_timed_mutex::try_lock() { return __base_.try_lock(); }
void shared_timed_mutex::unlock() { return __base_.unlock(); }
void shared_timed_mutex::lock_shared() { return __base_.lock_shared(); }
bool shared_timed_mutex::try_lock_shared() { return __base_.try_lock_shared(); }
void shared_timed_mutex::unlock_shared() { return __base_.unlock_shared(); }

_LIBCPP_END_NAMESPACE_STD
