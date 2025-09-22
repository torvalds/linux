// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STOP_TOKEN_ATOMIC_UNIQUE_GUARD_H
#define _LIBCPP___STOP_TOKEN_ATOMIC_UNIQUE_GUARD_H

#include <__bit/popcount.h>
#include <__config>
#include <atomic>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// This class implements an RAII unique_lock without a mutex.
// It uses std::atomic<State>,
// where State contains a lock bit and might contain other data,
// and LockedBit is the value of State when the lock bit is set, e.g  1 << 2
template <class _State, _State _LockedBit>
class _LIBCPP_AVAILABILITY_SYNC __atomic_unique_lock {
  static_assert(std::__libcpp_popcount(static_cast<unsigned long long>(_LockedBit)) == 1,
                "LockedBit must be an integer where only one bit is set");

  std::atomic<_State>& __state_;
  bool __is_locked_;

public:
  _LIBCPP_HIDE_FROM_ABI explicit __atomic_unique_lock(std::atomic<_State>& __state) noexcept
      : __state_(__state), __is_locked_(true) {
    __lock();
  }

  template <class _Pred>
  _LIBCPP_HIDE_FROM_ABI __atomic_unique_lock(std::atomic<_State>& __state, _Pred&& __give_up_locking) noexcept
      : __state_(__state), __is_locked_(false) {
    __is_locked_ = __lock_impl(__give_up_locking, __set_locked_bit, std::memory_order_acquire);
  }

  template <class _Pred, class _UnaryFunction>
  _LIBCPP_HIDE_FROM_ABI __atomic_unique_lock(
      std::atomic<_State>& __state,
      _Pred&& __give_up_locking,
      _UnaryFunction&& __state_after_lock,
      std::memory_order __locked_ordering) noexcept
      : __state_(__state), __is_locked_(false) {
    __is_locked_ = __lock_impl(__give_up_locking, __state_after_lock, __locked_ordering);
  }

  __atomic_unique_lock(const __atomic_unique_lock&)            = delete;
  __atomic_unique_lock(__atomic_unique_lock&&)                 = delete;
  __atomic_unique_lock& operator=(const __atomic_unique_lock&) = delete;
  __atomic_unique_lock& operator=(__atomic_unique_lock&&)      = delete;

  _LIBCPP_HIDE_FROM_ABI ~__atomic_unique_lock() {
    if (__is_locked_) {
      __unlock();
    }
  }

  _LIBCPP_HIDE_FROM_ABI bool __owns_lock() const noexcept { return __is_locked_; }

  _LIBCPP_HIDE_FROM_ABI void __lock() noexcept {
    const auto __never_give_up_locking = [](_State) { return false; };
    // std::memory_order_acquire because we'd like to make sure that all the read operations after the lock can read the
    // up-to-date values.
    __lock_impl(__never_give_up_locking, __set_locked_bit, std::memory_order_acquire);
    __is_locked_ = true;
  }

  _LIBCPP_HIDE_FROM_ABI void __unlock() noexcept {
    // unset the _LockedBit. `memory_order_release` because we need to make sure all the write operations before calling
    // `__unlock` will be made visible to other threads
    __state_.fetch_and(static_cast<_State>(~_LockedBit), std::memory_order_release);
    __state_.notify_all();
    __is_locked_ = false;
  }

private:
  template <class _Pred, class _UnaryFunction>
  _LIBCPP_HIDE_FROM_ABI bool
  __lock_impl(_Pred&& __give_up_locking, // while trying to lock the state, if the predicate returns true, give up
                                         // locking and return
              _UnaryFunction&& __state_after_lock,
              std::memory_order __locked_ordering) noexcept {
    // At this stage, until we exit the inner while loop, other than the atomic state, we are not reading any order
    // dependent values that is written on other threads, or writing anything that needs to be seen on other threads.
    // Therefore `memory_order_relaxed` is enough.
    _State __current_state = __state_.load(std::memory_order_relaxed);
    do {
      while (true) {
        if (__give_up_locking(__current_state)) {
          // user provided early return condition. fail to lock
          return false;
        } else if ((__current_state & _LockedBit) != 0) {
          // another thread has locked the state, we need to wait
          __state_.wait(__current_state, std::memory_order_relaxed);
          // when it is woken up by notifyAll or spuriously, the __state_
          // might have changed. reload the state
          // Note that the new state's _LockedBit may or may not equal to 0
          __current_state = __state_.load(std::memory_order_relaxed);
        } else {
          // at least for now, it is not locked. we can try `compare_exchange_weak` to lock it.
          // Note that the variable `__current_state`'s lock bit has to be 0 at this point.
          break;
        }
      }
    } while (!__state_.compare_exchange_weak(
        __current_state, // if __state_ has the same value of __current_state, lock bit must be zero before exchange and
                         // we are good to lock/exchange and return. If _state has a different value, because other
                         // threads locked it between the `break` statement above and this statement, exchange will fail
                         // and go back to the inner while loop above.
        __state_after_lock(__current_state), // state after lock. Usually it should be __current_state | _LockedBit.
                                             // Some use cases need to set other bits at the same time as an atomic
                                             // operation therefore we accept a function
        __locked_ordering,        // sucessful exchange order. Usually it should be std::memory_order_acquire.
                                  // Some use cases need more strict ordering therefore we accept it as a parameter
        std::memory_order_relaxed // fail to exchange order. We don't need any ordering as we are going back to the
                                  // inner while loop
        ));
    return true;
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr auto __set_locked_bit = [](_State __state) { return __state | _LockedBit; };
};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___STOP_TOKEN_ATOMIC_UNIQUE_GUARD_H
