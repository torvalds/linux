// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_HIGH_RESOLUTION_CLOCK_H
#define _LIBCPP___CHRONO_HIGH_RESOLUTION_CLOCK_H

#include <__chrono/steady_clock.h>
#include <__chrono/system_clock.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

#ifndef _LIBCPP_HAS_NO_MONOTONIC_CLOCK
typedef steady_clock high_resolution_clock;
#else
typedef system_clock high_resolution_clock;
#endif

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CHRONO_HIGH_RESOLUTION_CLOCK_H
