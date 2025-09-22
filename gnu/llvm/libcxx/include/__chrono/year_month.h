// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_YEAR_MONTH_H
#define _LIBCPP___CHRONO_YEAR_MONTH_H

#include <__chrono/duration.h>
#include <__chrono/month.h>
#include <__chrono/year.h>
#include <__config>
#include <compare>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

class year_month {
  chrono::year __y_;
  chrono::month __m_;

public:
  year_month() = default;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month(const chrono::year& __yval, const chrono::month& __mval) noexcept
      : __y_{__yval}, __m_{__mval} {}
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::year year() const noexcept { return __y_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::month month() const noexcept { return __m_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr year_month& operator+=(const months& __dm) noexcept;
  _LIBCPP_HIDE_FROM_ABI inline constexpr year_month& operator-=(const months& __dm) noexcept;
  _LIBCPP_HIDE_FROM_ABI inline constexpr year_month& operator+=(const years& __dy) noexcept;
  _LIBCPP_HIDE_FROM_ABI inline constexpr year_month& operator-=(const years& __dy) noexcept;
  _LIBCPP_HIDE_FROM_ABI inline constexpr bool ok() const noexcept { return __y_.ok() && __m_.ok(); }
};

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month operator/(const year& __y, const month& __m) noexcept {
  return year_month{__y, __m};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month operator/(const year& __y, int __m) noexcept {
  return year_month{__y, month(__m)};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator==(const year_month& __lhs, const year_month& __rhs) noexcept {
  return __lhs.year() == __rhs.year() && __lhs.month() == __rhs.month();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr strong_ordering
operator<=>(const year_month& __lhs, const year_month& __rhs) noexcept {
  if (auto __c = __lhs.year() <=> __rhs.year(); __c != 0)
    return __c;
  return __lhs.month() <=> __rhs.month();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month operator+(const year_month& __lhs, const months& __rhs) noexcept {
  int __dmi      = static_cast<int>(static_cast<unsigned>(__lhs.month())) - 1 + __rhs.count();
  const int __dy = (__dmi >= 0 ? __dmi : __dmi - 11) / 12;
  __dmi          = __dmi - __dy * 12 + 1;
  return (__lhs.year() + years(__dy)) / month(static_cast<unsigned>(__dmi));
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month operator+(const months& __lhs, const year_month& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month operator+(const year_month& __lhs, const years& __rhs) noexcept {
  return (__lhs.year() + __rhs) / __lhs.month();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month operator+(const years& __lhs, const year_month& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr months operator-(const year_month& __lhs, const year_month& __rhs) noexcept {
  return (__lhs.year() - __rhs.year()) +
         months(static_cast<unsigned>(__lhs.month()) - static_cast<unsigned>(__rhs.month()));
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month operator-(const year_month& __lhs, const months& __rhs) noexcept {
  return __lhs + -__rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month operator-(const year_month& __lhs, const years& __rhs) noexcept {
  return __lhs + -__rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month& year_month::operator+=(const months& __dm) noexcept {
  *this = *this + __dm;
  return *this;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month& year_month::operator-=(const months& __dm) noexcept {
  *this = *this - __dm;
  return *this;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month& year_month::operator+=(const years& __dy) noexcept {
  *this = *this + __dy;
  return *this;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month& year_month::operator-=(const years& __dy) noexcept {
  *this = *this - __dy;
  return *this;
}

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___CHRONO_YEAR_MONTH_H
