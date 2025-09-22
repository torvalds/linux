// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_OSTREAM_H
#define _LIBCPP___CHRONO_OSTREAM_H

#include <__chrono/calendar.h>
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
#include <__chrono/weekday.h>
#include <__chrono/year.h>
#include <__chrono/year_month.h>
#include <__chrono/year_month_day.h>
#include <__chrono/year_month_weekday.h>
#include <__chrono/zoned_time.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__format/format_functions.h>
#include <__fwd/ostream.h>
#include <ratio>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace chrono {

template <class _CharT, class _Traits, class _Duration>
  requires(!treat_as_floating_point_v<typename _Duration::rep> && _Duration{1} < days{1})
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const sys_time<_Duration>& __tp) {
  return __os << std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L%F %T}"), __tp);
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const sys_days& __dp) {
  return __os << year_month_day{__dp};
}

template <class _CharT, class _Traits, class _Duration>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const file_time<_Duration> __tp) {
  return __os << std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L%F %T}"), __tp);
}

template <class _CharT, class _Traits, class _Duration>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const local_time<_Duration> __tp) {
  return __os << sys_time<_Duration>{__tp.time_since_epoch()};
}

// Depending on the type the return is a const _CharT* or a basic_string<_CharT>
template <class _CharT, class _Period>
_LIBCPP_HIDE_FROM_ABI auto __units_suffix() {
  // TODO FMT LWG issue the suffixes are always char and not STATICALLY-WIDEN'ed.
  if constexpr (same_as<typename _Period::type, atto>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "as");
  else if constexpr (same_as<typename _Period::type, femto>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "fs");
  else if constexpr (same_as<typename _Period::type, pico>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "ps");
  else if constexpr (same_as<typename _Period::type, nano>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "ns");
  else if constexpr (same_as<typename _Period::type, micro>)
#  ifndef _LIBCPP_HAS_NO_UNICODE
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "\u00b5s");
#  else
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "us");
#  endif
  else if constexpr (same_as<typename _Period::type, milli>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "ms");
  else if constexpr (same_as<typename _Period::type, centi>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "cs");
  else if constexpr (same_as<typename _Period::type, deci>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "ds");
  else if constexpr (same_as<typename _Period::type, ratio<1>>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "s");
  else if constexpr (same_as<typename _Period::type, deca>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "das");
  else if constexpr (same_as<typename _Period::type, hecto>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "hs");
  else if constexpr (same_as<typename _Period::type, kilo>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "ks");
  else if constexpr (same_as<typename _Period::type, mega>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "Ms");
  else if constexpr (same_as<typename _Period::type, giga>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "Gs");
  else if constexpr (same_as<typename _Period::type, tera>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "Ts");
  else if constexpr (same_as<typename _Period::type, peta>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "Ps");
  else if constexpr (same_as<typename _Period::type, exa>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "Es");
  else if constexpr (same_as<typename _Period::type, ratio<60>>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "min");
  else if constexpr (same_as<typename _Period::type, ratio<3600>>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "h");
  else if constexpr (same_as<typename _Period::type, ratio<86400>>)
    return _LIBCPP_STATICALLY_WIDEN(_CharT, "d");
  else if constexpr (_Period::den == 1)
    return std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "[{}]s"), _Period::num);
  else
    return std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "[{}/{}]s"), _Period::num, _Period::den);
}

