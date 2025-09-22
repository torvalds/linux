// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___CHRONO_FORMATTER_H
#define _LIBCPP___CHRONO_FORMATTER_H

#include <__algorithm/ranges_copy.h>
#include <__chrono/calendar.h>
#include <__chrono/concepts.h>
#include <__chrono/convert_to_tm.h>
#include <__chrono/day.h>
#include <__chrono/duration.h>
#include <__chrono/file_clock.h>
#include <__chrono/hh_mm_ss.h>
#include <__chrono/local_info.h>
#include <__chrono/month.h>
#include <__chrono/month_weekday.h>
#include <__chrono/monthday.h>
#include <__chrono/ostream.h>
#include <__chrono/parser_std_format_spec.h>
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
#include <__concepts/arithmetic.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__format/concepts.h>
#include <__format/format_error.h>
#include <__format/format_functions.h>
#include <__format/format_parse_context.h>
#include <__format/formatter.h>
#include <__format/parser_std_format_spec.h>
#include <__format/write_escaped.h>
#include <__memory/addressof.h>
#include <__type_traits/is_specialization.h>
#include <cmath>
#include <ctime>
#include <limits>
#include <sstream>
#include <string_view>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace __formatter {

/// Formats a time based on a tm struct.
///
/// This formatter passes the formatting to time_put which uses strftime. When
/// the value is outside the valid range it's unspecified what strftime will
/// output. For example weekday 8 can print 1 when the day is processed modulo
/// 7 since that handles the Sunday for 0-based weekday. It can also print 8 if
/// 7 is handled as a special case.
///
/// The Standard doesn't specify what to do in this case so the result depends
/// on the result of the underlying code.
///
/// \pre When the (abbreviated) weekday or month name are used, the caller
///      validates whether the value is valid. So the caller handles that
///      requirement of Table 97: Meaning of conversion specifiers
///      [tab:time.format.spec].
///
/// When no chrono-specs are provided it uses the stream formatter.

// For tiny ratios it's not possible to convert a duration to a hh_mm_ss. This
// fails compile-time due to the limited precision of the ratio (64-bit is too
// small). Therefore a duration uses its own conversion.
template <class _CharT, class _Rep, class _Period>
_LIBCPP_HIDE_FROM_ABI void
__format_sub_seconds(basic_stringstream<_CharT>& __sstr, const chrono::duration<_Rep, _Period>& __value) {
  __sstr << std::use_facet<numpunct<_CharT>>(__sstr.getloc()).decimal_point();

  using __duration = chrono::duration<_Rep, _Period>;

  auto __fraction = __value - chrono::duration_cast<chrono::seconds>(__value);
  // Converts a negative fraction to its positive value.
  if (__value < chrono::seconds{0} && __fraction != __duration{0})
    __fraction += chrono::seconds{1};
  if constexpr (chrono::treat_as_floating_point_v<_Rep>)
    // When the floating-point value has digits itself they are ignored based
    // on the wording in [tab:time.format.spec]
    //   If the precision of the input cannot be exactly represented with
    //   seconds, then the format is a decimal floating-point number with a
    //   fixed format and a precision matching that of the precision of the
    //   input (or to a microseconds precision if the conversion to
    //   floating-point decimal seconds cannot be made within 18 fractional
    //   digits).
    //
    // This matches the behaviour of MSVC STL, fmtlib interprets this
    // differently and uses 3 decimals.
    // https://godbolt.org/z/6dsbnW8ba
    std::format_to(std::ostreambuf_iterator<_CharT>{__sstr},
                   _LIBCPP_STATICALLY_WIDEN(_CharT, "{:0{}.0f}"),
                   chrono::duration_cast<typename chrono::hh_mm_ss<__duration>::precision>(__fraction).count(),
                   chrono::hh_mm_ss<__duration>::fractional_width);
  else
    std::format_to(std::ostreambuf_iterator<_CharT>{__sstr},
                   _LIBCPP_STATICALLY_WIDEN(_CharT, "{:0{}}"),
                   chrono::duration_cast<typename chrono::hh_mm_ss<__duration>::precision>(__fraction).count(),
                   chrono::hh_mm_ss<__duration>::fractional_width);
}

