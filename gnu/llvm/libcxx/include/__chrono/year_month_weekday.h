// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_YEAR_MONTH_WEEKDAY_H
#define _LIBCPP___CHRONO_YEAR_MONTH_WEEKDAY_H

#include <__chrono/calendar.h>
#include <__chrono/day.h>
#include <__chrono/duration.h>
#include <__chrono/month.h>
#include <__chrono/month_weekday.h>
#include <__chrono/system_clock.h>
#include <__chrono/time_point.h>
#include <__chrono/weekday.h>
#include <__chrono/year.h>
#include <__chrono/year_month.h>
#include <__chrono/year_month_day.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

class year_month_weekday {
  chrono::year __y_;
  chrono::month __m_;
  chrono::weekday_indexed __wdi_;

public:
  year_month_weekday() = default;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday(
      const chrono::year& __yval, const chrono::month& __mval, const chrono::weekday_indexed& __wdival) noexcept
      : __y_{__yval}, __m_{__mval}, __wdi_{__wdival} {}
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday(const sys_days& __sysd) noexcept
      : year_month_weekday(__from_days(__sysd.time_since_epoch())) {}
  _LIBCPP_HIDE_FROM_ABI inline explicit constexpr year_month_weekday(const local_days& __locd) noexcept
      : year_month_weekday(__from_days(__locd.time_since_epoch())) {}
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday& operator+=(const months&) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday& operator-=(const months&) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday& operator+=(const years&) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday& operator-=(const years&) noexcept;

  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::year year() const noexcept { return __y_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::month month() const noexcept { return __m_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::weekday weekday() const noexcept { return __wdi_.weekday(); }
  _LIBCPP_HIDE_FROM_ABI inline constexpr unsigned index() const noexcept { return __wdi_.index(); }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::weekday_indexed weekday_indexed() const noexcept { return __wdi_; }

  _LIBCPP_HIDE_FROM_ABI inline constexpr operator sys_days() const noexcept { return sys_days{__to_days()}; }
  _LIBCPP_HIDE_FROM_ABI inline explicit constexpr operator local_days() const noexcept {
    return local_days{__to_days()};
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr bool ok() const noexcept {
    if (!__y_.ok() || !__m_.ok() || !__wdi_.ok())
      return false;
    if (__wdi_.index() <= 4)
      return true;
    auto __nth_weekday_day =
        __wdi_.weekday() - chrono::weekday{static_cast<sys_days>(__y_ / __m_ / 1)} + days{(__wdi_.index() - 1) * 7 + 1};
    return static_cast<unsigned>(__nth_weekday_day.count()) <= static_cast<unsigned>((__y_ / __m_ / last).day());
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr year_month_weekday __from_days(days __d) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr days __to_days() const noexcept;
};

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday year_month_weekday::__from_days(days __d) noexcept {
  const sys_days __sysd{__d};
  const chrono::weekday __wd = chrono::weekday(__sysd);
  const year_month_day __ymd = year_month_day(__sysd);
  return year_month_weekday{__ymd.year(), __ymd.month(), __wd[(static_cast<unsigned>(__ymd.day()) - 1) / 7 + 1]};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr days year_month_weekday::__to_days() const noexcept {
  const sys_days __sysd = sys_days(__y_ / __m_ / 1);
  return (__sysd + (__wdi_.weekday() - chrono::weekday(__sysd) + days{(__wdi_.index() - 1) * 7})).time_since_epoch();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool
operator==(const year_month_weekday& __lhs, const year_month_weekday& __rhs) noexcept {
  return __lhs.year() == __rhs.year() && __lhs.month() == __rhs.month() &&
         __lhs.weekday_indexed() == __rhs.weekday_indexed();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday
operator/(const year_month& __lhs, const weekday_indexed& __rhs) noexcept {
  return year_month_weekday{__lhs.year(), __lhs.month(), __rhs};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday
operator/(const year& __lhs, const month_weekday& __rhs) noexcept {
  return year_month_weekday{__lhs, __rhs.month(), __rhs.weekday_indexed()};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday operator/(int __lhs, const month_weekday& __rhs) noexcept {
  return year(__lhs) / __rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday
operator/(const month_weekday& __lhs, const year& __rhs) noexcept {
  return __rhs / __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday operator/(const month_weekday& __lhs, int __rhs) noexcept {
  return year(__rhs) / __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday
operator+(const year_month_weekday& __lhs, const months& __rhs) noexcept {
  return (__lhs.year() / __lhs.month() + __rhs) / __lhs.weekday_indexed();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday
operator+(const months& __lhs, const year_month_weekday& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday
operator-(const year_month_weekday& __lhs, const months& __rhs) noexcept {
  return __lhs + (-__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday
operator+(const year_month_weekday& __lhs, const years& __rhs) noexcept {
  return year_month_weekday{__lhs.year() + __rhs, __lhs.month(), __lhs.weekday_indexed()};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday
operator+(const years& __lhs, const year_month_weekday& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday
operator-(const year_month_weekday& __lhs, const years& __rhs) noexcept {
  return __lhs + (-__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday& year_month_weekday::operator+=(const months& __dm) noexcept {
  *this = *this + __dm;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday& year_month_weekday::operator-=(const months& __dm) noexcept {
  *this = *this - __dm;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday& year_month_weekday::operator+=(const years& __dy) noexcept {
  *this = *this + __dy;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday& year_month_weekday::operator-=(const years& __dy) noexcept {
  *this = *this - __dy;
  return *this;
}

class year_month_weekday_last {
private:
  chrono::year __y_;
  chrono::month __m_;
  chrono::weekday_last __wdl_;

public:
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday_last(
      const chrono::year& __yval, const chrono::month& __mval, const chrono::weekday_last& __wdlval) noexcept
      : __y_{__yval}, __m_{__mval}, __wdl_{__wdlval} {}
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday_last& operator+=(const months& __dm) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday_last& operator-=(const months& __dm) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday_last& operator+=(const years& __dy) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year_month_weekday_last& operator-=(const years& __dy) noexcept;

  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::year year() const noexcept { return __y_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::month month() const noexcept { return __m_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::weekday weekday() const noexcept { return __wdl_.weekday(); }
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::weekday_last weekday_last() const noexcept { return __wdl_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr operator sys_days() const noexcept { return sys_days{__to_days()}; }
  _LIBCPP_HIDE_FROM_ABI inline explicit constexpr operator local_days() const noexcept {
    return local_days{__to_days()};
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr bool ok() const noexcept { return __y_.ok() && __m_.ok() && __wdl_.ok(); }

  _LIBCPP_HIDE_FROM_ABI constexpr days __to_days() const noexcept;
};

_LIBCPP_HIDE_FROM_ABI inline constexpr days year_month_weekday_last::__to_days() const noexcept {
  const sys_days __last = sys_days{__y_ / __m_ / last};
  return (__last - (chrono::weekday{__last} - __wdl_.weekday())).time_since_epoch();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool
operator==(const year_month_weekday_last& __lhs, const year_month_weekday_last& __rhs) noexcept {
  return __lhs.year() == __rhs.year() && __lhs.month() == __rhs.month() && __lhs.weekday_last() == __rhs.weekday_last();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator/(const year_month& __lhs, const weekday_last& __rhs) noexcept {
  return year_month_weekday_last{__lhs.year(), __lhs.month(), __rhs};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator/(const year& __lhs, const month_weekday_last& __rhs) noexcept {
  return year_month_weekday_last{__lhs, __rhs.month(), __rhs.weekday_last()};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator/(int __lhs, const month_weekday_last& __rhs) noexcept {
  return year(__lhs) / __rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator/(const month_weekday_last& __lhs, const year& __rhs) noexcept {
  return __rhs / __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator/(const month_weekday_last& __lhs, int __rhs) noexcept {
  return year(__rhs) / __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator+(const year_month_weekday_last& __lhs, const months& __rhs) noexcept {
  return (__lhs.year() / __lhs.month() + __rhs) / __lhs.weekday_last();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator+(const months& __lhs, const year_month_weekday_last& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator-(const year_month_weekday_last& __lhs, const months& __rhs) noexcept {
  return __lhs + (-__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator+(const year_month_weekday_last& __lhs, const years& __rhs) noexcept {
  return year_month_weekday_last{__lhs.year() + __rhs, __lhs.month(), __lhs.weekday_last()};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator+(const years& __lhs, const year_month_weekday_last& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last
operator-(const year_month_weekday_last& __lhs, const years& __rhs) noexcept {
  return __lhs + (-__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last&
year_month_weekday_last::operator+=(const months& __dm) noexcept {
  *this = *this + __dm;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last&
year_month_weekday_last::operator-=(const months& __dm) noexcept {
  *this = *this - __dm;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last&
year_month_weekday_last::operator+=(const years& __dy) noexcept {
  *this = *this + __dy;
  return *this;
}
_LIBCPP_HIDE_FROM_ABI inline constexpr year_month_weekday_last&
year_month_weekday_last::operator-=(const years& __dy) noexcept {
  *this = *this - __dy;
  return *this;
}

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___CHRONO_YEAR_MONTH_WEEKDAY_H
