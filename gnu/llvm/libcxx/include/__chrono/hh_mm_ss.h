// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_HH_MM_SS_H
#define _LIBCPP___CHRONO_HH_MM_SS_H

#include <__chrono/duration.h>
#include <__chrono/time_point.h>
#include <__config>
#include <__type_traits/common_type.h>
#include <ratio>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

template <class _Duration>
class hh_mm_ss {
private:
  static_assert(__is_duration<_Duration>::value, "template parameter of hh_mm_ss must be a std::chrono::duration");
  using __CommonType = common_type_t<_Duration, chrono::seconds>;

  _LIBCPP_HIDE_FROM_ABI static constexpr uint64_t __pow10(unsigned __exp) {
    uint64_t __ret = 1;
    for (unsigned __i = 0; __i < __exp; ++__i)
      __ret *= 10U;
    return __ret;
  }

  _LIBCPP_HIDE_FROM_ABI static constexpr unsigned __width(uint64_t __n, uint64_t __d = 10, unsigned __w = 0) {
    if (__n >= 2 && __d != 0 && __w < 19)
      return 1 + __width(__n, __d % __n * 10, __w + 1);
    return 0;
  }

public:
  _LIBCPP_HIDE_FROM_ABI static unsigned constexpr fractional_width =
      __width(__CommonType::period::den) < 19 ? __width(__CommonType::period::den) : 6u;
  using precision = duration<typename __CommonType::rep, ratio<1, __pow10(fractional_width)>>;

  _LIBCPP_HIDE_FROM_ABI constexpr hh_mm_ss() noexcept : hh_mm_ss{_Duration::zero()} {}

  _LIBCPP_HIDE_FROM_ABI constexpr explicit hh_mm_ss(_Duration __d) noexcept
      : __is_neg_(__d < _Duration(0)),
        __h_(chrono::duration_cast<chrono::hours>(chrono::abs(__d))),
        __m_(chrono::duration_cast<chrono::minutes>(chrono::abs(__d) - hours())),
        __s_(chrono::duration_cast<chrono::seconds>(chrono::abs(__d) - hours() - minutes())),
        __f_(chrono::duration_cast<precision>(chrono::abs(__d) - hours() - minutes() - seconds())) {}

  _LIBCPP_HIDE_FROM_ABI constexpr bool is_negative() const noexcept { return __is_neg_; }
  _LIBCPP_HIDE_FROM_ABI constexpr chrono::hours hours() const noexcept { return __h_; }
  _LIBCPP_HIDE_FROM_ABI constexpr chrono::minutes minutes() const noexcept { return __m_; }
  _LIBCPP_HIDE_FROM_ABI constexpr chrono::seconds seconds() const noexcept { return __s_; }
  _LIBCPP_HIDE_FROM_ABI constexpr precision subseconds() const noexcept { return __f_; }

  _LIBCPP_HIDE_FROM_ABI constexpr precision to_duration() const noexcept {
    auto __dur = __h_ + __m_ + __s_ + __f_;
    return __is_neg_ ? -__dur : __dur;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr explicit operator precision() const noexcept { return to_duration(); }

private:
  bool __is_neg_;
  chrono::hours __h_;
  chrono::minutes __m_;
  chrono::seconds __s_;
  precision __f_;
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(hh_mm_ss);

_LIBCPP_HIDE_FROM_ABI inline constexpr bool is_am(const hours& __h) noexcept {
  return __h >= hours(0) && __h < hours(12);
}
_LIBCPP_HIDE_FROM_ABI inline constexpr bool is_pm(const hours& __h) noexcept {
  return __h >= hours(12) && __h < hours(24);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr hours make12(const hours& __h) noexcept {
  if (__h == hours(0))
    return hours(12);
  else if (__h <= hours(12))
    return __h;
  else
    return __h - hours(12);
}

_LIBCPP_HIDE_FROM_ABI inline constexpr hours make24(const hours& __h, bool __is_pm) noexcept {
  if (__is_pm)
    return __h == hours(12) ? __h : __h + hours(12);
  else
    return __h == hours(12) ? hours(0) : __h;
}
} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#endif // _LIBCPP___CHRONO_HH_MM_SS_H
