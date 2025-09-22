// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_MONTHDAY_H
#define _LIBCPP___CHRONO_MONTHDAY_H

#include <__chrono/calendar.h>
#include <__chrono/day.h>
#include <__chrono/month.h>
#include <__config>
#include <compare>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

class month_day {
private:
  chrono::month __m_;
  chrono::day __d_;

public:
  month_day() = default;
  _LIBCPP_HIDE_FROM_ABI constexpr month_day(const chrono::month& __mval, const chrono::day& __dval) noexcept
      : __m_{__mval}, __d_{__dval} {}
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::month month() const noexcept { return __m_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::day day() const noexcept { return __d_; }
  _LIBCPP_HIDE_FROM_ABI constexpr bool ok() const noexcept;
};

_LIBCPP_HIDE_FROM_ABI inline constexpr bool month_day::ok() const noexcept {
  if (!__m_.ok())
    return false;
  const unsigned __dval = static_cast<unsigned>(__d_);
  if (__dval < 1 || __dval > 31)
    return false;
  if (__dval <= 29)
    return true;
  //  Now we've got either 30 or 31
  const unsigned __mval = static_cast<unsigned>(__m_);
  if (__mval == 2)
    return false;
  if (__mval == 4 || __mval == 6 || __mval == 9 || __mval == 11)
    return __dval == 30;
  return true;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator==(const month_day& __lhs, const month_day& __rhs) noexcept {
  return __lhs.month() == __rhs.month() && __lhs.day() == __rhs.day();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr strong_ordering
operator<=>(const month_day& __lhs, const month_day& __rhs) noexcept {
  if (auto __c = __lhs.month() <=> __rhs.month(); __c != 0)
    return __c;
  return __lhs.day() <=> __rhs.day();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month_day operator/(const month& __lhs, const day& __rhs) noexcept {
  return month_day{__lhs, __rhs};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month_day operator/(const day& __lhs, const month& __rhs) noexcept {
  return __rhs / __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month_day operator/(const month& __lhs, int __rhs) noexcept {
  return __lhs / day(__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month_day operator/(int __lhs, const day& __rhs) noexcept {
  return month(__lhs) / __rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month_day operator/(const day& __lhs, int __rhs) noexcept {
  return month(__rhs) / __lhs;
}

class month_day_last {
private:
  chrono::month __m_;

public:
  _LIBCPP_HIDE_FROM_ABI explicit constexpr month_day_last(const chrono::month& __val) noexcept : __m_{__val} {}
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::month month() const noexcept { return __m_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr bool ok() const noexcept { return __m_.ok(); }
};

_LIBCPP_HIDE_FROM_ABI inline constexpr bool
operator==(const month_day_last& __lhs, const month_day_last& __rhs) noexcept {
  return __lhs.month() == __rhs.month();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr strong_ordering
operator<=>(const month_day_last& __lhs, const month_day_last& __rhs) noexcept {
  return __lhs.month() <=> __rhs.month();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month_day_last operator/(const month& __lhs, last_spec) noexcept {
  return month_day_last{__lhs};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month_day_last operator/(last_spec, const month& __rhs) noexcept {
  return month_day_last{__rhs};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month_day_last operator/(int __lhs, last_spec) noexcept {
  return month_day_last{month(__lhs)};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month_day_last operator/(last_spec, int __rhs) noexcept {
  return month_day_last{month(__rhs)};
}

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___CHRONO_MONTHDAY_H
