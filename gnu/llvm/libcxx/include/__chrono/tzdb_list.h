// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// For information see https://libcxx.llvm.org/DesignDocs/TimeZone.html

#ifndef _LIBCPP___CHRONO_TZDB_LIST_H
#define _LIBCPP___CHRONO_TZDB_LIST_H

#include <version>
// Enable the contents of the header only when libc++ was built with experimental features enabled.
#if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#  include <__chrono/time_zone.h>
#  include <__chrono/tzdb.h>
#  include <__config>
#  include <__fwd/string.h>
#  include <forward_list>

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

_LIBCPP_BEGIN_NAMESPACE_STD

#  if _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&   \
      !defined(_LIBCPP_HAS_NO_LOCALIZATION)

namespace chrono {

// TODO TZDB
// Libc++ recently switched to only export __ugly_names from the dylib.
// Since the library is still experimental the functions in this header
// should be adapted to this new style. The other tzdb headers should be
// evaluated too.

class _LIBCPP_AVAILABILITY_TZDB tzdb_list {
public:
  class __impl; // public to allow construction in dylib
  _LIBCPP_HIDE_FROM_ABI explicit tzdb_list(__impl* __p) : __impl_(__p) {
    _LIBCPP_ASSERT_NON_NULL(__impl_ != nullptr, "initialized time_zone without a valid pimpl object");
  }
  _LIBCPP_EXPORTED_FROM_ABI ~tzdb_list();

  tzdb_list(const tzdb_list&)            = delete;
  tzdb_list& operator=(const tzdb_list&) = delete;

  using const_iterator = forward_list<tzdb>::const_iterator;

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI const tzdb& front() const noexcept { return __front(); }

  _LIBCPP_HIDE_FROM_ABI const_iterator erase_after(const_iterator __p) { return __erase_after(__p); }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI const_iterator begin() const noexcept { return __begin(); }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI const_iterator end() const noexcept { return __end(); }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI const_iterator cbegin() const noexcept { return __cbegin(); }
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI const_iterator cend() const noexcept { return __cend(); }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI __impl& __implementation() { return *__impl_; }

private:
  [[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI const tzdb& __front() const noexcept;

  _LIBCPP_EXPORTED_FROM_ABI const_iterator __erase_after(const_iterator __p);

  [[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI const_iterator __begin() const noexcept;
  [[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI const_iterator __end() const noexcept;

  [[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI const_iterator __cbegin() const noexcept;
  [[nodiscard]] _LIBCPP_EXPORTED_FROM_ABI const_iterator __cend() const noexcept;

  __impl* __impl_;
};

[[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_EXPORTED_FROM_ABI tzdb_list& get_tzdb_list();

[[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_HIDE_FROM_ABI inline const tzdb& get_tzdb() {
  return get_tzdb_list().front();
}

[[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_HIDE_FROM_ABI inline const time_zone* locate_zone(string_view __name) {
  return get_tzdb().locate_zone(__name);
}

[[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_HIDE_FROM_ABI inline const time_zone* current_zone() {
  return get_tzdb().current_zone();
}

_LIBCPP_AVAILABILITY_TZDB _LIBCPP_EXPORTED_FROM_ABI const tzdb& reload_tzdb();

[[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_EXPORTED_FROM_ABI string remote_version();

} // namespace chrono

#  endif // _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM)
         // && !defined(_LIBCPP_HAS_NO_LOCALIZATION)

_LIBCPP_END_NAMESPACE_STD

#endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#endif // _LIBCPP___CHRONO_TZDB_LIST_H
