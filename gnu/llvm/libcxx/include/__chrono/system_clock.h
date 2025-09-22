// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_SYSTEM_CLOCK_H
#define _LIBCPP___CHRONO_SYSTEM_CLOCK_H

#include <__chrono/duration.h>
#include <__chrono/time_point.h>
#include <__config>
#include <ctime>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

class _LIBCPP_EXPORTED_FROM_ABI system_clock {
public:
  typedef microseconds duration;
  typedef duration::rep rep;
  typedef duration::period period;
  typedef chrono::time_point<system_clock> time_point;
  static _LIBCPP_CONSTEXPR_SINCE_CXX14 const bool is_steady = false;

  static time_point now() _NOEXCEPT;
  static time_t to_time_t(const time_point& __t) _NOEXCEPT;
  static time_point from_time_t(time_t __t) _NOEXCEPT;
};

#if _LIBCPP_STD_VER >= 20

template <class _Duration>
using sys_time    = time_point<system_clock, _Duration>;
using sys_seconds = sys_time<seconds>;
using sys_days    = sys_time<days>;

#endif

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CHRONO_SYSTEM_CLOCK_H