template <class _CharT, __is_time_point _Tp>
_LIBCPP_HIDE_FROM_ABI void __format_sub_seconds(basic_stringstream<_CharT>& __sstr, const _Tp& __value) {
  __formatter::__format_sub_seconds(__sstr, __value.time_since_epoch());
}

template <class _CharT, class _Duration>
_LIBCPP_HIDE_FROM_ABI void
__format_sub_seconds(basic_stringstream<_CharT>& __sstr, const chrono::hh_mm_ss<_Duration>& __value) {
  __sstr << std::use_facet<numpunct<_CharT>>(__sstr.getloc()).decimal_point();
  if constexpr (chrono::treat_as_floating_point_v<typename _Duration::rep>)
    std::format_to(std::ostreambuf_iterator<_CharT>{__sstr},
                   _LIBCPP_STATICALLY_WIDEN(_CharT, "{:0{}.0f}"),
                   __value.subseconds().count(),
                   __value.fractional_width);
  else
    std::format_to(std::ostreambuf_iterator<_CharT>{__sstr},
                   _LIBCPP_STATICALLY_WIDEN(_CharT, "{:0{}}"),
                   __value.subseconds().count(),
                   __value.fractional_width);
}

#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB) && !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) &&                     \
      !defined(_LIBCPP_HAS_NO_FILESYSTEM) && !defined(_LIBCPP_HAS_NO_LOCALIZATION)
template <class _CharT, class _Duration, class _TimeZonePtr>
_LIBCPP_HIDE_FROM_ABI void
__format_sub_seconds(basic_stringstream<_CharT>& __sstr, const chrono::zoned_time<_Duration, _TimeZonePtr>& __value) {
  __formatter::__format_sub_seconds(__sstr, __value.get_local_time().time_since_epoch());
}
#  endif

template <class _Tp>
consteval bool __use_fraction() {
  if constexpr (__is_time_point<_Tp>)
    return chrono::hh_mm_ss<typename _Tp::duration>::fractional_width;
#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB) && !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) &&                     \
      !defined(_LIBCPP_HAS_NO_FILESYSTEM) && !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  else if constexpr (__is_specialization_v<_Tp, chrono::zoned_time>)
    return chrono::hh_mm_ss<typename _Tp::duration>::fractional_width;
#  endif
  else if constexpr (chrono::__is_duration<_Tp>::value)
    return chrono::hh_mm_ss<_Tp>::fractional_width;
  else if constexpr (__is_hh_mm_ss<_Tp>)
    return _Tp::fractional_width;
  else
    return false;
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI void __format_year(basic_stringstream<_CharT>& __sstr, int __year) {
  if (__year < 0) {
    __sstr << _CharT('-');
    __year = -__year;
  }

  // TODO FMT Write an issue
  //   If the result has less than four digits it is zero-padded with 0 to two digits.
  // is less -> has less
  // left-padded -> zero-padded, otherwise the proper value would be 000-0.

  // Note according to the wording it should be left padded, which is odd.
  __sstr << std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "{:04}"), __year);
}

template <class _CharT>
_LIBCPP_HIDE_FROM_ABI void __format_century(basic_stringstream<_CharT>& __sstr, int __year) {
  // TODO FMT Write an issue
  // [tab:time.format.spec]
  //   %C The year divided by 100 using floored division. If the result is a
  //   single decimal digit, it is prefixed with 0.

  bool __negative = __year < 0;
  int __century   = (__year - (99 * __negative)) / 100; // floored division
  __sstr << std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "{:02}"), __century);
}

// Implements the %z format specifier according to [tab:time.format.spec], where
// '__modifier' signals %Oz or %Ez were used. (Both modifiers behave the same,
// so there is no need to distinguish between them.)
template <class _CharT>
_LIBCPP_HIDE_FROM_ABI void
__format_zone_offset(basic_stringstream<_CharT>& __sstr, chrono::seconds __offset, bool __modifier) {
  if (__offset < 0s) {
    __sstr << _CharT('-');
    __offset = -__offset;
  } else {
    __sstr << _CharT('+');
  }

  chrono::hh_mm_ss __hms{__offset};
  std::ostreambuf_iterator<_CharT> __out_it{__sstr};
  // Note HMS does not allow formatting hours > 23, but the offset is not limited to 24H.
  std::format_to(__out_it, _LIBCPP_STATICALLY_WIDEN(_CharT, "{:02}"), __hms.hours().count());
  if (__modifier)
    __sstr << _CharT(':');
  std::format_to(__out_it, _LIBCPP_STATICALLY_WIDEN(_CharT, "{:02}"), __hms.minutes().count());
}

