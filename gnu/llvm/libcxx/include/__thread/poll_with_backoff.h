// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___THREAD_POLL_WITH_BACKOFF_H
#define _LIBCPP___THREAD_POLL_WITH_BACKOFF_H

#include <__chrono/duration.h>
#include <__chrono/high_resolution_clock.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

static _LIBCPP_CONSTEXPR const int __libcpp_polling_count = 64;

// Polls a thread for a condition given by a predicate, and backs off based on a backoff policy
// before polling again.
//
// - __poll is the "test function" that should return true if polling succeeded, and false if it failed.
//
// - __backoff is the "backoff policy", which is called with the duration since we started polling. It should
//   return false in order to resume polling, and true if polling should stop entirely for some reason.
//   In general, backoff policies sleep for some time before returning control to the polling loop.
//
// - __max_elapsed is the maximum duration to try polling for. If the maximum duration is exceeded,
//   the polling loop will return false to report a timeout.
template <class _Poll, class _Backoff>
_LIBCPP_AVAILABILITY_SYNC _LIBCPP_HIDE_FROM_ABI bool __libcpp_thread_poll_with_backoff(
    _Poll&& __poll, _Backoff&& __backoff, chrono::nanoseconds __max_elapsed = chrono::nanoseconds::zero()) {
  auto const __start = chrono::high_resolution_clock::now();
  for (int __count = 0;;) {
    if (__poll())
      return true; // __poll completion means success
    if (__count < __libcpp_polling_count) {
      __count += 1;
      continue;
    }
    chrono::nanoseconds const __elapsed = chrono::high_resolution_clock::now() - __start;
    if (__max_elapsed != chrono::nanoseconds::zero() && __max_elapsed < __elapsed)
      return false; // timeout failure
    if (__backoff(__elapsed))
      return false; // __backoff completion means failure
  }
}

// A trivial backoff policy that always immediately returns the control to
// the polling loop.
//
// This is not very well-behaved since it will cause the polling loop to spin,
// so this should most likely only be used on single-threaded systems where there
// are no other threads to compete with.
struct __spinning_backoff_policy {
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR bool operator()(chrono::nanoseconds const&) const { return false; }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___THREAD_POLL_WITH_BACKOFF_H
