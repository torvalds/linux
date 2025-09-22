//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MUTEX_UNIQUE_LOCK_H
#define _LIBCPP___MUTEX_UNIQUE_LOCK_H

#include <__chrono/duration.h>
#include <__chrono/time_point.h>
#include <__config>
#include <__memory/addressof.h>
#include <__mutex/tag_types.h>
#include <__system_error/system_error.h>
#include <__utility/swap.h>
#include <cerrno>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#ifndef _LIBCPP_HAS_NO_THREADS

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Mutex>
class _LIBCPP_TEMPLATE_VIS unique_lock {
public:
  typedef _Mutex mutex_type;

private:
  mutex_type* __m_;
  bool __owns_;

public:
  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI unique_lock() _NOEXCEPT : __m_(nullptr), __owns_(false) {}
  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI explicit unique_lock(mutex_type& __m)
      : __m_(std::addressof(__m)), __owns_(true) {
    __m_->lock();
  }

  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI unique_lock(mutex_type& __m, defer_lock_t) _NOEXCEPT
      : __m_(std::addressof(__m)),
        __owns_(false) {}

  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI unique_lock(mutex_type& __m, try_to_lock_t)
      : __m_(std::addressof(__m)), __owns_(__m.try_lock()) {}

  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI unique_lock(mutex_type& __m, adopt_lock_t)
      : __m_(std::addressof(__m)), __owns_(true) {}

  template <class _Clock, class _Duration>
  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI unique_lock(mutex_type& __m, const chrono::time_point<_Clock, _Duration>& __t)
      : __m_(std::addressof(__m)), __owns_(__m.try_lock_until(__t)) {}

  template <class _Rep, class _Period>
  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI unique_lock(mutex_type& __m, const chrono::duration<_Rep, _Period>& __d)
      : __m_(std::addressof(__m)), __owns_(__m.try_lock_for(__d)) {}

  _LIBCPP_HIDE_FROM_ABI ~unique_lock() {
    if (__owns_)
      __m_->unlock();
  }

  unique_lock(unique_lock const&)            = delete;
  unique_lock& operator=(unique_lock const&) = delete;

  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI unique_lock(unique_lock&& __u) _NOEXCEPT
      : __m_(__u.__m_),
        __owns_(__u.__owns_) {
    __u.__m_    = nullptr;
    __u.__owns_ = false;
  }

  _LIBCPP_HIDE_FROM_ABI unique_lock& operator=(unique_lock&& __u) _NOEXCEPT {
    if (__owns_)
      __m_->unlock();

    __m_        = __u.__m_;
    __owns_     = __u.__owns_;
    __u.__m_    = nullptr;
    __u.__owns_ = false;
    return *this;
  }

  void lock();
  bool try_lock();

  template <class _Rep, class _Period>
  bool try_lock_for(const chrono::duration<_Rep, _Period>& __d);

  template <class _Clock, class _Duration>
  bool try_lock_until(const chrono::time_point<_Clock, _Duration>& __t);

  void unlock();

  _LIBCPP_HIDE_FROM_ABI void swap(unique_lock& __u) _NOEXCEPT {
    std::swap(__m_, __u.__m_);
    std::swap(__owns_, __u.__owns_);
  }

  _LIBCPP_HIDE_FROM_ABI mutex_type* release() _NOEXCEPT {
    mutex_type* __m = __m_;
    __m_            = nullptr;
    __owns_         = false;
    return __m;
  }

  _LIBCPP_HIDE_FROM_ABI bool owns_lock() const _NOEXCEPT { return __owns_; }
  _LIBCPP_HIDE_FROM_ABI explicit operator bool() const _NOEXCEPT { return __owns_; }
  _LIBCPP_HIDE_FROM_ABI mutex_type* mutex() const _NOEXCEPT { return __m_; }
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(unique_lock);

template <class _Mutex>
void unique_lock<_Mutex>::lock() {
  if (__m_ == nullptr)
    __throw_system_error(EPERM, "unique_lock::lock: references null mutex");
  if (__owns_)
    __throw_system_error(EDEADLK, "unique_lock::lock: already locked");
  __m_->lock();
  __owns_ = true;
}

template <class _Mutex>
bool unique_lock<_Mutex>::try_lock() {
  if (__m_ == nullptr)
    __throw_system_error(EPERM, "unique_lock::try_lock: references null mutex");
  if (__owns_)
    __throw_system_error(EDEADLK, "unique_lock::try_lock: already locked");
  __owns_ = __m_->try_lock();
  return __owns_;
}

template <class _Mutex>
template <class _Rep, class _Period>
bool unique_lock<_Mutex>::try_lock_for(const chrono::duration<_Rep, _Period>& __d) {
  if (__m_ == nullptr)
    __throw_system_error(EPERM, "unique_lock::try_lock_for: references null mutex");
  if (__owns_)
    __throw_system_error(EDEADLK, "unique_lock::try_lock_for: already locked");
  __owns_ = __m_->try_lock_for(__d);
  return __owns_;
}

template <class _Mutex>
template <class _Clock, class _Duration>
bool unique_lock<_Mutex>::try_lock_until(const chrono::time_point<_Clock, _Duration>& __t) {
  if (__m_ == nullptr)
    __throw_system_error(EPERM, "unique_lock::try_lock_until: references null mutex");
  if (__owns_)
    __throw_system_error(EDEADLK, "unique_lock::try_lock_until: already locked");
  __owns_ = __m_->try_lock_until(__t);
  return __owns_;
}

template <class _Mutex>
void unique_lock<_Mutex>::unlock() {
  if (!__owns_)
    __throw_system_error(EPERM, "unique_lock::unlock: not locked");
  __m_->unlock();
  __owns_ = false;
}

template <class _Mutex>
inline _LIBCPP_HIDE_FROM_ABI void swap(unique_lock<_Mutex>& __x, unique_lock<_Mutex>& __y) _NOEXCEPT {
  __x.swap(__y);
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_HAS_NO_THREADS

#endif // _LIBCPP___MUTEX_UNIQUE_LOCK_H
