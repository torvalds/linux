// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_FILE_CLOCK_H
#define _LIBCPP___CHRONO_FILE_CLOCK_H

#include <__chrono/duration.h>
#include <__chrono/system_clock.h>
#include <__chrono/time_point.h>
#include <__config>
#include <ratio>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#ifndef _LIBCPP_CXX03_LANG
_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM
struct _FilesystemClock;
_LIBCPP_END_NAMESPACE_FILESYSTEM
#endif // !_LIBCPP_CXX03_LANG

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

// [time.clock.file], type file_clock
using file_clock = filesystem::_FilesystemClock;

template <class _Duration>
using file_time = time_point<file_clock, _Duration>;

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

#ifndef _LIBCPP_CXX03_LANG
_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM
struct _FilesystemClock {
#  if !defined(_LIBCPP_HAS_NO_INT128)
  typedef __int128_t rep;
  typedef nano period;
#  else
  typedef long long rep;
  typedef nano period;
#  endif

  typedef chrono::duration<rep, period> duration;
  typedef chrono::time_point<_FilesystemClock> time_point;

  _LIBCPP_EXPORTED_FROM_ABI static _LIBCPP_CONSTEXPR_SINCE_CXX14 const bool is_steady = false;

  _LIBCPP_AVAILABILITY_FILESYSTEM_LIBRARY _LIBCPP_EXPORTED_FROM_ABI static time_point now() noexcept;

#  if _LIBCPP_STD_VER >= 20
  template <class _Duration>
  _LIBCPP_HIDE_FROM_ABI static chrono::sys_time<_Duration> to_sys(const chrono::file_time<_Duration>& __t) {
    return chrono::sys_time<_Duration>(__t.time_since_epoch());
  }

  template <class _Duration>
  _LIBCPP_HIDE_FROM_ABI static chrono::file_time<_Duration> from_sys(const chrono::sys_time<_Duration>& __t) {
    return chrono::file_time<_Duration>(__t.time_since_epoch());
  }
#  endif // _LIBCPP_STD_VER >= 20
};
_LIBCPP_END_NAMESPACE_FILESYSTEM
#endif // !_LIBCPP_CXX03_LANG

#endif // _LIBCPP___CHRONO_FILE_CLOCK_H
