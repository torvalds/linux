// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STOP_TOKEN_STOP_CALLBACK_H
#define _LIBCPP___STOP_TOKEN_STOP_CALLBACK_H

#include <__concepts/constructible.h>
#include <__concepts/destructible.h>
#include <__concepts/invocable.h>
#include <__config>
#include <__stop_token/intrusive_shared_ptr.h>
#include <__stop_token/stop_state.h>
#include <__stop_token/stop_token.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <__utility/private_constructor_tag.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN) && !defined(_LIBCPP_HAS_NO_THREADS)

template <class _Callback>
class _LIBCPP_AVAILABILITY_SYNC stop_callback : private __stop_callback_base {
  static_assert(invocable<_Callback>,
                "Mandates: stop_callback is instantiated with an argument for the template parameter Callback that "
                "satisfies invocable.");
  static_assert(destructible<_Callback>,
                "Mandates: stop_callback is instantiated with an argument for the template parameter Callback that "
                "satisfies destructible.");

public:
  using callback_type = _Callback;

  template <class _Cb>
    requires constructible_from<_Callback, _Cb>
  _LIBCPP_HIDE_FROM_ABI explicit stop_callback(const stop_token& __st,
                                               _Cb&& __cb) noexcept(is_nothrow_constructible_v<_Callback, _Cb>)
      : stop_callback(__private_constructor_tag{}, __st.__state_, std::forward<_Cb>(__cb)) {}

  template <class _Cb>
    requires constructible_from<_Callback, _Cb>
  _LIBCPP_HIDE_FROM_ABI explicit stop_callback(stop_token&& __st,
                                               _Cb&& __cb) noexcept(is_nothrow_constructible_v<_Callback, _Cb>)
      : stop_callback(__private_constructor_tag{}, std::move(__st.__state_), std::forward<_Cb>(__cb)) {}

  _LIBCPP_HIDE_FROM_ABI ~stop_callback() {
    if (__state_) {
      __state_->__remove_callback(this);
    }
  }

  stop_callback(const stop_callback&)            = delete;
  stop_callback(stop_callback&&)                 = delete;
  stop_callback& operator=(const stop_callback&) = delete;
  stop_callback& operator=(stop_callback&&)      = delete;

private:
  _LIBCPP_NO_UNIQUE_ADDRESS _Callback __callback_;
  __intrusive_shared_ptr<__stop_state> __state_;

  friend __stop_callback_base;

  template <class _StatePtr, class _Cb>
  _LIBCPP_HIDE_FROM_ABI explicit stop_callback(__private_constructor_tag, _StatePtr&& __state, _Cb&& __cb) noexcept(
      is_nothrow_constructible_v<_Callback, _Cb>)
      : __stop_callback_base([](__stop_callback_base* __cb_base) noexcept {
          // stop callback is supposed to only be called once
          std::forward<_Callback>(static_cast<stop_callback*>(__cb_base)->__callback_)();
        }),
        __callback_(std::forward<_Cb>(__cb)),
        __state_() {
    if (__state && __state->__add_callback(this)) {
      // st.stop_requested() was false and this is successfully added to the linked list
      __state_ = std::forward<_StatePtr>(__state);
    }
  }
};

template <class _Callback>
_LIBCPP_AVAILABILITY_SYNC stop_callback(stop_token, _Callback) -> stop_callback<_Callback>;

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN) && !defined(_LIBCPP_HAS_NO_THREADS)
