// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_CALENDAR_H
#define _LIBCPP___CHRONO_CALENDAR_H

#include <__chrono/duration.h>
#include <__chrono/time_point.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

struct local_t {};
template <class _Duration>
using local_time    = time_point<local_t, _Duration>;
using local_seconds = local_time<seconds>;
using local_days    = local_time<days>;

struct last_spec {
  explicit last_spec() = default;
};
inline constexpr last_spec last{};

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___CHRONO_CALENDAR_H
