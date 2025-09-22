// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_WEEKDAY_H
#define _LIBCPP___CHRONO_WEEKDAY_H

#include <__chrono/calendar.h>
#include <__chrono/duration.h>
#include <__chrono/system_clock.h>
#include <__chrono/time_point.h>
#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

class weekday_indexed;
class weekday_last;

class weekday {
private:
  unsigned char __wd_;
  _LIBCPP_HIDE_FROM_ABI static constexpr unsigned char __weekday_from_days(int __days) noexcept;

public:
  weekday() = default;
  _LIBCPP_HIDE_FROM_ABI inline explicit constexpr weekday(unsigned __val) noexcept
      : __wd_(static_cast<unsigned char>(__val == 7 ? 0 : __val)) {}
  _LIBCPP_HIDE_FROM_ABI inline constexpr weekday(const sys_days& __sysd) noexcept
      : __wd_(__weekday_from_days(__sysd.time_since_epoch().count())) {}
  _LIBCPP_HIDE_FROM_ABI inline explicit constexpr weekday(const local_days& __locd) noexcept
      : __wd_(__weekday_from_days(__locd.time_since_epoch().count())) {}

  _LIBCPP_HIDE_FROM_ABI inline constexpr weekday& operator++() noexcept {
    __wd_ = (__wd_ == 6 ? 0 : __wd_ + 1);
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr weekday operator++(int) noexcept {
    weekday __tmp = *this;
    ++(*this);
    return __tmp;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr weekday& operator--() noexcept {
    __wd_ = (__wd_ == 0 ? 6 : __wd_ - 1);
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr weekday operator--(int) noexcept {
    weekday __tmp = *this;
    --(*this);
    return __tmp;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr weekday& operator+=(const days& __dd) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr weekday& operator-=(const days& __dd) noexcept;
  _LIBCPP_HIDE_FROM_ABI inline constexpr unsigned c_encoding() const noexcept { return __wd_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr unsigned iso_encoding() const noexcept { return __wd_ == 0u ? 7 : __wd_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr bool ok() const noexcept { return __wd_ <= 6; }
  _LIBCPP_HIDE_FROM_ABI constexpr weekday_indexed operator[](unsigned __index) const noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr weekday_last operator[](last_spec) const noexcept;
};

// https://howardhinnant.github.io/date_algorithms.html#weekday_from_days
_LIBCPP_HIDE_FROM_ABI inline constexpr unsigned char weekday::__weekday_from_days(int __days) noexcept {
  return static_cast<unsigned char>(static_cast<unsigned>(__days >= -4 ? (__days + 4) % 7 : (__days + 5) % 7 + 6));
}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator==(const weekday& __lhs, const weekday& __rhs) noexcept {
  return __lhs.c_encoding() == __rhs.c_encoding();
}

// TODO(LLVM 20): Remove the escape hatch
#  ifdef _LIBCPP_ENABLE_REMOVED_WEEKDAY_RELATIONAL_OPERATORS
_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator<(const weekday& __lhs, const weekday& __rhs) noexcept {
  return __lhs.c_encoding() < __rhs.c_encoding();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator>(const weekday& __lhs, const weekday& __rhs) noexcept {
  return __rhs < __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator<=(const weekday& __lhs, const weekday& __rhs) noexcept {
  return !(__rhs < __lhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator>=(const weekday& __lhs, const weekday& __rhs) noexcept {
  return !(__lhs < __rhs);
}
#  endif // _LIBCPP_ENABLE_REMOVED_WEEKDAY_RELATIONAL_OPERATORS

_LIBCPP_HIDE_FROM_ABI inline constexpr weekday operator+(const weekday& __lhs, const days& __rhs) noexcept {
  auto const __mu = static_cast<long long>(__lhs.c_encoding()) + __rhs.count();
  auto const __yr = (__mu >= 0 ? __mu : __mu - 6) / 7;
  return weekday{static_cast<unsigned>(__mu - __yr * 7)};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr weekday operator+(const days& __lhs, const weekday& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr weekday operator-(const weekday& __lhs, const days& __rhs) noexcept {
  return __lhs + -__rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr days operator-(const weekday& __lhs, const weekday& __rhs) noexcept {
  const int __wdu = __lhs.c_encoding() - __rhs.c_encoding();
  const int __wk  = (__wdu >= 0 ? __wdu : __wdu - 6) / 7;
  return days{__wdu - __wk * 7};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr weekday& weekday::operator+=(const days& __dd) noexcept {
  *this = *this + __dd;
  return *this;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr weekday& weekday::operator-=(const days& __dd) noexcept {
  *this = *this - __dd;
  return *this;
}

class weekday_indexed {
private:
  chrono::weekday __wd_;
  unsigned char __idx_;

public:
  weekday_indexed() = default;
  _LIBCPP_HIDE_FROM_ABI inline constexpr weekday_indexed(const chrono::weekday& __wdval, unsigned __idxval) noexcept
      : __wd_{__wdval}, __idx_(__idxval) {}
  _LIBCPP_HIDE_FROM_ABI inline constexpr chrono::weekday weekday() const noexcept { return __wd_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr unsigned index() const noexcept { return __idx_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr bool ok() const noexcept { return __wd_.ok() && __idx_ >= 1 && __idx_ <= 5; }
};

_LIBCPP_HIDE_FROM_ABI inline constexpr bool
operator==(const weekday_indexed& __lhs, const weekday_indexed& __rhs) noexcept {
  return __lhs.weekday() == __rhs.weekday() && __lhs.index() == __rhs.index();
}

class weekday_last {
private:
  chrono::weekday __wd_;

public:
  _LIBCPP_HIDE_FROM_ABI explicit constexpr weekday_last(const chrono::weekday& __val) noexcept : __wd_{__val} {}
  _LIBCPP_HIDE_FROM_ABI constexpr chrono::weekday weekday() const noexcept { return __wd_; }
  _LIBCPP_HIDE_FROM_ABI constexpr bool ok() const noexcept { return __wd_.ok(); }
};

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator==(const weekday_last& __lhs, const weekday_last& __rhs) noexcept {
  return __lhs.weekday() == __rhs.weekday();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr weekday_indexed weekday::operator[](unsigned __index) const noexcept {
  return weekday_indexed{*this, __index};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr weekday_last weekday::operator[](last_spec) const noexcept {
  return weekday_last{*this};
}

inline constexpr weekday Sunday{0};
inline constexpr weekday Monday{1};
inline constexpr weekday Tuesday{2};
inline constexpr weekday Wednesday{3};
inline constexpr weekday Thursday{4};
inline constexpr weekday Friday{5};
inline constexpr weekday Saturday{6};

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___CHRONO_WEEKDAY_H
