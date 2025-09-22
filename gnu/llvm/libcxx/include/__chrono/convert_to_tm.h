// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_CONVERT_TO_TM_H
#define _LIBCPP___CHRONO_CONVERT_TO_TM_H

#include <__chrono/calendar.h>
#include <__chrono/concepts.h>
#include <__chrono/day.h>
#include <__chrono/duration.h>
#include <__chrono/file_clock.h>
#include <__chrono/hh_mm_ss.h>
#include <__chrono/local_info.h>
#include <__chrono/month.h>
#include <__chrono/month_weekday.h>
#include <__chrono/monthday.h>
#include <__chrono/statically_widen.h>
#include <__chrono/sys_info.h>
#include <__chrono/system_clock.h>
#include <__chrono/time_point.h>
#include <__chrono/weekday.h>
#include <__chrono/year.h>
#include <__chrono/year_month.h>
#include <__chrono/year_month_day.h>
#include <__chrono/year_month_weekday.h>
#include <__chrono/zoned_time.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__format/format_error.h>
#include <__memory/addressof.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_specialization.h>
#include <cstdint>
#include <ctime>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// Conerts a chrono date and weekday to a given _Tm type.
//
// This is an implementation detail for the function
//   template <class _Tm, class _ChronoT>
//   _Tm __convert_to_tm(const _ChronoT& __value)
//
// This manually converts the two values to the proper type. It is possible to
// convert from sys_days to time_t and then to _Tm. But this leads to the Y2K
// bug when time_t is a 32-bit signed integer. Chrono considers years beyond
// the year 2038 valid, so instead do the transformation manually.
template <class _Tm, class _Date>
  requires(same_as<_Date, chrono::year_month_day> || same_as<_Date, chrono::year_month_day_last>)
_LIBCPP_HIDE_FROM_ABI _Tm __convert_to_tm(const _Date& __date, chrono::weekday __weekday) {
  _Tm __result = {};
#  ifdef __GLIBC__
  __result.tm_zone = "UTC";
#  endif
  __result.tm_year = static_cast<int>(__date.year()) - 1900;
  __result.tm_mon  = static_cast<unsigned>(__date.month()) - 1;
  __result.tm_mday = static_cast<unsigned>(__date.day());
  __result.tm_wday = static_cast<unsigned>(__weekday.c_encoding());
  __result.tm_yday =
      (static_cast<chrono::sys_days>(__date) -
       static_cast<chrono::sys_days>(chrono::year_month_day{__date.year(), chrono::January, chrono::day{1}}))
          .count();

  return __result;
}

template <class _Tm, class _Duration>
_LIBCPP_HIDE_FROM_ABI _Tm __convert_to_tm(const chrono::sys_time<_Duration> __tp) {
  chrono::sys_days __days = chrono::floor<chrono::days>(__tp);
  chrono::year_month_day __ymd{__days};

  _Tm __result = std::__convert_to_tm<_Tm>(chrono::year_month_day{__ymd}, chrono::weekday{__days});

  uint64_t __sec =
      chrono::duration_cast<chrono::seconds>(__tp - chrono::time_point_cast<chrono::seconds>(__days)).count();
  __sec %= 24 * 3600;
  __result.tm_hour = __sec / 3600;
  __sec %= 3600;
  __result.tm_min = __sec / 60;
  __result.tm_sec = __sec % 60;

  return __result;
}

