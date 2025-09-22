// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STOP_TOKEN_STOP_STATE_H
#define _LIBCPP___STOP_TOKEN_STOP_STATE_H

#include <__assert>
#include <__config>
#include <__stop_token/atomic_unique_lock.h>
#include <__stop_token/intrusive_list_view.h>
#include <__thread/id.h>
#include <atomic>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_THREADS)

struct __stop_callback_base : __intrusive_node_base<__stop_callback_base> {
  using __callback_fn_t = void(__stop_callback_base*) noexcept;
  _LIBCPP_HIDE_FROM_ABI explicit __stop_callback_base(__callback_fn_t* __callback_fn) : __callback_fn_(__callback_fn) {}

  _LIBCPP_HIDE_FROM_ABI void __invoke() noexcept { __callback_fn_(this); }

  __callback_fn_t* __callback_fn_;
  atomic<bool> __completed_ = false;
  bool* __destroyed_        = nullptr;
};

class __stop_state {
  static constexpr uint32_t __stop_requested_bit        = 1;
  static constexpr uint32_t __callback_list_locked_bit  = 1 << 1;
  static constexpr uint32_t __stop_source_counter_shift = 2;

  // The "stop_source counter" is not used for lifetime reference counting.
  // When the number of stop_source reaches 0, the remaining stop_tokens's
  // stop_possible will return false. We need this counter to track this.
  //
  // The "callback list locked" bit implements the atomic_unique_lock to
  // guard the operations on the callback list
  //
  //       31 - 2          |  1                   |    0           |
  //  stop_source counter  | callback list locked | stop_requested |
  atomic<uint32_t> __state_ = 0;

  // Reference count for stop_token + stop_callback + stop_source
  // When the counter reaches zero, the state is destroyed
  // It is used by __intrusive_shared_ptr, but it is stored here for better layout
  atomic<uint32_t> __ref_count_ = 0;

  using __state_t            = uint32_t;
  using __callback_list_lock = __atomic_unique_lock<__state_t, __callback_list_locked_bit>;
  using __callback_list      = __intrusive_list_view<__stop_callback_base>;

  __callback_list __callback_list_;
  __thread_id __requesting_thread_;

public:
  _LIBCPP_HIDE_FROM_ABI __stop_state() noexcept = default;

  _LIBCPP_HIDE_FROM_ABI void __increment_stop_source_counter() noexcept {
    _LIBCPP_ASSERT_UNCATEGORIZED(
        __state_.load(std::memory_order_relaxed) <= static_cast<__state_t>(~(1 << __stop_source_counter_shift)),
        "stop_source's counter reaches the maximum. Incrementing the counter will overflow");
    __state_.fetch_add(1 << __stop_source_counter_shift, std::memory_order_relaxed);
  }

  // We are not destroying the object after counter decrements to zero, nor do we have
  // operations depend on the ordering of decrementing the counter. relaxed is enough.
  _LIBCPP_HIDE_FROM_ABI void __decrement_stop_source_counter() noexcept {
    _LIBCPP_ASSERT_UNCATEGORIZED(
        __state_.load(std::memory_order_relaxed) >= static_cast<__state_t>(1 << __stop_source_counter_shift),
        "stop_source's counter is 0. Decrementing the counter will underflow");
    __state_.fetch_sub(1 << __stop_source_counter_shift, std::memory_order_relaxed);
  }

  _LIBCPP_HIDE_FROM_ABI bool __stop_requested() const noexcept {
    // acquire because [thread.stoptoken.intro] A call to request_stop that returns true
    // synchronizes with a call to stop_requested on an associated stop_token or stop_source
    // object that returns true.
    // request_stop's compare_exchange_weak has release which syncs with this acquire
    return (__state_.load(std::memory_order_acquire) & __stop_requested_bit) != 0;
  }

  _LIBCPP_HIDE_FROM_ABI bool __stop_possible_for_stop_token() const noexcept {
    // [stoptoken.mem] false if "a stop request was not made and there are no associated stop_source objects"
    // Todo: Can this be std::memory_order_relaxed as the standard does not say anything except not to introduce data
    // race?
    __state_t __curent_state = __state_.load(std::memory_order_acquire);
    return ((__curent_state & __stop_requested_bit) != 0) || ((__curent_state >> __stop_source_counter_shift) != 0);
  }

