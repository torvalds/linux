// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// For information see https://libcxx.llvm.org/DesignDocs/TimeZone.html

#ifndef __LIBCPP_SRC_INCLUDE_TZDB_TYPES_PRIVATE_H
#define __LIBCPP_SRC_INCLUDE_TZDB_TYPES_PRIVATE_H

#include <chrono>
#include <string>
#include <utility>
#include <variant>
#include <vector>

_LIBCPP_BEGIN_NAMESPACE_STD

// TODO TZDB
// The helper classes in this header have no constructor but are loaded with
// dedicated parse functions. In the original design this header was public and
// the parsing was done in the dylib. In that design having constructors would
// expand the ABI interface. Since this header is now in the dylib that design
// should be reconsidered. (For now the design is kept as is, in case this
// header needs to be public for unforseen reasons.)

namespace chrono::__tz {

// Sun>=8   first Sunday on or after the eighth
// Sun<=25  last Sunday on or before the 25th
struct __constrained_weekday {
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI year_month_day operator()(year __year, month __month) const {
    auto __result = static_cast<sys_days>(year_month_day{__year, __month, __day});
    weekday __wd{static_cast<sys_days>(__result)};

    if (__comparison == __le)
      __result -= __wd - __weekday;
    else
      __result += __weekday - __wd;

    return __result;
  }

  weekday __weekday;
  enum __comparison_t { __le, __ge } __comparison;
  day __day;
};

// The on field has a few alternative presentations
//  5        the fifth of the month
//  lastSun  the last Sunday in the month
//  lastMon  the last Monday in the month
//  Sun>=8   first Sunday on or after the eighth
//  Sun<=25  last Sunday on or before the 25th
using __on = variant<day, weekday_last, __constrained_weekday>;

enum class __clock { __local, __standard, __universal };

struct __at {
  seconds __time{0};
  __tz::__clock __clock{__tz::__clock::__local};
};

struct __save {
  seconds __time;
  bool __is_dst;
};

// The names of the fields match the fields of a Rule.
struct __rule {
  year __from;
  year __to;
  month __in;
  __tz::__on __on;
  __tz::__at __at;
  __tz::__save __save;
  string __letters;
};

using __rules_storage_type = std::vector<std::pair<string, vector<__tz::__rule>>>; // TODO TZDB use flat_map;

struct __continuation {
  // Non-owning link to the RULE entries.
  __tz::__rules_storage_type* __rule_database_;

  seconds __stdoff;

  // The RULES is either a SAVE or a NAME.
  // The size_t is used as cache. After loading the rules they are
  // sorted and remain stable, then an index in the vector can be
  // used.
  // If this field contains - then standard time always
  // applies. This is indicated by the monostate.
  // TODO TZDB Investigate implantation the size_t based caching.
  using __rules_t = variant<monostate, __tz::__save, string /*, size_t*/>;

  __rules_t __rules;

  string __format;
  // TODO TZDB the until field can contain more than just a year.
  // Parts of the UNTIL, the optional parts are default initialized
  //    optional<year> __until_;
  year __year = chrono::year::min();
  month __in{January};
  __tz::__on __on{chrono::day{1}};
  __tz::__at __at{chrono::seconds{0}, __tz::__clock::__local};
};

} // namespace chrono::__tz

_LIBCPP_END_NAMESPACE_STD

#endif // __LIBCPP_SRC_INCLUDE_TZDB_TYPES_PRIVATE_H
