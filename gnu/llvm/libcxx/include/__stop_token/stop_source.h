// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STOP_TOKEN_STOP_SOURCE_H
#define _LIBCPP___STOP_TOKEN_STOP_SOURCE_H

#include <__config>
#include <__stop_token/intrusive_shared_ptr.h>
#include <__stop_token/stop_state.h>
#include <__stop_token/stop_token.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN) && !defined(_LIBCPP_HAS_NO_THREADS)

struct nostopstate_t {
  explicit nostopstate_t() = default;
};

inline constexpr nostopstate_t nostopstate{};

class _LIBCPP_AVAILABILITY_SYNC stop_source {
public:
  _LIBCPP_HIDE_FROM_ABI stop_source() : __state_(new __stop_state()) { __state_->__increment_stop_source_counter(); }

  _LIBCPP_HIDE_FROM_ABI explicit stop_source(nostopstate_t) noexcept : __state_(nullptr) {}

  _LIBCPP_HIDE_FROM_ABI stop_source(const stop_source& __other) noexcept : __state_(__other.__state_) {
    if (__state_) {
      __state_->__increment_stop_source_counter();
    }
  }

  _LIBCPP_HIDE_FROM_ABI stop_source(stop_source&& __other) noexcept = default;

  _LIBCPP_HIDE_FROM_ABI stop_source& operator=(const stop_source& __other) noexcept {
    // increment `__other` first so that we don't hit 0 in case of self-assignment
    if (__other.__state_) {
      __other.__state_->__increment_stop_source_counter();
    }
    if (__state_) {
      __state_->__decrement_stop_source_counter();
    }
    __state_ = __other.__state_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI stop_source& operator=(stop_source&&) noexcept = default;

  _LIBCPP_HIDE_FROM_ABI ~stop_source() {
    if (__state_) {
      __state_->__decrement_stop_source_counter();
    }
  }

  _LIBCPP_HIDE_FROM_ABI void swap(stop_source& __other) noexcept { __state_.swap(__other.__state_); }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI stop_token get_token() const noexcept { return stop_token(__state_); }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI bool stop_possible() const noexcept { return __state_ != nullptr; }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI bool stop_requested() const noexcept {
    return __state_ != nullptr && __state_->__stop_requested();
  }

  _LIBCPP_HIDE_FROM_ABI bool request_stop() noexcept { return __state_ && __state_->__request_stop(); }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI friend bool operator==(const stop_source&, const stop_source&) noexcept = default;

  _LIBCPP_HIDE_FROM_ABI friend void swap(stop_source& __lhs, stop_source& __rhs) noexcept { __lhs.swap(__rhs); }

private:
  __intrusive_shared_ptr<__stop_state> __state_;
};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN) && !defined(_LIBCPP_HAS_NO_THREADS)