// Helper to store the time zone information needed for formatting.
struct _LIBCPP_HIDE_FROM_ABI __time_zone {
  // Typically these abbreviations are short and fit in the string's internal
  // buffer.
  string __abbrev;
  chrono::seconds __offset;
};

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI __time_zone __convert_to_time_zone([[maybe_unused]] const _Tp& __value) {
#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  if constexpr (same_as<_Tp, chrono::sys_info>)
    return {__value.abbrev, __value.offset};
#    if !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&                          \
        !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  else if constexpr (__is_specialization_v<_Tp, chrono::zoned_time>)
    return __formatter::__convert_to_time_zone(__value.get_info());
#    endif
  else
#  endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
    return {"UTC", chrono::seconds{0}};
}

template <class _CharT, class _Tp>
_LIBCPP_HIDE_FROM_ABI void __format_chrono_using_chrono_specs(
    basic_stringstream<_CharT>& __sstr, const _Tp& __value, basic_string_view<_CharT> __chrono_specs) {
  tm __t              = std::__convert_to_tm<tm>(__value);
  __time_zone __z     = __formatter::__convert_to_time_zone(__value);
  const auto& __facet = std::use_facet<time_put<_CharT>>(__sstr.getloc());
  for (auto __it = __chrono_specs.begin(); __it != __chrono_specs.end(); ++__it) {
    if (*__it == _CharT('%')) {
      auto __s = __it;
      ++__it;
      // We only handle the types that can't be directly handled by time_put.
      // (as an optimization n, t, and % are also handled directly.)
      switch (*__it) {
      case _CharT('n'):
        __sstr << _CharT('\n');
        break;
      case _CharT('t'):
        __sstr << _CharT('\t');
        break;
      case _CharT('%'):
        __sstr << *__it;
        break;

      case _CharT('C'): {
        // strftime's output is only defined in the range [00, 99].
        int __year = __t.tm_year + 1900;
        if (__year < 1000 || __year > 9999)
          __formatter::__format_century(__sstr, __year);
        else
          __facet.put(
              {__sstr}, __sstr, _CharT(' '), std::addressof(__t), std::to_address(__s), std::to_address(__it + 1));
      } break;

      case _CharT('j'):
        if constexpr (chrono::__is_duration<_Tp>::value)
          // Converting a duration where the period has a small ratio to days
          // may fail to compile. This due to loss of precision in the
          // conversion. In order to avoid that issue convert to seconds as
          // an intemediate step.
          __sstr << chrono::duration_cast<chrono::days>(chrono::duration_cast<chrono::seconds>(__value)).count();
        else
          __facet.put(
              {__sstr}, __sstr, _CharT(' '), std::addressof(__t), std::to_address(__s), std::to_address(__it + 1));
        break;

      case _CharT('q'):
        if constexpr (chrono::__is_duration<_Tp>::value) {
          __sstr << chrono::__units_suffix<_CharT, typename _Tp::period>();
          break;
        }
        __builtin_unreachable();

      case _CharT('Q'):
        // TODO FMT Determine the proper ideas
        // - Should it honour the precision?
        // - Shoult it honour the locale setting for the separators?
        // The wording for Q doesn't use the word locale and the effect of
        // precision is unspecified.
        //
        // MSVC STL ignores precision but uses separator
        // FMT honours precision and has a bug for separator
        // https://godbolt.org/z/78b7sMxns
        if constexpr (chrono::__is_duration<_Tp>::value) {
          __sstr << std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "{}"), __value.count());
          break;
        }
        __builtin_unreachable();

      case _CharT('S'):
      case _CharT('T'):
        __facet.put(
            {__sstr}, __sstr, _CharT(' '), std::addressof(__t), std::to_address(__s), std::to_address(__it + 1));
        if constexpr (__use_fraction<_Tp>())
          __formatter::__format_sub_seconds(__sstr, __value);
        break;

        // Unlike time_put and strftime the formatting library requires %Y
        //
        // [tab:time.format.spec]
        //   The year as a decimal number. If the result is less than four digits
        //   it is left-padded with 0 to four digits.
        //
        // This means years in the range (-1000, 1000) need manual formatting.
        // It's unclear whether %EY needs the same treatment. For example the
        // Japanese EY contains the era name and year. This is zero-padded to 2
        // digits in time_put (note that older glibc versions didn't do
        // padding.) However most eras won't reach 100 years, let alone 1000.
        // So padding to 4 digits seems unwanted for Japanese.
        //
        // The same applies to %Ex since that too depends on the era.
        //
        // %x the locale's date representation is currently doesn't handle the
        // zero-padding too.
        //
        // The 4 digits can be implemented better at a later time. On POSIX
        // systems the required information can be extracted by nl_langinfo
        // https://man7.org/linux/man-pages/man3/nl_langinfo.3.html
        //
        // Note since year < -1000 is expected to be rare it uses the more
        // expensive year routine.
        //
        // TODO FMT evaluate the comment above.

#  if defined(__GLIBC__) || defined(_AIX) || defined(_WIN32)
      case _CharT('y'):
        // Glibc fails for negative values, AIX for positive values too.
        __sstr << std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "{:02}"), (std::abs(__t.tm_year + 1900)) % 100);
        break;
