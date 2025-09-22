// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_LITERALS_H
#define _LIBCPP___CHRONO_LITERALS_H

#include <__chrono/day.h>
#include <__chrono/year.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

inline namespace literals {
inline namespace chrono_literals {
_LIBCPP_HIDE_FROM_ABI constexpr chrono::day operator""d(unsigned long long __d) noexcept {
  return chrono::day(static_cast<unsigned>(__d));
}

_LIBCPP_HIDE_FROM_ABI constexpr chrono::year operator""y(unsigned long long __y) noexcept {
  return chrono::year(static_cast<int>(__y));
}
} // namespace chrono_literals
} // namespace literals

namespace chrono { // hoist the literals into namespace std::chrono
using namespace literals::chrono_literals;
} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___CHRONO_LITERALS_H
