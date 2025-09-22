// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___THREAD_THIS_THREAD_H
#define _LIBCPP___THREAD_THIS_THREAD_H

#include <__chrono/steady_clock.h>
#include <__chrono/time_point.h>
#include <__condition_variable/condition_variable.h>
#include <__config>
#include <__mutex/mutex.h>
#include <__mutex/unique_lock.h>
#include <__thread/support.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

namespace this_thread {

_LIBCPP_EXPORTED_FROM_ABI void sleep_for(const chrono::nanoseconds& __ns);

template <class _Rep, class _Period>
_LIBCPP_HIDE_FROM_ABI void sleep_for(const chrono::duration<_Rep, _Period>& __d) {
  if (__d > chrono::duration<_Rep, _Period>::zero()) {
    // The standard guarantees a 64bit signed integer resolution for nanoseconds,
    // so use INT64_MAX / 1e9 as cut-off point. Use a constant to avoid <climits>
    // and issues with long double folding on PowerPC with GCC.
    _LIBCPP_CONSTEXPR chrono::duration<long double> __max = chrono::duration<long double>(9223372036.0L);
    chrono::nanoseconds __ns;
    if (__d < __max) {
      __ns = chrono::duration_cast<chrono::nanoseconds>(__d);
      if (__ns < __d)
        ++__ns;
    } else
      __ns = chrono::nanoseconds::max();
    this_thread::sleep_for(__ns);
  }
}

template <class _Clock, class _Duration>
_LIBCPP_HIDE_FROM_ABI void sleep_until(const chrono::time_point<_Clock, _Duration>& __t) {
  mutex __mut;
  condition_variable __cv;
  unique_lock<mutex> __lk(__mut);
  while (_Clock::now() < __t)
    __cv.wait_until(__lk, __t);
}

template <class _Duration>
inline _LIBCPP_HIDE_FROM_ABI void sleep_until(const chrono::time_point<chrono::steady_clock, _Duration>& __t) {
  this_thread::sleep_for(__t - chrono::steady_clock::now());
}

inline _LIBCPP_HIDE_FROM_ABI void yield() _NOEXCEPT { __libcpp_thread_yield(); }

} // namespace this_thread

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___THREAD_THIS_THREAD_H