#  endif // defined(__GLIBC__) || defined(_AIX) || defined(_WIN32)

      case _CharT('Y'):
        // Depending on the platform's libc the range of supported years is
        // limited. Intead of of testing all conditions use the internal
        // implementation unconditionally.
        __formatter::__format_year(__sstr, __t.tm_year + 1900);
        break;

      case _CharT('F'):
        // Depending on the platform's libc the range of supported years is
        // limited. Instead of testing all conditions use the internal
        // implementation unconditionally.
        __formatter::__format_year(__sstr, __t.tm_year + 1900);
        __sstr << std::format(_LIBCPP_STATICALLY_WIDEN(_CharT, "-{:02}-{:02}"), __t.tm_mon + 1, __t.tm_mday);
        break;

      case _CharT('z'):
        __formatter::__format_zone_offset(__sstr, __z.__offset, false);
        break;

      case _CharT('Z'):
        // __abbrev is always a char so the copy may convert.
        ranges::copy(__z.__abbrev, std::ostreambuf_iterator<_CharT>{__sstr});
        break;

      case _CharT('O'):
        if constexpr (__use_fraction<_Tp>()) {
          // Handle OS using the normal representation for the non-fractional
          // part. There seems to be no locale information regarding how the
          // fractional part should be formatted.
          if (*(__it + 1) == 'S') {
            ++__it;
            __facet.put(
                {__sstr}, __sstr, _CharT(' '), std::addressof(__t), std::to_address(__s), std::to_address(__it + 1));
            __formatter::__format_sub_seconds(__sstr, __value);
            break;
          }
        }

        // Oz produces the same output as Ez below.
        [[fallthrough]];
      case _CharT('E'):
        ++__it;
        if (*__it == 'z') {
          __formatter::__format_zone_offset(__sstr, __z.__offset, true);
          break;
        }
        [[fallthrough]];
      default:
        __facet.put(
            {__sstr}, __sstr, _CharT(' '), std::addressof(__t), std::to_address(__s), std::to_address(__it + 1));
        break;
      }
    } else {
      __sstr << *__it;
    }
  }
}

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr bool __weekday_ok(const _Tp& __value) {
  if constexpr (__is_time_point<_Tp>)
    return true;
  else if constexpr (same_as<_Tp, chrono::day>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year>)
    return true;
  else if constexpr (same_as<_Tp, chrono::weekday>)
    return true;
  else if constexpr (same_as<_Tp, chrono::weekday_indexed>)
    return true;
  else if constexpr (same_as<_Tp, chrono::weekday_last>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_day>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_day_last>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_weekday>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_weekday_last>)
    return true;
  else if constexpr (same_as<_Tp, chrono::year_month>)
    return true;
  else if constexpr (same_as<_Tp, chrono::year_month_day>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year_month_day_last>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year_month_weekday>)
    return __value.weekday().ok();
  else if constexpr (same_as<_Tp, chrono::year_month_weekday_last>)
    return __value.weekday().ok();
  else if constexpr (__is_hh_mm_ss<_Tp>)
    return true;
#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  else if constexpr (same_as<_Tp, chrono::sys_info>)
    return true;
  else if constexpr (same_as<_Tp, chrono::local_info>)
    return true;
#    if !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&                          \
        !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  else if constexpr (__is_specialization_v<_Tp, chrono::zoned_time>)
    return true;
#    endif
#  endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  else
    static_assert(sizeof(_Tp) == 0, "Add the missing type specialization");
}

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr bool __weekday_name_ok(const _Tp& __value) {
  if constexpr (__is_time_point<_Tp>)
    return true;
  else if constexpr (same_as<_Tp, chrono::day>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year>)
    return true;
  else if constexpr (same_as<_Tp, chrono::weekday>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::weekday_indexed>)
    return __value.weekday().ok();
  else if constexpr (same_as<_Tp, chrono::weekday_last>)
    return __value.weekday().ok();
  else if constexpr (same_as<_Tp, chrono::month_day>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_day_last>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_weekday>)
    return __value.weekday_indexed().ok();
  else if constexpr (same_as<_Tp, chrono::month_weekday_last>)
    return __value.weekday_indexed().ok();
  else if constexpr (same_as<_Tp, chrono::year_month>)
    return true;
  else if constexpr (same_as<_Tp, chrono::year_month_day>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year_month_day_last>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year_month_weekday>)
    return __value.weekday().ok();
  else if constexpr (same_as<_Tp, chrono::year_month_weekday_last>)
    return __value.weekday().ok();
  else if constexpr (__is_hh_mm_ss<_Tp>)
    return true;
#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  else if constexpr (same_as<_Tp, chrono::sys_info>)
    return true;
  else if constexpr (same_as<_Tp, chrono::local_info>)
    return true;
#    if !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&                          \
        !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  else if constexpr (__is_specialization_v<_Tp, chrono::zoned_time>)
    return true;
#    endif
#  endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  else
    static_assert(sizeof(_Tp) == 0, "Add the missing type specialization");
}

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr bool __date_ok(const _Tp& __value) {
  if constexpr (__is_time_point<_Tp>)
    return true;
  else if constexpr (same_as<_Tp, chrono::day>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year>)
    return true;
  else if constexpr (same_as<_Tp, chrono::weekday>)
    return true;
  else if constexpr (same_as<_Tp, chrono::weekday_indexed>)
    return true;
  else if constexpr (same_as<_Tp, chrono::weekday_last>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_day>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_day_last>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_weekday>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_weekday_last>)
    return true;
  else if constexpr (same_as<_Tp, chrono::year_month>)
    return true;
  else if constexpr (same_as<_Tp, chrono::year_month_day>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year_month_day_last>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year_month_weekday>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year_month_weekday_last>)
    return __value.ok();
  else if constexpr (__is_hh_mm_ss<_Tp>)
    return true;
#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  else if constexpr (same_as<_Tp, chrono::sys_info>)
    return true;
  else if constexpr (same_as<_Tp, chrono::local_info>)
    return true;
#    if !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&                          \
        !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  else if constexpr (__is_specialization_v<_Tp, chrono::zoned_time>)
    return true;
#    endif
#  endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  else
    static_assert(sizeof(_Tp) == 0, "Add the missing type specialization");
}

