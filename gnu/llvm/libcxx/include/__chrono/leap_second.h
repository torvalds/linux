// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// For information see https://libcxx.llvm.org/DesignDocs/TimeZone.html

#ifndef _LIBCPP___CHRONO_LEAP_SECOND_H
#define _LIBCPP___CHRONO_LEAP_SECOND_H

#include <version>
// Enable the contents of the header only when libc++ was built with experimental features enabled.
#if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#  include <__chrono/duration.h>
#  include <__chrono/system_clock.h>
#  include <__chrono/time_point.h>
#  include <__compare/ordering.h>
#  include <__compare/three_way_comparable.h>
#  include <__config>
#  include <__utility/private_constructor_tag.h>

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

_LIBCPP_BEGIN_NAMESPACE_STD

#  if _LIBCPP_STD_VER >= 20

namespace chrono {

class leap_second {
public:
  [[nodiscard]]
  _LIBCPP_HIDE_FROM_ABI explicit constexpr leap_second(__private_constructor_tag, sys_seconds __date, seconds __value)
      : __date_(__date), __value_(__value) {}

  _LIBCPP_HIDE_FROM_ABI leap_second(const leap_second&)            = default;
  _LIBCPP_HIDE_FROM_ABI leap_second& operator=(const leap_second&) = default;

  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI constexpr sys_seconds date() const noexcept { return __date_; }

  _LIBCPP_NODISCARD _LIBCPP_HIDE_FROM_ABI constexpr seconds value() const noexcept { return __value_; }

private:
  sys_seconds __date_;
  seconds __value_;
};

_LIBCPP_HIDE_FROM_ABI inline constexpr bool operator==(const leap_second& __x, const leap_second& __y) {
  return __x.date() == __y.date();
}

_LIBCPP_HIDE_FROM_ABI inline constexpr strong_ordering operator<=>(const leap_second& __x, const leap_second& __y) {
  return __x.date() <=> __y.date();
}

template <class _Duration>
_LIBCPP_HIDE_FROM_ABI constexpr bool operator==(const leap_second& __x, const sys_time<_Duration>& __y) {
  return __x.date() == __y;
}

template <class _Duration>
_LIBCPP_HIDE_FROM_ABI constexpr bool operator<(const leap_second& __x, const sys_time<_Duration>& __y) {
  return __x.date() < __y;
}

template <class _Duration>
_LIBCPP_HIDE_FROM_ABI constexpr bool operator<(const sys_time<_Duration>& __x, const leap_second& __y) {
  return __x < __y.date();
}

template <class _Duration>
_LIBCPP_HIDE_FROM_ABI constexpr bool operator>(const leap_second& __x, const sys_time<_Duration>& __y) {
  return __y < __x;
}

template <class _Duration>
_LIBCPP_HIDE_FROM_ABI constexpr bool operator>(const sys_time<_Duration>& __x, const leap_second& __y) {
  return __y < __x;
}

template <class _Duration>
_LIBCPP_HIDE_FROM_ABI constexpr bool operator<=(const leap_second& __x, const sys_time<_Duration>& __y) {
  return !(__y < __x);
}

template <class _Duration>
_LIBCPP_HIDE_FROM_ABI constexpr bool operator<=(const sys_time<_Duration>& __x, const leap_second& __y) {
  return !(__y < __x);
}

template <class _Duration>
_LIBCPP_HIDE_FROM_ABI constexpr bool operator>=(const leap_second& __x, const sys_time<_Duration>& __y) {
  return !(__x < __y);
}

template <class _Duration>
_LIBCPP_HIDE_FROM_ABI constexpr bool operator>=(const sys_time<_Duration>& __x, const leap_second& __y) {
  return !(__x < __y);
}

#    ifndef _LIBCPP_COMPILER_GCC
// This requirement cause a compilation loop in GCC-13 and running out of memory.
// TODO TZDB Test whether GCC-14 fixes this.
template <class _Duration>
  requires three_way_comparable_with<sys_seconds, sys_time<_Duration>>
_LIBCPP_HIDE_FROM_ABI constexpr auto operator<=>(const leap_second& __x, const sys_time<_Duration>& __y) {
  return __x.date() <=> __y;
}
#    endif

} // namespace chrono

#  endif //_LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#endif // _LIBCPP___CHRONO_LEAP_SECOND_H
