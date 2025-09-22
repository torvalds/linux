// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_MONTH_H
#define _LIBCPP___CHRONO_MONTH_H

#include <__chrono/duration.h>
#include <__config>
#include <compare>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

class month {
private:
  unsigned char __m_;

public:
  month() = default;
  _LIBCPP_HIDE_FROM_ABI explicit inline constexpr month(unsigned __val) noexcept
      : __m_(static_cast<unsigned char>(__val)) {}
  _LIBCPP_HIDE_FROM_ABI inline constexpr month& operator++() noexcept {
    *this += months{1};
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr month operator++(int) noexcept {
    month __tmp = *this;
    ++(*this);
    return __tmp;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr month& operator--() noexcept {
    *this -= months{1};
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI inline constexpr month operator--(int) noexcept {
    month __tmp = *this;
    --(*this);
    return __tmp;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr month& operator+=(const months& __m1) noexcept;
  _LIBCPP_HIDE_FROM_ABI constexpr month& operator-=(const months& __m1) noexcept;
  _LIBCPP_HIDE_FROM_ABI explicit inline constexpr operator unsigned() const noexcept { return __m_; }
  _LIBCPP_HIDE_FROM_ABI inline constexpr bool ok() const noexcept { return __m_ >= 1 && __m_ <= 12; }
};

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator==(const month& __lhs, const month& __rhs) noexcept {
  return static_cast<unsigned>(__lhs) == static_cast<unsigned>(__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr strong_ordering operator<=>(const month& __lhs, const month& __rhs) noexcept {
  return static_cast<unsigned>(__lhs) <=> static_cast<unsigned>(__rhs);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month operator+(const month& __lhs, const months& __rhs) noexcept {
  auto const __mu = static_cast<long long>(static_cast<unsigned>(__lhs)) + (__rhs.count() - 1);
  auto const __yr = (__mu >= 0 ? __mu : __mu - 11) / 12;
  return month{static_cast<unsigned>(__mu - __yr * 12 + 1)};
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month operator+(const months& __lhs, const month& __rhs) noexcept {
  return __rhs + __lhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month operator-(const month& __lhs, const months& __rhs) noexcept {
  return __lhs + -__rhs;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr months operator-(const month& __lhs, const month& __rhs) noexcept {
  auto const __dm = static_cast<unsigned>(__lhs) - static_cast<unsigned>(__rhs);
  return months(__dm <= 11 ? __dm : __dm + 12);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month& month::operator+=(const months& __dm) noexcept {
  *this = *this + __dm;
  return *this;
}

_LIBCPP_HIDE_FROM_ABI inline constexpr month& month::operator-=(const months& __dm) noexcept {
  *this = *this - __dm;
  return *this;
}

inline constexpr month January{1};
inline constexpr month February{2};
inline constexpr month March{3};
inline constexpr month April{4};
inline constexpr month May{5};
inline constexpr month June{6};
inline constexpr month July{7};
inline constexpr month August{8};
inline constexpr month September{9};
inline constexpr month October{10};
inline constexpr month November{11};
inline constexpr month December{12};

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___CHRONO_MONTH_H