template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr bool __month_name_ok(const _Tp& __value) {
  if constexpr (__is_time_point<_Tp>)
    return true;
  else if constexpr (same_as<_Tp, chrono::day>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month>)
    return __value.ok();
  else if constexpr (same_as<_Tp, chrono::year>)
    return true;
  else if constexpr (same_as<_Tp, chrono::weekday>)
    return true;
  else if constexpr (same_as<_Tp, chrono::weekday_indexed>)
    return true;
  else if constexpr (same_as<_Tp, chrono::weekday_last>)
    return true;
  else if constexpr (same_as<_Tp, chrono::month_day>)
    return __value.month().ok();
  else if constexpr (same_as<_Tp, chrono::month_day_last>)
    return __value.month().ok();
  else if constexpr (same_as<_Tp, chrono::month_weekday>)
    return __value.month().ok();
  else if constexpr (same_as<_Tp, chrono::month_weekday_last>)
    return __value.month().ok();
  else if constexpr (same_as<_Tp, chrono::year_month>)
    return __value.month().ok();
  else if constexpr (same_as<_Tp, chrono::year_month_day>)
    return __value.month().ok();
  else if constexpr (same_as<_Tp, chrono::year_month_day_last>)
    return __value.month().ok();
  else if constexpr (same_as<_Tp, chrono::year_month_weekday>)
    return __value.month().ok();
  else if constexpr (same_as<_Tp, chrono::year_month_weekday_last>)
    return __value.month().ok();
  else if constexpr (__is_hh_mm_ss<_Tp>)
    return true;
#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  else if constexpr (same_as<_Tp, chrono::sys_info>)
    return true;
  else if constexpr (same_as<_Tp, chrono::local_info>)
    return true;
#    if !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&                          \
        !defined(_LIBCPP_HAS_NO_LOCALIZATION)
  else if constexpr (__is_specialization_v<_Tp, chrono::zoned_time>)
    return true;
#    endif
#  endif // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
  else
    static_assert(sizeof(_Tp) == 0, "Add the missing type specialization");
}

