// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STOP_TOKEN_STOP_TOKEN_H
#define _LIBCPP___STOP_TOKEN_STOP_TOKEN_H

#include <__config>
#include <__stop_token/intrusive_shared_ptr.h>
#include <__stop_token/stop_state.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN) && !defined(_LIBCPP_HAS_NO_THREADS)

class _LIBCPP_AVAILABILITY_SYNC stop_token {
public:
  _LIBCPP_HIDE_FROM_ABI stop_token() noexcept = default;

  _LIBCPP_HIDE_FROM_ABI stop_token(const stop_token&) noexcept            = default;
  _LIBCPP_HIDE_FROM_ABI stop_token(stop_token&&) noexcept                 = default;
  _LIBCPP_HIDE_FROM_ABI stop_token& operator=(const stop_token&) noexcept = default;
  _LIBCPP_HIDE_FROM_ABI stop_token& operator=(stop_token&&) noexcept      = default;
  _LIBCPP_HIDE_FROM_ABI ~stop_token()                                     = default;

  _LIBCPP_HIDE_FROM_ABI void swap(stop_token& __other) noexcept { __state_.swap(__other.__state_); }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI bool stop_requested() const noexcept {
    return __state_ != nullptr && __state_->__stop_requested();
  }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI bool stop_possible() const noexcept {
    return __state_ != nullptr && __state_->__stop_possible_for_stop_token();
  }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI friend bool operator==(const stop_token&, const stop_token&) noexcept = default;

  _LIBCPP_HIDE_FROM_ABI friend void swap(stop_token& __lhs, stop_token& __rhs) noexcept { __lhs.swap(__rhs); }

private:
  __intrusive_shared_ptr<__stop_state> __state_;

  friend class stop_source;
  template <class _Tp>
  friend class stop_callback;

  _LIBCPP_HIDE_FROM_ABI explicit stop_token(const __intrusive_shared_ptr<__stop_state>& __state) : __state_(__state) {}
};

#endif // _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN) && !defined(_LIBCPP_HAS_NO_THREADS)

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___STOP_TOKEN_STOP_TOKEN_H
