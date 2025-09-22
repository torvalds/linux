// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// For information see https://libcxx.llvm.org/DesignDocs/TimeZone.html

#ifndef _LIBCPP___CHRONO_EXCEPTION_H
#define _LIBCPP___CHRONO_EXCEPTION_H

#include <version>
// Enable the contents of the header only when libc++ was built with experimental features enabled.
#if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#  include <__chrono/calendar.h>
#  include <__chrono/local_info.h>
#  include <__chrono/time_point.h>
#  include <__config>
#  include <__configuration/availability.h>
#  include <__verbose_abort>
#  include <format>
#  include <stdexcept>
#  include <string>

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

_LIBCPP_BEGIN_NAMESPACE_STD

#  if _LIBCPP_STD_VER >= 20

namespace chrono {

class nonexistent_local_time : public runtime_error {
public:
  template <class _Duration>
  _LIBCPP_HIDE_FROM_ABI nonexistent_local_time(const local_time<_Duration>& __time, const local_info& __info)
      : runtime_error{__create_message(__time, __info)} {
    // [time.zone.exception.nonexist]/2
    //   Preconditions: i.result == local_info::nonexistent is true.
    // The value of __info.result is not used.
    _LIBCPP_ASSERT_PEDANTIC(__info.result == local_info::nonexistent,
                            "creating an nonexistent_local_time from a local_info that is not non-existent");
  }

  _LIBCPP_HIDE_FROM_ABI nonexistent_local_time(const nonexistent_local_time&)            = default;
  _LIBCPP_HIDE_FROM_ABI nonexistent_local_time& operator=(const nonexistent_local_time&) = default;

  _LIBCPP_AVAILABILITY_TZDB _LIBCPP_EXPORTED_FROM_ABI ~nonexistent_local_time() override; // exported as key function

private:
  template <class _Duration>
  _LIBCPP_HIDE_FROM_ABI string __create_message(const local_time<_Duration>& __time, const local_info& __info) {
    return std::format(
        R"({} is in a gap between
{} {} and
{} {} which are both equivalent to
{} UTC)",
        __time,
        local_seconds{__info.first.end.time_since_epoch()} + __info.first.offset,
        __info.first.abbrev,
        local_seconds{__info.second.begin.time_since_epoch()} + __info.second.offset,
        __info.second.abbrev,
        __info.first.end);
  }
};

template <class _Duration>
_LIBCPP_NORETURN _LIBCPP_AVAILABILITY_TZDB _LIBCPP_HIDE_FROM_ABI void __throw_nonexistent_local_time(
    [[maybe_unused]] const local_time<_Duration>& __time, [[maybe_unused]] const local_info& __info) {
#    ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  throw nonexistent_local_time(__time, __info);
#    else
  _LIBCPP_VERBOSE_ABORT("nonexistent_local_time was thrown in -fno-exceptions mode");
#    endif
}

class ambiguous_local_time : public runtime_error {
public:
  template <class _Duration>
  _LIBCPP_HIDE_FROM_ABI ambiguous_local_time(const local_time<_Duration>& __time, const local_info& __info)
      : runtime_error{__create_message(__time, __info)} {
    // [time.zone.exception.ambig]/2
    //   Preconditions: i.result == local_info::ambiguous is true.
    // The value of __info.result is not used.
    _LIBCPP_ASSERT_PEDANTIC(__info.result == local_info::ambiguous,
                            "creating an ambiguous_local_time from a local_info that is not ambiguous");
  }

  _LIBCPP_HIDE_FROM_ABI ambiguous_local_time(const ambiguous_local_time&)            = default;
  _LIBCPP_HIDE_FROM_ABI ambiguous_local_time& operator=(const ambiguous_local_time&) = default;

  _LIBCPP_AVAILABILITY_TZDB _LIBCPP_EXPORTED_FROM_ABI ~ambiguous_local_time() override; // exported as key function

private:
  template <class _Duration>
  _LIBCPP_HIDE_FROM_ABI string __create_message(const local_time<_Duration>& __time, const local_info& __info) {
    return std::format(
        // There are two spaces after the full-stop; this has been verified
        // in the sources of the Standard.
        R"({0} is ambiguous.  It could be
{0} {1} == {2} UTC or
{0} {3} == {4} UTC)",
        __time,
        __info.first.abbrev,
        __time - __info.first.offset,
        __info.second.abbrev,
        __time - __info.second.offset);
  }
};

template <class _Duration>
_LIBCPP_NORETURN _LIBCPP_AVAILABILITY_TZDB _LIBCPP_HIDE_FROM_ABI void __throw_ambiguous_local_time(
    [[maybe_unused]] const local_time<_Duration>& __time, [[maybe_unused]] const local_info& __info) {
#    ifndef _LIBCPP_HAS_NO_EXCEPTIONS
  throw ambiguous_local_time(__time, __info);
#    else
  _LIBCPP_VERBOSE_ABORT("ambiguous_local_time was thrown in -fno-exceptions mode");
#    endif
}

} // namespace chrono

#  endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#endif // _LIBCPP___CHRONO_EXCEPTION_H