template <class _CharT, class _Tp, class _FormatContext>
_LIBCPP_HIDE_FROM_ABI auto
__format_chrono(const _Tp& __value,
                _FormatContext& __ctx,
                __format_spec::__parsed_specifications<_CharT> __specs,
                basic_string_view<_CharT> __chrono_specs) {
  basic_stringstream<_CharT> __sstr;
  // [time.format]/2
  // 2.1 - the "C" locale if the L option is not present in chrono-format-spec, otherwise
  // 2.2 - the locale passed to the formatting function if any, otherwise
  // 2.3 - the global locale.
  // Note that the __ctx's locale() call does 2.2 and 2.3.
  if (__specs.__chrono_.__locale_specific_form_)
    __sstr.imbue(__ctx.locale());
  else
    __sstr.imbue(locale::classic());

  if (__chrono_specs.empty())
    __sstr << __value;
  else {
    if constexpr (chrono::__is_duration<_Tp>::value) {
      // A duration can be a user defined arithmetic type. Users may specialize
      // numeric_limits, but they may not specialize is_signed.
      if constexpr (numeric_limits<typename _Tp::rep>::is_signed) {
        if (__value < __value.zero()) {
          __sstr << _CharT('-');
          __formatter::__format_chrono_using_chrono_specs(__sstr, -__value, __chrono_specs);
        } else
          __formatter::__format_chrono_using_chrono_specs(__sstr, __value, __chrono_specs);
      } else
        __formatter::__format_chrono_using_chrono_specs(__sstr, __value, __chrono_specs);
      // TODO FMT When keeping the precision it will truncate the string.
      // Note that the behaviour what the precision does isn't specified.
      __specs.__precision_ = -1;
    } else {
      // Test __weekday_name_ before __weekday_ to give a better error.
      if (__specs.__chrono_.__weekday_name_ && !__formatter::__weekday_name_ok(__value))
        std::__throw_format_error("Formatting a weekday name needs a valid weekday");

      if (__specs.__chrono_.__weekday_ && !__formatter::__weekday_ok(__value))
        std::__throw_format_error("Formatting a weekday needs a valid weekday");

      if (__specs.__chrono_.__day_of_year_ && !__formatter::__date_ok(__value))
        std::__throw_format_error("Formatting a day of year needs a valid date");

      if (__specs.__chrono_.__week_of_year_ && !__formatter::__date_ok(__value))
        std::__throw_format_error("Formatting a week of year needs a valid date");

      if (__specs.__chrono_.__month_name_ && !__formatter::__month_name_ok(__value))
        std::__throw_format_error("Formatting a month name from an invalid month number");

      if constexpr (__is_hh_mm_ss<_Tp>) {
        // Note this is a pedantic intepretation of the Standard. A hh_mm_ss
        // is no longer a time_of_day and can store an arbitrary number of
        // hours. A number of hours in a 12 or 24 hour clock can't represent
        // 24 hours or more. The functions std::chrono::make12 and
        // std::chrono::make24 reaffirm this view point.
        //
        // Interestingly this will be the only output stream function that
        // throws.
        //
        // TODO FMT The wording probably needs to be adapted to
        // - The displayed hours is hh_mm_ss.hours() % 24
        // - It should probably allow %j in the same fashion as duration.
        // - The stream formatter should change its output when hours >= 24
        //   - Write it as not valid,
        //   - or write the number of days.
        if (__specs.__chrono_.__hour_ && __value.hours().count() > 23)
          std::__throw_format_error("Formatting a hour needs a valid value");

        if (__value.is_negative())
          __sstr << _CharT('-');
      }

      __formatter::__format_chrono_using_chrono_specs(__sstr, __value, __chrono_specs);
    }
  }

  return __formatter::__write_string(__sstr.view(), __ctx.out(), __specs);
}

} // namespace __formatter

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS __formatter_chrono {
public:
  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator
  __parse(_ParseContext& __ctx, __format_spec::__fields __fields, __format_spec::__flags __flags) {
    return __parser_.__parse(__ctx, __fields, __flags);
  }

  template <class _Tp, class _FormatContext>
  _LIBCPP_HIDE_FROM_ABI typename _FormatContext::iterator format(const _Tp& __value, _FormatContext& __ctx) const {
    return __formatter::__format_chrono(
        __value, __ctx, __parser_.__parser_.__get_parsed_chrono_specifications(__ctx), __parser_.__chrono_specs_);
  }

  __format_spec::__parser_chrono<_CharT> __parser_;
};

