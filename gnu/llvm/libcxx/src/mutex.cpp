//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <__assert>
#include <__thread/id.h>
#include <__utility/exception_guard.h>
#include <limits>
#include <mutex>

#include "include/atomic_support.h"

#if defined(__ELF__) && defined(_LIBCPP_LINK_PTHREAD_LIB)
#  pragma comment(lib, "pthread")
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// ~mutex is defined elsewhere

void mutex::lock() {
  int ec = __libcpp_mutex_lock(&__m_);
  if (ec)
    __throw_system_error(ec, "mutex lock failed");
}

bool mutex::try_lock() noexcept { return __libcpp_mutex_trylock(&__m_); }

void mutex::unlock() noexcept {
  int ec = __libcpp_mutex_unlock(&__m_);
  (void)ec;
  _LIBCPP_ASSERT_VALID_EXTERNAL_API_CALL(
      ec == 0, "call to mutex::unlock failed. A possible reason is that the mutex wasn't locked");
}

// recursive_mutex

recursive_mutex::recursive_mutex() {
  int ec = __libcpp_recursive_mutex_init(&__m_);
  if (ec)
    __throw_system_error(ec, "recursive_mutex constructor failed");
}

recursive_mutex::~recursive_mutex() {
  int e = __libcpp_recursive_mutex_destroy(&__m_);
  (void)e;
  _LIBCPP_ASSERT_VALID_EXTERNAL_API_CALL(e == 0, "call to ~recursive_mutex() failed");
}

void recursive_mutex::lock() {
  int ec = __libcpp_recursive_mutex_lock(&__m_);
  if (ec)
    __throw_system_error(ec, "recursive_mutex lock failed");
}

void recursive_mutex::unlock() noexcept {
  int e = __libcpp_recursive_mutex_unlock(&__m_);
  (void)e;
  _LIBCPP_ASSERT_VALID_EXTERNAL_API_CALL(
      e == 0, "call to recursive_mutex::unlock() failed. A possible reason is that the mutex wasn't locked");
}

bool recursive_mutex::try_lock() noexcept { return __libcpp_recursive_mutex_trylock(&__m_); }

// timed_mutex

timed_mutex::timed_mutex() : __locked_(false) {}

timed_mutex::~timed_mutex() { lock_guard<mutex> _(__m_); }

void timed_mutex::lock() {
  unique_lock<mutex> lk(__m_);
  while (__locked_)
    __cv_.wait(lk);
  __locked_ = true;
}

bool timed_mutex::try_lock() noexcept {
  unique_lock<mutex> lk(__m_, try_to_lock);
  if (lk.owns_lock() && !__locked_) {
    __locked_ = true;
    return true;
  }
  return false;
}

void timed_mutex::unlock() noexcept {
  lock_guard<mutex> _(__m_);
  __locked_ = false;
  __cv_.notify_one();
}

// recursive_timed_mutex

recursive_timed_mutex::recursive_timed_mutex() : __count_(0), __id_{} {}

recursive_timed_mutex::~recursive_timed_mutex() { lock_guard<mutex> _(__m_); }

void recursive_timed_mutex::lock() {
  __thread_id id = this_thread::get_id();
  unique_lock<mutex> lk(__m_);
  if (id == __id_) {
    if (__count_ == numeric_limits<size_t>::max())
      __throw_system_error(EAGAIN, "recursive_timed_mutex lock limit reached");
    ++__count_;
    return;
  }
  while (__count_ != 0)
    __cv_.wait(lk);
  __count_ = 1;
  __id_    = id;
}

bool recursive_timed_mutex::try_lock() noexcept {
  __thread_id id = this_thread::get_id();
  unique_lock<mutex> lk(__m_, try_to_lock);
  if (lk.owns_lock() && (__count_ == 0 || id == __id_)) {
    if (__count_ == numeric_limits<size_t>::max())
      return false;
    ++__count_;
    __id_ = id;
    return true;
  }
  return false;
}

void recursive_timed_mutex::unlock() noexcept {
  unique_lock<mutex> lk(__m_);
  if (--__count_ == 0) {
    __id_.__reset();
    lk.unlock();
    __cv_.notify_one();
  }
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS
