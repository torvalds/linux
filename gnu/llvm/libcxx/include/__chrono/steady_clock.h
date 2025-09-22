// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_STEADY_CLOCK_H
#define _LIBCPP___CHRONO_STEADY_CLOCK_H

#include <__chrono/duration.h>
#include <__chrono/time_point.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

#ifndef _LIBCPP_HAS_NO_MONOTONIC_CLOCK
class _LIBCPP_EXPORTED_FROM_ABI steady_clock {
public:
  typedef nanoseconds duration;
  typedef duration::rep rep;
  typedef duration::period period;
  typedef chrono::time_point<steady_clock, duration> time_point;
  static _LIBCPP_CONSTEXPR_SINCE_CXX14 const bool is_steady = true;

  static time_point now() _NOEXCEPT;
};
#endif

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CHRONO_STEADY_CLOCK_H
