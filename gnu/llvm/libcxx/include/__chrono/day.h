// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_DAY_H
#define _LIBCPP___CHRONO_DAY_H

#include <__chrono/duration.h>
#include <__config>
#include <compare>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

class day {
private:
  unsigned char __d_;

public:
  day() = default;
  _LIBCPP_HIDE_FROM_ABI explicit inline constexpr day(unsigned __val) noexcept
      : __d_(static_cast<unsigned char>(__val)) {}
  _LIBCPP_HIDE_FROM_ABI inline constexpr day& operator++() noexcept {
    ++__d_;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr day operator++(int) noexcept {
    day __tmp = *this;
    ++(*this);
    return __tmp;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr day& operator--() noexcept {
    --__d_;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr day operator--(int) noexcept {
    day __tmp = *this;
    --(*this);
    return __tmp;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr day& operator+=(const days& __dd) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr day& operator-=(const days& __dd) noexcept;
  _LIBCPP_HIDE_FROM_ABI explicit inline constexpr operator unsigned() const noexcept { return __d_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr bool ok() const noexcept { return __d_ >= 1 && __d_ <= 31; }
};

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator==(const day& __lhs, const day& __rhs) noexcept {
  return static_cast<unsigned>(__lhs) == static_cast<unsigned>(__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr strong_ordering operator<=>(const day& __lhs, const day& __rhs) noexcept {
  return static_cast<unsigned>(__lhs) <=> static_cast<unsigned>(__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr day operator+(const day& __lhs, const days& __rhs) noexcept {
  return day(static_cast<unsigned>(__lhs) + __rhs.count());
}

_LIBCPP_HIDE_FROM_ABI inline constexpr day operator+(const days& __lhs, const day& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr day operator-(const day& __lhs, const days& __rhs) noexcept {
  return __lhs + -__rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr days operator-(const day& __lhs, const day& __rhs) noexcept {
  return days(static_cast<int>(static_cast<unsigned>(__lhs)) - static_cast<int>(static_cast<unsigned>(__rhs)));
}

_LIBCPP_HIDE_FROM_ABI inline constexpr day& day::operator+=(const days& __dd) noexcept {
  *this = *this + __dd;
  return *this;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr day& day::operator-=(const days& __dd) noexcept {
  *this = *this - __dd;
  return *this;
}

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___CHRONO_DAY_H
