// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {

  namespace chrono {
    using std::chrono::duration;
    using std::chrono::time_point;

  } // namespace chrono

  using std::common_type;

  namespace chrono {

    // [time.traits], customization traits
    using std::chrono::treat_as_floating_point;
    using std::chrono::treat_as_floating_point_v;

    using std::chrono::duration_values;

    // using std::chrono::is_clock;
    // using std::chrono::is_clock_v;

    // [time.duration.nonmember], duration arithmetic
    using std::chrono::operator+;
    using std::chrono::operator-;
    using std::chrono::operator*;
    using std::chrono::operator/;
    using std::chrono::operator%;

    // [time.duration.comparisons], duration comparisons
    using std::chrono::operator==;
    using std::chrono::operator<;
    using std::chrono::operator>;
    using std::chrono::operator<=;
    using std::chrono::operator>=;
    using std::chrono::operator<=>;

    // [time.duration.cast], conversions
    using std::chrono::ceil;
    using std::chrono::duration_cast;
    using std::chrono::floor;
    using std::chrono::round;

    // [time.duration.io], duration I/O
#ifndef _LIBCPP_HAS_NO_LOCALIZATION
    using std::chrono::operator<<;
#endif
    // using std::chrono::from_stream;

    // convenience typedefs
    using std::chrono::days;
    using std::chrono::hours;
    using std::chrono::microseconds;
    using std::chrono::milliseconds;
    using std::chrono::minutes;
    using std::chrono::months;
    using std::chrono::nanoseconds;
    using std::chrono::seconds;
    using std::chrono::weeks;
    using std::chrono::years;

    // [time.point.nonmember], time_point arithmetic

    // [time.point.comparisons], time_point comparisons

    // [time.point.cast], conversions
    using std::chrono::time_point_cast;

    // [time.duration.alg], specialized algorithms
    using std::chrono::abs;

    // [time.clock.system], class system_clock
    using std::chrono::system_clock;

    using std::chrono::sys_days;
    using std::chrono::sys_seconds;
    using std::chrono::sys_time;

#if 0
    // [time.clock.utc], class utc_clock
    using std::chrono::utc_clock;

    using std::chrono::utc_seconds;
    using std::chrono::utc_time;

    using std::chrono::leap_second_info;

    using std::chrono::get_leap_second_info;
    // [time.clock.tai], class tai_clock
    using std::chrono::tai_clock;

    using std::chrono::tai_seconds;
    using std::chrono::tai_time;

    // [time.clock.gps], class gps_clock
    using std::chrono::gps_clock;

    using std::chrono::gps_seconds;
    using std::chrono::gps_time;
#endif
    // [time.clock.file], type file_clock
    using std::chrono::file_clock;

    using std::chrono::file_time;

#ifndef _LIBCPP_HAS_NO_MONOTONIC_CLOCK
    // [time.clock.steady], class steady_clock
    using std::chrono::steady_clock;
#endif

    // [time.clock.hires], class high_resolution_clock
    using std::chrono::high_resolution_clock;

    // [time.clock.local], local time
    using std::chrono::local_days;
    using std::chrono::local_seconds;
    using std::chrono::local_t;
    using std::chrono::local_time;

    // [time.clock.cast], time_point conversions
    // using std::chrono::clock_time_conversion;

    // using std::chrono::clock_cast;

    // [time.cal.last], class last_spec
    using std::chrono::last_spec;

    // [time.cal.day], class day
    using std::chrono::day;

    // [time.cal.month], class month
    using std::chrono::month;

    // [time.cal.year], class year
    using std::chrono::year;

    // [time.cal.wd], class weekday
    using std::chrono::weekday;

    // [time.cal.wdidx], class weekday_indexed
    using std::chrono::weekday_indexed;

    // [time.cal.wdlast], class weekday_last
    using std::chrono::weekday_last;

    // [time.cal.md], class month_day
    using std::chrono::month_day;

    // [time.cal.mdlast], class month_day_last
    using std::chrono::month_day_last;

    // [time.cal.mwd], class month_weekday
    using std::chrono::month_weekday;

    // [time.cal.mwdlast], class month_weekday_last
    using std::chrono::month_weekday_last;

    // [time.cal.ym], class year_month
    using std::chrono::year_month;

    // [time.cal.ymd], class year_month_day
    using std::chrono::year_month_day;

    // [time.cal.ymdlast], class year_month_day_last
    using std::chrono::year_month_day_last;

    // [time.cal.ymwd], class year_month_weekday
    using std::chrono::year_month_weekday;

    // [time.cal.ymwdlast], class year_month_weekday_last
    using std::chrono::year_month_weekday_last;

    // [time.cal.operators], civil calendar conventional syntax operators

    // [time.hms], class template hh_mm_ss
    using std::chrono::hh_mm_ss;

    // [time.12], 12/24 hour functions
    using std::chrono::is_am;
    using std::chrono::is_pm;
    using std::chrono::make12;
    using std::chrono::make24;

#ifdef _LIBCPP_ENABLE_EXPERIMENTAL

#  if !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&                            \
      !defined(_LIBCPP_HAS_NO_LOCALIZATION)

    // [time.zone.db], time zone database
    using std::chrono::tzdb;
    using std::chrono::tzdb_list;

    // [time.zone.db.access], time zone database access
    using std::chrono::current_zone;
    using std::chrono::get_tzdb;
    using std::chrono::get_tzdb_list;
    using std::chrono::locate_zone;

    // [time.zone.db.remote], remote time zone database support
    using std::chrono::reload_tzdb;
    using std::chrono::remote_version;

#  endif //  !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&
         //  !defined(_LIBCPP_HAS_NO_LOCALIZATION)

    // [time.zone.exception], exception classes
    using std::chrono::ambiguous_local_time;
    using std::chrono::nonexistent_local_time;

    // [time.zone.info], information classes
    using std::chrono::local_info;
    using std::chrono::sys_info;

#  if !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&                            \
      !defined(_LIBCPP_HAS_NO_LOCALIZATION)

    // [time.zone.timezone], class time_zone
    using std::chrono::choose;
    using std::chrono::time_zone;

    // [time.zone.zonedtraits], class template zoned_traits
    using std::chrono::zoned_traits;

    // [time.zone.zonedtime], class template zoned_time
    using std::chrono::zoned_time;

    using std::chrono::zoned_seconds;

    // [time.zone.leap], leap second support
    using std::chrono::leap_second;

    // [time.zone.link], class time_zone_link
    using std::chrono::time_zone_link;

#    if 0
    // [time.format], formatting
    using std::chrono::local_time_format;
#    endif
#  endif //  !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&
         //  !defined(_LIBCPP_HAS_NO_LOCALIZATION)
#endif   // _LIBCPP_ENABLE_EXPERIMENTAL

  } // namespace chrono

