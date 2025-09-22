// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___THREAD_TIMED_BACKOFF_POLICY_H
#define _LIBCPP___THREAD_TIMED_BACKOFF_POLICY_H

#include <__config>

#ifndef _LIBCPP_HAS_NO_THREADS

#  include <__chrono/duration.h>
#  include <__thread/support.h>

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

_LIBCPP_BEGIN_NAMESPACE_STD

struct __libcpp_timed_backoff_policy {
  _LIBCPP_HIDE_FROM_ABI bool operator()(chrono::nanoseconds __elapsed) const {
    if (__elapsed > chrono::milliseconds(128))
      __libcpp_thread_sleep_for(chrono::milliseconds(8));
    else if (__elapsed > chrono::microseconds(64))
      __libcpp_thread_sleep_for(__elapsed / 2);
    else if (__elapsed > chrono::microseconds(4))
      __libcpp_thread_yield();
    else {
    } // poll
    return false;
  }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_HAS_NO_THREADS

#endif // _LIBCPP___THREAD_TIMED_BACKOFF_POLICY_H