// Convert a chrono (calendar) time point, or dururation to the given _Tm type,
// which must have the same properties as std::tm.
template <class _Tm, class _ChronoT>
_LIBCPP_HIDE_FROM_ABI _Tm __convert_to_tm(const _ChronoT& __value) {
  _Tm __result = {};
#  ifdef __GLIBC__
  __result.tm_zone = "UTC";
#  endif

  if constexpr (__is_time_point<_ChronoT>) {
    if constexpr (same_as<typename _ChronoT::clock, chrono::system_clock>)
      return std::__convert_to_tm<_Tm>(__value);
    else if constexpr (same_as<typename _ChronoT::clock, chrono::file_clock>)
      return std::__convert_to_tm<_Tm>(_ChronoT::clock::to_sys(__value));
    else if constexpr (same_as<typename _ChronoT::clock, chrono::local_t>)
      return std::__convert_to_tm<_Tm>(chrono::sys_time<typename _ChronoT::duration>{__value.time_since_epoch()});
    else
      static_assert(sizeof(_ChronoT) == 0, "TODO: Add the missing clock specialization");
  } else if constexpr (chrono::__is_duration<_ChronoT>::value) {
    // [time.format]/6
    //   ...  However, if a flag refers to a "time of day" (e.g. %H, %I, %p,
    //   etc.), then a specialization of duration is interpreted as the time of
    //   day elapsed since midnight.

    // Not all values can be converted to hours, it may run into ratio
    // conversion errors. In that case the conversion to seconds works.
    if constexpr (is_convertible_v<_ChronoT, chrono::hours>) {
      auto __hour      = chrono::floor<chrono::hours>(__value);
      auto __sec       = chrono::duration_cast<chrono::seconds>(__value - __hour);
      __result.tm_hour = __hour.count() % 24;
      __result.tm_min  = __sec.count() / 60;
      __result.tm_sec  = __sec.count() % 60;
    } else {
      uint64_t __sec = chrono::duration_cast<chrono::seconds>(__value).count();
      __sec %= 24 * 3600;
      __result.tm_hour = __sec / 3600;
      __sec %= 3600;
      __result.tm_min = __sec / 60;
      __result.tm_sec = __sec % 60;
    }
  } else if constexpr (same_as<_ChronoT, chrono::day>)
    __result.tm_mday = static_cast<unsigned>(__value);
  else if constexpr (same_as<_ChronoT, chrono::month>)
    __result.tm_mon = static_cast<unsigned>(__value) - 1;
  else if constexpr (same_as<_ChronoT, chrono::year>)
    __result.tm_year = static_cast<int>(__value) - 1900;
  else if constexpr (same_as<_ChronoT, chrono::weekday>)
    __result.tm_wday = __value.c_encoding();
  else if constexpr (same_as<_ChronoT, chrono::weekday_indexed> || same_as<_ChronoT, chrono::weekday_last>)
    __result.tm_wday = __value.weekday().c_encoding();
  else if constexpr (same_as<_ChronoT, chrono::month_day>) {
    __result.tm_mday = static_cast<unsigned>(__value.day());
    __result.tm_mon  = static_cast<unsigned>(__value.month()) - 1;
  } else if constexpr (same_as<_ChronoT, chrono::month_day_last>) {
    __result.tm_mon = static_cast<unsigned>(__value.month()) - 1;
  } else if constexpr (same_as<_ChronoT, chrono::month_weekday> || same_as<_ChronoT, chrono::month_weekday_last>) {
    __result.tm_wday = __value.weekday_indexed().weekday().c_encoding();
    __result.tm_mon  = static_cast<unsigned>(__value.month()) - 1;
  } else if constexpr (same_as<_ChronoT, chrono::year_month>) {
    __result.tm_year = static_cast<int>(__value.year()) - 1900;
    __result.tm_mon  = static_cast<unsigned>(__value.month()) - 1;
  } else if constexpr (same_as<_ChronoT, chrono::year_month_day> || same_as<_ChronoT, chrono::year_month_day_last>) {
    return std::__convert_to_tm<_Tm>(
        chrono::year_month_day{__value}, chrono::weekday{static_cast<chrono::sys_days>(__value)});
  } else if constexpr (same_as<_ChronoT, chrono::year_month_weekday> ||
                       same_as<_ChronoT, chrono::year_month_weekday_last>) {
    return std::__convert_to_tm<_Tm>(chrono::year_month_day{static_cast<chrono::sys_days>(__value)}, __value.weekday());
  } else if constexpr (__is_hh_mm_ss<_ChronoT>) {
    __result.tm_sec = __value.seconds().count();
    __result.tm_min = __value.minutes().count();
    // In libc++ hours is stored as a long. The type in std::tm is an int. So
    // the overflow can only occur when hour uses more bits than an int
    // provides.
    if constexpr (sizeof(std::chrono::hours::rep) > sizeof(__result.tm_hour))
      if (__value.hours().count() > std::numeric_limits<decltype(__result.tm_hour)>::max())
        std::__throw_format_error("Formatting hh_mm_ss, encountered an hour overflow");
    __result.tm_hour = __value.hours().count();
#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  } else if constexpr (same_as<_ChronoT, chrono::sys_info>) {
    // Has no time information.
  } else if constexpr (same_as<_ChronoT, chrono::local_info>) {
    // Has no time information.
#    if !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&                          \
        !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  } else if constexpr (__is_specialization_v<_ChronoT, chrono::zoned_time>) {
    return std::__convert_to_tm<_Tm>(
        chrono::sys_time<typename _ChronoT::duration>{__value.get_local_time().time_since_epoch()});
#    endif
#  endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  } else
    static_assert(sizeof(_ChronoT) == 0, "Add the missing type specialization");

  return __result;
}

#endif // if _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___CHRONO_CONVERT_TO_TM_H
