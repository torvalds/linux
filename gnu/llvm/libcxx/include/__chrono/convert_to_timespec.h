// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_CONVERT_TO_TIMESPEC_H
#define _LIBCPP___CHRONO_CONVERT_TO_TIMESPEC_H

#include <__chrono/duration.h>
#include <__config>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// Convert a nanoseconds duration to the given TimeSpec type, which must have
// the same properties as std::timespec.
template <class _TimeSpec>
_LIBCPP_HIDE_FROM_ABI inline _TimeSpec __convert_to_timespec(const chrono::nanoseconds& __ns) {
  using namespace chrono;
  seconds __s = duration_cast<seconds>(__ns);
  _TimeSpec __ts;
  typedef decltype(__ts.tv_sec) __ts_sec;
  const __ts_sec __ts_sec_max = numeric_limits<__ts_sec>::max();

  if (__s.count() < __ts_sec_max) {
    __ts.tv_sec  = static_cast<__ts_sec>(__s.count());
    __ts.tv_nsec = static_cast<decltype(__ts.tv_nsec)>((__ns - __s).count());
  } else {
    __ts.tv_sec  = __ts_sec_max;
    __ts.tv_nsec = 999999999; // (10^9 - 1)
  }

  return __ts;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___CHRONO_CONVERT_TO_TIMESPEC_H