  _LIBCPP_AVAILABILITY_SYNC _LIBCPP_HIDE_FROM_ABI bool __request_stop() noexcept {
    auto __cb_list_lock = __try_lock_for_request_stop();
    if (!__cb_list_lock.__owns_lock()) {
      return false;
    }
    __requesting_thread_ = this_thread::get_id();

    while (!__callback_list_.__empty()) {
      auto __cb = __callback_list_.__pop_front();

      // allow other callbacks to be removed while invoking the current callback
      __cb_list_lock.__unlock();

      bool __destroyed   = false;
      __cb->__destroyed_ = &__destroyed;

      __cb->__invoke();

      // __cb's invoke function could potentially delete itself. We need to check before accessing __cb's member
      if (!__destroyed) {
        // needs to set __destroyed_ pointer to nullptr, otherwise it points to a local variable
        // which is to be destroyed at the end of the loop
        __cb->__destroyed_ = nullptr;

        // [stopcallback.cons] If callback is concurrently executing on another thread, then the return
        // from the invocation of callback strongly happens before ([intro.races]) callback is destroyed.
        // this release syncs with the acquire in the remove_callback
        __cb->__completed_.store(true, std::memory_order_release);
        __cb->__completed_.notify_all();
      }

      __cb_list_lock.__lock();
    }

    return true;
  }

  _LIBCPP_AVAILABILITY_SYNC _LIBCPP_HIDE_FROM_ABI bool __add_callback(__stop_callback_base* __cb) noexcept {
    // If it is already stop_requested. Do not try to request it again.
    const auto __give_up_trying_to_lock_condition = [__cb](__state_t __state) {
      if ((__state & __stop_requested_bit) != 0) {
        // already stop requested, synchronously run the callback and no need to lock the list again
        __cb->__invoke();
        return true;
      }
      // no stop source. no need to lock the list to add the callback as it can never be invoked
      return (__state >> __stop_source_counter_shift) == 0;
    };

    __callback_list_lock __cb_list_lock(__state_, __give_up_trying_to_lock_condition);

    if (!__cb_list_lock.__owns_lock()) {
      return false;
    }

    __callback_list_.__push_front(__cb);

    return true;
    // unlock here: [thread.stoptoken.intro] Registration of a callback synchronizes with the invocation of
    // that callback.
    // Note: this release sync with the acquire in the request_stop' __try_lock_for_request_stop
  }

  // called by the destructor of stop_callback
  _LIBCPP_AVAILABILITY_SYNC _LIBCPP_HIDE_FROM_ABI void __remove_callback(__stop_callback_base* __cb) noexcept {
    __callback_list_lock __cb_list_lock(__state_);

    // under below condition, the request_stop call just popped __cb from the list and could execute it now
    bool __potentially_executing_now = __cb->__prev_ == nullptr && !__callback_list_.__is_head(__cb);

    if (__potentially_executing_now) {
      auto __requested_thread = __requesting_thread_;
      __cb_list_lock.__unlock();

      if (std::this_thread::get_id() != __requested_thread) {
        // [stopcallback.cons] If callback is concurrently executing on another thread, then the return
        // from the invocation of callback strongly happens before ([intro.races]) callback is destroyed.
        __cb->__completed_.wait(false, std::memory_order_acquire);
      } else {
        // The destructor of stop_callback runs on the same thread of the thread that invokes the callback.
        // The callback is potentially invoking its own destuctor. Set the flag to avoid accessing destroyed
        // members on the invoking side
        if (__cb->__destroyed_) {
          *__cb->__destroyed_ = true;
        }
      }
    } else {
      __callback_list_.__remove(__cb);
    }
  }

private:
  _LIBCPP_AVAILABILITY_SYNC _LIBCPP_HIDE_FROM_ABI __callback_list_lock __try_lock_for_request_stop() noexcept {
    // If it is already stop_requested, do not try to request stop or lock the list again.
    const auto __lock_fail_condition = [](__state_t __state) { return (__state & __stop_requested_bit) != 0; };

    // set locked and requested bit at the same time
    const auto __after_lock_state = [](__state_t __state) {
      return __state | __callback_list_locked_bit | __stop_requested_bit;
    };

    // acq because [thread.stoptoken.intro] Registration of a callback synchronizes with the invocation of that
    //     callback. We are going to invoke the callback after getting the lock, acquire so that we can see the
    //     registration of a callback (and other writes that happens-before the add_callback)
    //     Note: the rel (unlock) in the add_callback syncs with this acq
    // rel because [thread.stoptoken.intro] A call to request_stop that returns true synchronizes with a call
    //     to stop_requested on an associated stop_token or stop_source object that returns true.
    //     We need to make sure that all writes (including user code) before request_stop will be made visible
    //     to the threads that waiting for `stop_requested == true`
    //     Note: this rel syncs with the acq in `stop_requested`
    const auto __locked_ordering = std::memory_order_acq_rel;

    return __callback_list_lock(__state_, __lock_fail_condition, __after_lock_state, __locked_ordering);
  }

  template <class _Tp>
  friend struct __intrusive_shared_ptr_traits;
};

template <class _Tp>
struct __intrusive_shared_ptr_traits;

template <>
struct __intrusive_shared_ptr_traits<__stop_state> {
  _LIBCPP_HIDE_FROM_ABI static atomic<uint32_t>& __get_atomic_ref_count(__stop_state& __state) {
    return __state.__ref_count_;
  }
};

#endif // _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_THREADS)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___STOP_TOKEN_STOP_STATE_H