template <class _CharT, class _Traits, class _Rep, class _Period>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const duration<_Rep, _Period>& __d) {
  basic_ostringstream<_CharT, _Traits> __s;
  __s.flags(__os.flags());
  __s.imbue(__os.getloc());
  __s.precision(__os.precision());
  __s << __d.count() << chrono::__units_suffix<_CharT, _Period>();
  return __os << __s.str();
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>& operator<<(basic_ostream<_CharT, _Traits>& __os, const day& __d) {
  return __os << (__d.ok() ? std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "{:%d}"), __d)
                           // Note this error differs from the wording of the Standard. The
                           // Standard wording doesn't work well on AIX or Windows. There
                           // the formatted day seems to be either modulo 100 or completely
                           // omitted. Judging by the wording this is valid.
                           // TODO FMT Write a paper of file an LWG issue.
                           : std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "{:02} is not a valid day"),
                                         static_cast<unsigned>(__d)));
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const month& __m) {
  return __os << (__m.ok() ? std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L%b}"), __m)
                           : std::format(__os.getloc(),
                                         _LIBCPP_STATICALLY_WIDEN(_CharT, "{} is not a valid month"),
                                         static_cast<unsigned>(__m))); // TODO FMT Standard mandated locale isn't used.
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const year& __y) {
  return __os << (__y.ok() ? std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "{:%Y}"), __y)
                           : std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "{:%Y} is not a valid year"), __y));
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const weekday& __wd) {
  return __os << (__wd.ok() ? std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L%a}"), __wd)
                            : std::format(__os.getloc(), // TODO FMT Standard mandated locale isn't used.
                                          _LIBCPP_STATICALLY_WIDEN(_CharT, "{} is not a valid weekday"),
                                          static_cast<unsigned>(__wd.c_encoding())));
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const weekday_indexed& __wdi) {
  auto __i = __wdi.index();
  return __os << (__i >= 1 && __i <= 5
                      ? std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L}[{}]"), __wdi.weekday(), __i)
                      : std::format(__os.getloc(),
                                    _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L}[{} is not a valid index]"),
                                    __wdi.weekday(),
                                    __i));
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const weekday_last& __wdl) {
  return __os << std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L}[last]"), __wdl.weekday());
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const month_day& __md) {
  // TODO FMT The Standard allows 30th of February to be printed.
  // It would be nice to show an error message instead.
  return __os << std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L}/{}"), __md.month(), __md.day());
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const month_day_last& __mdl) {
  return __os << std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L}/last"), __mdl.month());
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const month_weekday& __mwd) {
  return __os << std::format(
             __os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L}/{:L}"), __mwd.month(), __mwd.weekday_indexed());
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const month_weekday_last& __mwdl) {
  return __os << std::format(
             __os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L}/{:L}"), __mwdl.month(), __mwdl.weekday_last());
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const year_month& __ym) {
  return __os << std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{}/{:L}"), __ym.year(), __ym.month());
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const year_month_day& __ymd) {
  return __os << (__ymd.ok() ? std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "{:%F}"), __ymd)
                             : std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "{:%F} is not a valid date"), __ymd));
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const year_month_day_last& __ymdl) {
  return __os << std::format(
             __os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{}/{:L}"), __ymdl.year(), __ymdl.month_day_last());
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const year_month_weekday& __ymwd) {
  return __os << std::format(
             __os.getloc(),
             _LIBCPP_STATICALLY_WIDEN(_CharT, "{}/{:L}/{:L}"),
             __ymwd.year(),
             __ymwd.month(),
             __ymwd.weekday_indexed());
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const year_month_weekday_last& __ymwdl) {
  return __os << std::format(
             __os.getloc(),
             _LIBCPP_STATICALLY_WIDEN(_CharT, "{}/{:L}/{:L}"),
             __ymwdl.year(),
             __ymwdl.month(),
             __ymwdl.weekday_last());
}

template <class _CharT, class _Traits, class _Duration>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const hh_mm_ss<_Duration> __hms) {
  return __os << std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L%T}"), __hms);
}

#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const sys_info& __info) {
  // __info.abbrev is always std::basic_string<char>.
  // Since these strings typically are short the conversion should be cheap.
  std::basic_string<_CharT> __abbrev{__info.abbrev.begin(), __info.abbrev.end()};
  return __os << std::format(
             _LIBCPP_STATICALLY_WIDEN(_CharT, "[{:%F %T}, {:%F %T}) {:%T} {:%Q%q} \"{}\""),
             __info.begin,
             __info.end,
             hh_mm_ss{__info.offset},
             __info.save,
             __abbrev);
}

template <class _CharT, class _Traits>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const local_info& __info) {
  auto __result = [&]() -> basic_string<_CharT> {
    switch (__info.result) {
    case local_info::unique:
      return _LIBCPP_STATICALLY_WIDEN(_CharT, "unique");
    case local_info::nonexistent:
      return _LIBCPP_STATICALLY_WIDEN(_CharT, "non-existent");
    case local_info::ambiguous:
      return _LIBCPP_STATICALLY_WIDEN(_CharT, "ambiguous");

    default:
      return std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "unspecified result ({})"), __info.result);
    };
  };

  return __os << std::format(
             _LIBCPP_STATICALLY_WIDEN(_CharT, "{}: {{{}, {}}}"), __result(), __info.first, __info.second);
}

#    if !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&                          \
        !defined(_LIBCPP_HAS_NO_LOCALIZATION)
template <class _CharT, class _Traits, class _Duration, class _TimeZonePtr>
_LIBCPP_HIDE_FROM_ABI basic_ostream<_CharT, _Traits>&
operator<<(basic_ostream<_CharT, _Traits>& __os, const zoned_time<_Duration, _TimeZonePtr>& __tp) {
  return __os << std::format(__os.getloc(), _LIBCPP_STATICALLY_WIDEN(_CharT, "{:L%F %T %Z}"), __tp);
}
#    endif
#  endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

} // namespace chrono

#endif // if _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___CHRONO_OSTREAM_H
