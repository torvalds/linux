// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_YEAR_MONTH_DAY_H
#define _LIBCPP___CHRONO_YEAR_MONTH_DAY_H

#include <__chrono/calendar.h>
#include <__chrono/day.h>
#include <__chrono/duration.h>
#include <__chrono/month.h>
#include <__chrono/monthday.h>
#include <__chrono/system_clock.h>
#include <__chrono/time_point.h>
#include <__chrono/year.h>
#include <__chrono/year_month.h>
#include <__config>
#include <compare>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

class year_month_day_last;

class year_month_day {
private:
  chrono::year __y_;
  chrono::month __m_;
  chrono::day __d_;

public:
  year_month_day() = default;
  _LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day(
      const chrono::year& __yval, const chrono::month& __mval, const chrono::day& __dval) noexcept
      : __y_{__yval}, __m_{__mval}, __d_{__dval} {}
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_day(const year_month_day_last& __ymdl) noexcept;
  _LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day(const sys_days& __sysd) noexcept
      : year_month_day(__from_days(__sysd.time_since_epoch())) {}
  _LIBCPP_HIDE_FROM_ABI inline explicit constexpr year_month_day(const local_days& __locd) noexcept
      : year_month_day(__from_days(__locd.time_since_epoch())) {}

  _LIBCPP_HIDE_FROM_ABI constexpr year_month_day& operator+=(const months& __dm) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_day& operator-=(const months& __dm) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_day& operator+=(const years& __dy) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_day& operator-=(const years& __dy) noexcept;

  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::year year() const noexcept { return __y_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::month month() const noexcept { return __m_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::day day() const noexcept { return __d_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr operator sys_days() const noexcept { return sys_days{__to_days()}; }
  _LIBCPP_HIDE_FROM_ABI inline explicit constexpr operator local_days() const noexcept {
    return local_days{__to_days()};
  }

  _LIBCPP_HIDE_FROM_ABI constexpr bool ok() const noexcept;

  _LIBCPP_HIDE_FROM_ABI static constexpr year_month_day __from_days(days __d) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr days __to_days() const noexcept;
};

// https://howardhinnant.github.io/date_algorithms.html#civil_from_days
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day year_month_day::__from_days(days __d) noexcept {
  static_assert(numeric_limits<unsigned>::digits >= 18, "");
  static_assert(numeric_limits<int>::digits >= 20, "");
  const int __z        = __d.count() + 719468;
  const int __era      = (__z >= 0 ? __z : __z - 146096) / 146097;
  const unsigned __doe = static_cast<unsigned>(__z - __era * 146097);                   // [0, 146096]
  const unsigned __yoe = (__doe - __doe / 1460 + __doe / 36524 - __doe / 146096) / 365; // [0, 399]
  const int __yr       = static_cast<int>(__yoe) + __era * 400;
  const unsigned __doy = __doe - (365 * __yoe + __yoe / 4 - __yoe / 100); // [0, 365]
  const unsigned __mp  = (5 * __doy + 2) / 153;                           // [0, 11]
  const unsigned __dy  = __doy - (153 * __mp + 2) / 5 + 1;                // [1, 31]
  const unsigned __mth = __mp + (__mp < 10 ? 3 : -9);                     // [1, 12]
  return year_month_day{chrono::year{__yr + (__mth <= 2)}, chrono::month{__mth}, chrono::day{__dy}};
}

// https://howardhinnant.github.io/date_algorithms.html#days_from_civil
_LIBCPP_HIDE_FROM_ABI inline constexpr days year_month_day::__to_days() const noexcept {
  static_assert(numeric_limits<unsigned>::digits >= 18, "");
  static_assert(numeric_limits<int>::digits >= 20, "");

  const int __yr       = static_cast<int>(__y_) - (__m_ <= February);
  const unsigned __mth = static_cast<unsigned>(__m_);
  const unsigned __dy  = static_cast<unsigned>(__d_);

  const int __era      = (__yr >= 0 ? __yr : __yr - 399) / 400;
  const unsigned __yoe = static_cast<unsigned>(__yr - __era * 400);                 // [0, 399]
  const unsigned __doy = (153 * (__mth + (__mth > 2 ? -3 : 9)) + 2) / 5 + __dy - 1; // [0, 365]
  const unsigned __doe = __yoe * 365 + __yoe / 4 - __yoe / 100 + __doy;             // [0, 146096]
  return days{__era * 146097 + static_cast<int>(__doe) - 719468};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool
operator==(const year_month_day& __lhs, const year_month_day& __rhs) noexcept {
  return __lhs.year() == __rhs.year() && __lhs.month() == __rhs.month() && __lhs.day() == __rhs.day();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr strong_ordering
operator<=>(const year_month_day& __lhs, const year_month_day& __rhs) noexcept {
  if (auto __c = __lhs.year() <=> __rhs.year(); __c != 0)
    return __c;
  if (auto __c = __lhs.month() <=> __rhs.month(); __c != 0)
    return __c;
  return __lhs.day() <=> __rhs.day();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day operator/(const year_month& __lhs, const day& __rhs) noexcept {
  return year_month_day{__lhs.year(), __lhs.month(), __rhs};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day operator/(const year_month& __lhs, int __rhs) noexcept {
  return __lhs / day(__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day operator/(const year& __lhs, const month_day& __rhs) noexcept {
  return __lhs / __rhs.month() / __rhs.day();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day operator/(int __lhs, const month_day& __rhs) noexcept {
  return year(__lhs) / __rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day operator/(const month_day& __lhs, const year& __rhs) noexcept {
  return __rhs / __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day operator/(const month_day& __lhs, int __rhs) noexcept {
  return year(__rhs) / __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day
operator+(const year_month_day& __lhs, const months& __rhs) noexcept {
  return (__lhs.year() / __lhs.month() + __rhs) / __lhs.day();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day
operator+(const months& __lhs, const year_month_day& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day
operator-(const year_month_day& __lhs, const months& __rhs) noexcept {
  return __lhs + -__rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day
operator+(const year_month_day& __lhs, const years& __rhs) noexcept {
  return (__lhs.year() + __rhs) / __lhs.month() / __lhs.day();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day
operator+(const years& __lhs, const year_month_day& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day
operator-(const year_month_day& __lhs, const years& __rhs) noexcept {
  return __lhs + -__rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day& year_month_day::operator+=(const months& __dm) noexcept {
  *this = *this + __dm;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day& year_month_day::operator-=(const months& __dm) noexcept {
  *this = *this - __dm;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day& year_month_day::operator+=(const years& __dy) noexcept {
  *this = *this + __dy;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day& year_month_day::operator-=(const years& __dy) noexcept {
  *this = *this - __dy;
  return *this;
}

class year_month_day_last {
private:
  chrono::year __y_;
  chrono::month_day_last __mdl_;

public:
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_day_last(const year& __yval, const month_day_last& __mdlval) noexcept
      : __y_{__yval}, __mdl_{__mdlval} {}

  _LIBCPP_HIDE_FROM_ABI constexpr year_month_day_last& operator+=(const months& __m) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_day_last& operator-=(const months& __m) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_day_last& operator+=(const years& __y) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_day_last& operator-=(const years& __y) noexcept;

  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::year year() const noexcept { return __y_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::month month() const noexcept { return __mdl_.month(); }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::month_day_last month_day_last() const noexcept { return __mdl_; }
  _LIBCPP_HIDE_FROM_ABI constexpr chrono::day day() const noexcept;
  _LIBCPP_HIDE_FROM_ABI inline constexpr operator sys_days() const noexcept {
    return sys_days{year() / month() / day()};
  }
  _LIBCPP_HIDE_FROM_ABI inline explicit constexpr operator local_days() const noexcept {
    return local_days{year() / month() / day()};
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr bool ok() const noexcept { return __y_.ok() && __mdl_.ok(); }
};

_LIBCPP_HIDE_FROM_ABI inline constexpr chrono::day year_month_day_last::day() const noexcept {
  constexpr chrono::day __d[] = {
      chrono::day(31),
      chrono::day(28),
      chrono::day(31),
      chrono::day(30),
      chrono::day(31),
      chrono::day(30),
      chrono::day(31),
      chrono::day(31),
      chrono::day(30),
      chrono::day(31),
      chrono::day(30),
      chrono::day(31)};
  return (month() != February || !__y_.is_leap()) && month().ok()
           ? __d[static_cast<unsigned>(month()) - 1]
           : chrono::day{29};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool
operator==(const year_month_day_last& __lhs, const year_month_day_last& __rhs) noexcept {
  return __lhs.year() == __rhs.year() && __lhs.month_day_last() == __rhs.month_day_last();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr strong_ordering
operator<=>(const year_month_day_last& __lhs, const year_month_day_last& __rhs) noexcept {
  if (auto __c = __lhs.year() <=> __rhs.year(); __c != 0)
    return __c;
  return __lhs.month_day_last() <=> __rhs.month_day_last();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last operator/(const year_month& __lhs, last_spec) noexcept {
  return year_month_day_last{__lhs.year(), month_day_last{__lhs.month()}};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last
operator/(const year& __lhs, const month_day_last& __rhs) noexcept {
  return year_month_day_last{__lhs, __rhs};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last operator/(int __lhs, const month_day_last& __rhs) noexcept {
  return year_month_day_last{year{__lhs}, __rhs};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last
operator/(const month_day_last& __lhs, const year& __rhs) noexcept {
  return __rhs / __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last operator/(const month_day_last& __lhs, int __rhs) noexcept {
  return year{__rhs} / __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last
operator+(const year_month_day_last& __lhs, const months& __rhs) noexcept {
  return (__lhs.year() / __lhs.month() + __rhs) / last;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last
operator+(const months& __lhs, const year_month_day_last& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last
operator-(const year_month_day_last& __lhs, const months& __rhs) noexcept {
  return __lhs + (-__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last
operator+(const year_month_day_last& __lhs, const years& __rhs) noexcept {
  return year_month_day_last{__lhs.year() + __rhs, __lhs.month_day_last()};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last
operator+(const years& __lhs, const year_month_day_last& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last
operator-(const year_month_day_last& __lhs, const years& __rhs) noexcept {
  return __lhs + (-__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last&
year_month_day_last::operator+=(const months& __dm) noexcept {
  *this = *this + __dm;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last&
year_month_day_last::operator-=(const months& __dm) noexcept {
  *this = *this - __dm;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last&
year_month_day_last::operator+=(const years& __dy) noexcept {
  *this = *this + __dy;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day_last&
year_month_day_last::operator-=(const years& __dy) noexcept {
  *this = *this - __dy;
  return *this;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_day::year_month_day(const year_month_day_last& __ymdl) noexcept
    : __y_{__ymdl.year()}, __m_{__ymdl.month()}, __d_{__ymdl.day()} {}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool year_month_day::ok() const noexcept {
  if (!__y_.ok() || !__m_.ok())
    return false;
  return chrono::day{1} <= __d_ && __d_ <= (__y_ / __m_ / last).day();
}

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___CHRONO_YEAR_MONTH_DAY_H