#ifndef _LIBCPP_HAS_NO_LOCALIZATION
  using std::formatter;
#endif // _LIBCPP_HAS_NO_LOCALIZATION

  namespace chrono {
    // using std::chrono::parse;

    // calendrical constants
    using std::chrono::last;

    using std::chrono::Friday;
    using std::chrono::Monday;
    using std::chrono::Saturday;
    using std::chrono::Sunday;
    using std::chrono::Thursday;
    using std::chrono::Tuesday;
    using std::chrono::Wednesday;

    using std::chrono::April;
    using std::chrono::August;
    using std::chrono::December;
    using std::chrono::February;
    using std::chrono::January;
    using std::chrono::July;
    using std::chrono::June;
    using std::chrono::March;
    using std::chrono::May;
    using std::chrono::November;
    using std::chrono::October;
    using std::chrono::September;

  } // namespace chrono

} // namespace std
export namespace std::inline literals::inline chrono_literals {
  // [time.duration.literals], suffixes for duration literals
  using std::literals::chrono_literals::operator""h;
  using std::literals::chrono_literals::operator""min;
  using std::literals::chrono_literals::operator""s;
  using std::literals::chrono_literals::operator""ms;
  using std::literals::chrono_literals::operator""us;
  using std::literals::chrono_literals::operator""ns;

  // [using std::literals::chrono_literals::.cal.day.nonmembers], non-member functions
  using std::literals::chrono_literals::operator""d;

  // [using std::literals::chrono_literals::.cal.year.nonmembers], non-member functions
  using std::literals::chrono_literals::operator""y;
} // namespace std::inline literals::inline chrono_literals
