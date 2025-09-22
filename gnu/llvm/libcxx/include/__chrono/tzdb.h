// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// For information see https://libcxx.llvm.org/DesignDocs/TimeZone.html

#ifndef _LIBCPP___CHRONO_TZDB_H
#define _LIBCPP___CHRONO_TZDB_H

#include <version>
// Enable the contents of the header only when libc++ was built with experimental features enabled.
#if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#  include <__algorithm/ranges_lower_bound.h>
#  include <__chrono/leap_second.h>
#  include <__chrono/time_zone.h>
#  include <__chrono/time_zone_link.h>
#  include <__config>
#  include <string>
#  include <vector>

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

_LIBCPP_PUSH_MACROS
#  include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#  if _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&   \
      !defined(_LIBCPP_HAS_NO_LOCALIZATION)

namespace chrono {

struct tzdb {
  string version;
  vector<time_zone> zones;
  vector<time_zone_link> links;

  vector<leap_second> leap_seconds;

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI const time_zone* __locate_zone(string_view __name) const {
    if (const time_zone* __result = __find_in_zone(__name))
      return __result;

    if (auto __it = ranges::lower_bound(links, __name, {}, &time_zone_link::name);
        __it != links.end() && __it->name() == __name)
      if (const time_zone* __result = __find_in_zone(__it->target()))
        return __result;

    return nullptr;
  }

  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI const time_zone* locate_zone(string_view __name) const {
    if (const time_zone* __result = __locate_zone(__name))
      return __result;

    std::__throw_runtime_error("tzdb: requested time zone not found");
  }

  [[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_HIDE_FROM_ABI const time_zone* current_zone() const {
    return __current_zone();
  }

private:
  _LIBCPP_HIDE_FROM_ABI const time_zone* __find_in_zone(string_view __name) const noexcept {
    if (auto __it = ranges::lower_bound(zones, __name, {}, &time_zone::name);
        __it != zones.end() && __it->name() == __name)
      return std::addressof(*__it);

    return nullptr;
  }

  [[nodiscard]] _LIBCPP_AVAILABILITY_TZDB _LIBCPP_EXPORTED_FROM_ABI const time_zone* __current_zone() const;
};

} // namespace chrono

#  endif // _LIBCPP_STD_VER >= 20 && !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM)
         // && !defined(_LIBCPP_HAS_NO_LOCALIZATION)

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#endif // _LIBCPP___CHRONO_TZDB_H
