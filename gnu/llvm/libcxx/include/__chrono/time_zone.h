// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// For information see https://libcxx.llvm.org/DesignDocs/TimeZone.html

#ifndef _LIBCPP___CHRONO_TIME_ZONE_H
#define _LIBCPP___CHRONO_TIME_ZONE_H

#include <version>
// Enable the contents of the header only when libc++ was built with experimental features enabled.
#if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#  include <__chrono/calendar.h>
#  include <__chrono/duration.h>
#  include <__chrono/exception.h>
#  include <__chrono/local_info.h>
#  include <__chrono/sys_info.h>
#  include <__chrono/system_clock.h>
#  include <__compare/strong_order.h>
#  include <__config>
#  include <__memory/unique_ptr.h>
#  include <__type_traits/common_type.h>
#  include <string_view>

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

_LIBCPP_PUSH_MACROS
#  include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#  if _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&   \
      !defined(_LIBCPP_HAS_NO_LOCALIZATION)

namespace chrono {

enum class choose { earliest, latest };

class _LIBCPP_AVAILABILITY_TZDB time_zone {
  _LIBCPP_HIDE_FROM_ABI time_zone() = default;

public:
  class __impl; // public so it can be used by make_unique.

  // The "constructor".
  //
  // The default constructor is private to avoid the constructor from being
  // part of the ABI. Instead use an __ugly_named function as an ABI interface,
  // since that gives us the ability to change it in the future.
  [[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI static time_zone __create(unique_ptr<__impl>&& __p);

  _LIBCPP_EXPORTED_FROM_ABI ~time_zone();

  _LIBCPP_HIDE_FROM_ABI time_zone(time_zone&&)            = default;
  _LIBCPP_HIDE_FROM_ABI time_zone& operator=(time_zone&&) = default;

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI string_view name() const noexcept { return __name(); }

  template <class _Duration>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI sys_info get_info(const sys_time<_Duration>& __time) const {
    return __get_info(chrono::time_point_cast<seconds>(__time));
  }

  template <class _Duration>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI local_info get_info(const local_time<_Duration>& __time) const {
    return __get_info(chrono::time_point_cast<seconds>(__time));
  }

  // We don't apply nodiscard here since this function throws on many inputs,
  // so it could be used as a validation.
  template <class _Duration>
  _LIBCPP_HIDE_FROM_ABI sys_time<common_type_t<_Duration, seconds>> to_sys(const local_time<_Duration>& __time) const {
    local_info __info = get_info(__time);
    switch (__info.result) {
    case local_info::unique:
      return sys_time<common_type_t<_Duration, seconds>>{__time.time_since_epoch() - __info.first.offset};

    case local_info::nonexistent:
      chrono::__throw_nonexistent_local_time(__time, __info);

    case local_info::ambiguous:
      chrono::__throw_ambiguous_local_time(__time, __info);
    }

    // TODO TZDB The Standard does not specify anything in these cases.
    _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(
        __info.result != -1, "cannot convert the local time; it would be before the minimum system clock value");
    _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(
        __info.result != -2, "cannot convert the local time; it would be after the maximum system clock value");

    return {};
  }

  template <class _Duration>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI sys_time<common_type_t<_Duration, seconds>>
  to_sys(const local_time<_Duration>& __time, choose __z) const {
    local_info __info = get_info(__time);
    switch (__info.result) {
    case local_info::unique:
    case local_info::nonexistent: // first and second are the same
      return sys_time<common_type_t<_Duration, seconds>>{__time.time_since_epoch() - __info.first.offset};

    case local_info::ambiguous:
      switch (__z) {
      case choose::earliest:
        return sys_time<common_type_t<_Duration, seconds>>{__time.time_since_epoch() - __info.first.offset};

      case choose::latest:
        return sys_time<common_type_t<_Duration, seconds>>{__time.time_since_epoch() - __info.second.offset};

        // Note a value out of bounds is not specified.
      }
    }

    // TODO TZDB The standard does not specify anything in these cases.
    _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(
        __info.result != -1, "cannot convert the local time; it would be before the minimum system clock value");
    _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(
        __info.result != -2, "cannot convert the local time; it would be after the maximum system clock value");

    return {};
  }

  template <class _Duration>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI local_time<common_type_t<_Duration, seconds>>
  to_local(const sys_time<_Duration>& __time) const {
    using _Dp = common_type_t<_Duration, seconds>;

    sys_info __info = get_info(__time);

    _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(
        __info.offset >= chrono::seconds{0} || __time.time_since_epoch() >= _Dp::min() - __info.offset,
        "cannot convert the system time; it would be before the minimum local clock value");

    _LIBCPP_ASSERT_ARGUMENT_WITHIN_DOMAIN(
        __info.offset <= chrono::seconds{0} || __time.time_since_epoch() <= _Dp::max() - __info.offset,
        "cannot convert the system time; it would be after the maximum local clock value");

    return local_time<_Dp>{__time.time_since_epoch() + __info.offset};
  }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI const __impl& __implementation() const noexcept { return *__impl_; }

private:
  [[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI string_view __name() const noexcept;

  [[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_EXPORTED_FROM_ABI sys_info __get_info(sys_seconds __time) const;
  [[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_EXPORTED_FROM_ABI local_info __get_info(local_seconds __time) const;

  unique_ptr<__impl> __impl_;
};

[[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_HIDE_FROM_ABI inline bool
operator==(const time_zone& __x, const time_zone& __y) noexcept {
  return __x.name() == __y.name();
}

[[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_HIDE_FROM_ABI inline strong_ordering
operator<=>(const time_zone& __x, const time_zone& __y) noexcept {
  return __x.name() <=> __y.name();
}

} // namespace chrono

#  endif // _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM)
         // && !defined(_LIBCPP_HAS_NO_LOCALIZATION)

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#endif // _LIBCPP___CHRONO_TIME_ZONE_H