template <class _Duration, __fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::sys_time<_Duration>, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__clock);
  }
};

template <class _Duration, __fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::file_time<_Duration>, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__clock);
  }
};

template <class _Duration, __fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::local_time<_Duration>, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    // The flags are not __clock since there is no associated time-zone.
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__date_time);
  }
};

template <class _Rep, class _Period, __fmt_char_type _CharT>
struct formatter<chrono::duration<_Rep, _Period>, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    // [time.format]/1
    // Giving a precision specification in the chrono-format-spec is valid only
    // for std::chrono::duration types where the representation type Rep is a
    // floating-point type. For all other Rep types, an exception of type
    // format_error is thrown if the chrono-format-spec contains a precision
    // specification.
    //
    // Note this doesn't refer to chrono::treat_as_floating_point_v<_Rep>.
    if constexpr (std::floating_point<_Rep>)
      return _Base::__parse(__ctx, __format_spec::__fields_chrono_fractional, __format_spec::__flags::__duration);
    else
      return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__duration);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::day, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__day);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::month, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__month);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::year, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__year);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::weekday, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__weekday);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::weekday_indexed, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__weekday);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::weekday_last, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__weekday);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::month_day, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__month_day);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::month_day_last, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__month);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::month_weekday, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__month_weekday);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::month_weekday_last, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__month_weekday);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::year_month, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__year_month);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::year_month_day, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__date);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::year_month_day_last, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__date);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::year_month_weekday, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__date);
  }
};

template <__fmt_char_type _CharT>
struct _LIBCPP_TEMPLATE_VIS formatter<chrono::year_month_weekday_last, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__date);
  }
};

template <class _Duration, __fmt_char_type _CharT>
struct formatter<chrono::hh_mm_ss<_Duration>, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__time);
  }
};

#  if !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)
template <__fmt_char_type _CharT>
struct formatter<chrono::sys_info, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__time_zone);
  }
};

template <__fmt_char_type _CharT>
struct formatter<chrono::local_info, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags{});
  }
};
#    if !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&                          \
        !defined(_LIBCPP_HAS_NO_LOCALIZATION)
// Note due to how libc++'s formatters are implemented there is no need to add
// the exposition only local-time-format-t abstraction.
template <class _Duration, class _TimeZonePtr, __fmt_char_type _CharT>
struct formatter<chrono::zoned_time<_Duration, _TimeZonePtr>, _CharT> : public __formatter_chrono<_CharT> {
public:
  using _Base = __formatter_chrono<_CharT>;

  template <class _ParseContext>
  _LIBCPP_HIDE_FROM_ABI constexpr typename _ParseContext::iterator parse(_ParseContext& __ctx) {
    return _Base::__parse(__ctx, __format_spec::__fields_chrono, __format_spec::__flags::__clock);
  }
};
#    endif // !defined(_LIBCPP_HAS_NO_TIME_ZONE_DATABASE) && !defined(_LIBCPP_HAS_NO_FILESYSTEM) &&
           // !defined(_LIBCPP_HAS_NO_LOCALIZATION)
#  endif   // !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_TZDB)

#endif // if _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

#endif //  _LIBCPP___CHRONO_FORMATTER_H
