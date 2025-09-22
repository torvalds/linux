// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_YEAR_H
#define _LIBCPP___CHRONO_YEAR_H

#include <__chrono/duration.h>
#include <__config>
#include <compare>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

class year {
private:
  short __y_;

public:
  year() = default;
  _LIBCPP_HIDE_FROM_ABI explicit inline constexpr year(int __val) noexcept : __y_(static_cast<short>(__val)) {}

  _LIBCPP_HIDE_FROM_ABI inline constexpr year& operator++() noexcept {
    ++__y_;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr year operator++(int) noexcept {
    year __tmp = *this;
    ++(*this);
    return __tmp;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr year& operator--() noexcept {
    --__y_;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr year operator--(int) noexcept {
    year __tmp = *this;
    --(*this);
    return __tmp;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr year& operator+=(const years& __dy) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr year& operator-=(const years& __dy) noexcept;
  _LIBCPP_HIDE_FROM_ABI inline constexpr year operator+() const noexcept { return *this; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr year operator-() const noexcept { return year{-__y_}; }

  _LIBCPP_HIDE_FROM_ABI inline constexpr bool is_leap() const noexcept {
    return __y_ % 4 == 0 && (__y_ % 100 != 0 || __y_ % 400 == 0);
  }
  _LIBCPP_HIDE_FROM_ABI explicit inline constexpr operator int() const noexcept { return __y_; }
  _LIBCPP_HIDE_FROM_ABI constexpr bool ok() const noexcept;
  _LIBCPP_HIDE_FROM_ABI static inline constexpr year min() noexcept { return year{-32767}; }
  _LIBCPP_HIDE_FROM_ABI static inline constexpr year max() noexcept { return year{32767}; }
};

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator==(const year& __lhs, const year& __rhs) noexcept {
  return static_cast<int>(__lhs) == static_cast<int>(__rhs);
}

_LIBCPP_HIDE_FROM_ABI constexpr strong_ordering operator<=>(const year& __lhs, const year& __rhs) noexcept {
  return static_cast<int>(__lhs) <=> static_cast<int>(__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year operator+(const year& __lhs, const years& __rhs) noexcept {
  return year(static_cast<int>(__lhs) + __rhs.count());
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year operator+(const years& __lhs, const year& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year operator-(const year& __lhs, const years& __rhs) noexcept {
  return __lhs + -__rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr years operator-(const year& __lhs, const year& __rhs) noexcept {
  return years{static_cast<int>(__lhs) - static_cast<int>(__rhs)};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year& year::operator+=(const years& __dy) noexcept {
  *this = *this + __dy;
  return *this;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr year& year::operator-=(const years& __dy) noexcept {
  *this = *this - __dy;
  return *this;
}

_LIBCPP_HIDE_FROM_ABI constexpr bool year::ok() const noexcept {
  static_assert(static_cast<int>(std::numeric_limits<decltype(__y_)>::max()) == static_cast<int>(max()));
  return static_cast<int>(min()) <= __y_;
}

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___CHRONO_YEAR_H
