//===----------------------------------------------------------------------===////
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===////

#ifndef FILESYSTEM_TIME_UTILS_H
#define FILESYSTEM_TIME_UTILS_H

#include <__config>
#include <array>
#include <chrono>
#include <filesystem>
#include <limits>
#include <ratio>
#include <system_error>
#include <type_traits>
#include <utility>

#include "error.h"
#include "format_string.h"

#if defined(_LIBCPP_WIN32API)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/time.h> // for ::utimes as used in __last_write_time
#endif

// We can use the presence of UTIME_OMIT to detect platforms that provide utimensat.
#if defined(UTIME_OMIT)
#  define _LIBCPP_USE_UTIMENSAT
#endif

_LIBCPP_BEGIN_NAMESPACE_FILESYSTEM

namespace detail {

#if defined(_LIBCPP_WIN32API)
// Various C runtime versions (UCRT, or the legacy msvcrt.dll used by
// some mingw toolchains) provide different stat function implementations,
// with a number of limitations with respect to what we want from the
// stat function. Instead provide our own which does exactly what we want,
// along with our own stat structure and flag macros.

struct TimeSpec {
  int64_t tv_sec;
  int64_t tv_nsec;
};
struct StatT {
  unsigned st_mode;
  TimeSpec st_atim;
  TimeSpec st_mtim;
  uint64_t st_dev; // FILE_ID_INFO::VolumeSerialNumber
  struct FileIdStruct {
    unsigned char id[16]; // FILE_ID_INFO::FileId
    bool operator==(const FileIdStruct& other) const {
      for (int i = 0; i < 16; i++)
        if (id[i] != other.id[i])
          return false;
      return true;
    }
  } st_ino;
  uint32_t st_nlink;
  uintmax_t st_size;
};

// There were 369 years and 89 leap days from the Windows epoch
// (1601) to the Unix epoch (1970).
#  define FILE_TIME_OFFSET_SECS (uint64_t(369 * 365 + 89) * (24 * 60 * 60))

inline TimeSpec filetime_to_timespec(LARGE_INTEGER li) {
  TimeSpec ret;
  ret.tv_sec  = li.QuadPart / 10000000 - FILE_TIME_OFFSET_SECS;
  ret.tv_nsec = (li.QuadPart % 10000000) * 100;
  return ret;
}

inline TimeSpec filetime_to_timespec(FILETIME ft) {
  LARGE_INTEGER li;
  li.LowPart  = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  return filetime_to_timespec(li);
}

inline FILETIME timespec_to_filetime(TimeSpec ts) {
  LARGE_INTEGER li;
  li.QuadPart = ts.tv_nsec / 100 + (ts.tv_sec + FILE_TIME_OFFSET_SECS) * 10000000;
  FILETIME ft;
  ft.dwLowDateTime  = li.LowPart;
  ft.dwHighDateTime = li.HighPart;
  return ft;
}

#else
using TimeSpec = struct timespec;
using TimeVal  = struct timeval;
using StatT    = struct stat;

inline TimeVal make_timeval(TimeSpec const& ts) {
  using namespace chrono;
  auto Convert = [](long nsec) {
    using int_type = decltype(std::declval<TimeVal>().tv_usec);
    auto dur       = duration_cast<microseconds>(nanoseconds(nsec)).count();
    return static_cast<int_type>(dur);
  };
  TimeVal TV = {};
  TV.tv_sec  = ts.tv_sec;
  TV.tv_usec = Convert(ts.tv_nsec);
  return TV;
}
#endif

using chrono::duration;
using chrono::duration_cast;

template <class FileTimeT, class TimeT, bool IsFloat = is_floating_point<typename FileTimeT::rep>::value>
struct time_util_base {
  using rep             = typename FileTimeT::rep;
  using fs_duration     = typename FileTimeT::duration;
  using fs_seconds      = duration<rep>;
  using fs_nanoseconds  = duration<rep, nano>;
  using fs_microseconds = duration<rep, micro>;

  static constexpr rep max_seconds = duration_cast<fs_seconds>(FileTimeT::duration::max()).count();

  static constexpr rep max_nsec =
      duration_cast<fs_nanoseconds>(FileTimeT::duration::max() - fs_seconds(max_seconds)).count();

  static constexpr rep min_seconds = duration_cast<fs_seconds>(FileTimeT::duration::min()).count();

  static constexpr rep min_nsec_timespec =
      duration_cast<fs_nanoseconds>((FileTimeT::duration::min() - fs_seconds(min_seconds)) + fs_seconds(1)).count();

private:
  static constexpr fs_duration get_min_nsecs() {
    return duration_cast<fs_duration>(fs_nanoseconds(min_nsec_timespec) - duration_cast<fs_nanoseconds>(fs_seconds(1)));
  }
  // Static assert that these values properly round trip.
  static_assert(fs_seconds(min_seconds) + get_min_nsecs() == FileTimeT::duration::min(), "value doesn't roundtrip");

  static constexpr bool check_range() {
    // This kinda sucks, but it's what happens when we don't have __int128_t.
    if (sizeof(TimeT) == sizeof(rep)) {
      typedef duration<long long, ratio<3600 * 24 * 365> > Years;
      return duration_cast<Years>(fs_seconds(max_seconds)) > Years(250) &&
             duration_cast<Years>(fs_seconds(min_seconds)) < Years(-250);
    }
    return max_seconds >= numeric_limits<TimeT>::max() && min_seconds <= numeric_limits<TimeT>::min();
  }
#if _LIBCPP_STD_VER >= 14
  static_assert(check_range(), "the representable range is unacceptable small");
#endif
};

template <class FileTimeT, class TimeT>
struct time_util_base<FileTimeT, TimeT, true> {
  using rep             = typename FileTimeT::rep;
  using fs_duration     = typename FileTimeT::duration;
  using fs_seconds      = duration<rep>;
  using fs_nanoseconds  = duration<rep, nano>;
  using fs_microseconds = duration<rep, micro>;

  static const rep max_seconds;
  static const rep max_nsec;
  static const rep min_seconds;
  static const rep min_nsec_timespec;
};

template <class FileTimeT, class TimeT>
const typename FileTimeT::rep time_util_base<FileTimeT, TimeT, true>::max_seconds =
    duration_cast<fs_seconds>(FileTimeT::duration::max()).count();

template <class FileTimeT, class TimeT>
const typename FileTimeT::rep time_util_base<FileTimeT, TimeT, true>::max_nsec =
    duration_cast<fs_nanoseconds>(FileTimeT::duration::max() - fs_seconds(max_seconds)).count();

template <class FileTimeT, class TimeT>
const typename FileTimeT::rep time_util_base<FileTimeT, TimeT, true>::min_seconds =
    duration_cast<fs_seconds>(FileTimeT::duration::min()).count();

template <class FileTimeT, class TimeT>
const typename FileTimeT::rep time_util_base<FileTimeT, TimeT, true>::min_nsec_timespec =
    duration_cast<fs_nanoseconds>((FileTimeT::duration::min() - fs_seconds(min_seconds)) + fs_seconds(1)).count();

template <class FileTimeT, class TimeT, class TimeSpecT>
struct time_util : time_util_base<FileTimeT, TimeT> {
  using Base = time_util_base<FileTimeT, TimeT>;
  using Base::max_nsec;
  using Base::max_seconds;
  using Base::min_nsec_timespec;
  using Base::min_seconds;

  using typename Base::fs_duration;
  using typename Base::fs_microseconds;
  using typename Base::fs_nanoseconds;
  using typename Base::fs_seconds;

public:
  template <class CType, class ChronoType>
  static constexpr bool checked_set(CType* out, ChronoType time) {
    using Lim = numeric_limits<CType>;
    if (time > Lim::max() || time < Lim::min())
      return false;
    *out = static_cast<CType>(time);
    return true;
  }

  static constexpr bool is_representable(TimeSpecT tm) {
    if (tm.tv_sec >= 0) {
      return tm.tv_sec < max_seconds || (tm.tv_sec == max_seconds && tm.tv_nsec <= max_nsec);
    } else if (tm.tv_sec == (min_seconds - 1)) {
      return tm.tv_nsec >= min_nsec_timespec;
    } else {
      return tm.tv_sec >= min_seconds;
    }
  }

  static constexpr bool is_representable(FileTimeT tm) {
    auto secs  = duration_cast<fs_seconds>(tm.time_since_epoch());
    auto nsecs = duration_cast<fs_nanoseconds>(tm.time_since_epoch() - secs);
    if (nsecs.count() < 0) {
      secs  = secs + fs_seconds(1);
      nsecs = nsecs + fs_seconds(1);
    }
    using TLim = numeric_limits<TimeT>;
    if (secs.count() >= 0)
      return secs.count() <= TLim::max();
    return secs.count() >= TLim::min();
  }

  static constexpr FileTimeT convert_from_timespec(TimeSpecT tm) {
    if (tm.tv_sec >= 0 || tm.tv_nsec == 0) {
      return FileTimeT(fs_seconds(tm.tv_sec) + duration_cast<fs_duration>(fs_nanoseconds(tm.tv_nsec)));
    } else { // tm.tv_sec < 0
      auto adj_subsec = duration_cast<fs_duration>(fs_seconds(1) - fs_nanoseconds(tm.tv_nsec));
      auto Dur        = fs_seconds(tm.tv_sec + 1) - adj_subsec;
      return FileTimeT(Dur);
    }
  }

  template <class SubSecT>
  static constexpr bool set_times_checked(TimeT* sec_out, SubSecT* subsec_out, FileTimeT tp) {
    auto dur        = tp.time_since_epoch();
    auto sec_dur    = duration_cast<fs_seconds>(dur);
    auto subsec_dur = duration_cast<fs_nanoseconds>(dur - sec_dur);
    // The tv_nsec and tv_usec fields must not be negative so adjust accordingly
    if (subsec_dur.count() < 0) {
      if (sec_dur.count() > min_seconds) {
        sec_dur    = sec_dur - fs_seconds(1);
        subsec_dur = subsec_dur + fs_seconds(1);
      } else {
        subsec_dur = fs_nanoseconds::zero();
      }
    }
    return checked_set(sec_out, sec_dur.count()) && checked_set(subsec_out, subsec_dur.count());
  }
  static constexpr bool convert_to_timespec(TimeSpecT& dest, FileTimeT tp) {
    if (!is_representable(tp))
      return false;
    return set_times_checked(&dest.tv_sec, &dest.tv_nsec, tp);
  }
};

#if defined(_LIBCPP_WIN32API)
using fs_time = time_util<file_time_type, int64_t, TimeSpec>;
#else
using fs_time = time_util<file_time_type, time_t, TimeSpec>;
#endif

#if defined(__APPLE__)
inline TimeSpec extract_mtime(StatT const& st) { return st.st_mtimespec; }
inline TimeSpec extract_atime(StatT const& st) { return st.st_atimespec; }
#elif defined(__MVS__)
inline TimeSpec extract_mtime(StatT const& st) {
  TimeSpec TS = {st.st_mtime, 0};
  return TS;
}
inline TimeSpec extract_atime(StatT const& st) {
  TimeSpec TS = {st.st_atime, 0};
  return TS;
}
#elif defined(_AIX)
inline TimeSpec extract_mtime(StatT const& st) {
  TimeSpec TS = {st.st_mtime, st.st_mtime_n};
  return TS;
}
inline TimeSpec extract_atime(StatT const& st) {
  TimeSpec TS = {st.st_atime, st.st_atime_n};
  return TS;
}
#else
inline TimeSpec extract_mtime(StatT const& st) { return st.st_mtim; }
inline TimeSpec extract_atime(StatT const& st) { return st.st_atim; }
#endif

#ifndef _LIBCPP_HAS_NO_FILESYSTEM

#  if !defined(_LIBCPP_WIN32API)
inline bool posix_utimes(const path& p, std::array<TimeSpec, 2> const& TS, error_code& ec) {
  TimeVal ConvertedTS[2] = {make_timeval(TS[0]), make_timeval(TS[1])};
  if (::utimes(p.c_str(), ConvertedTS) == -1) {
    ec = capture_errno();
    return true;
  }
  return false;
}

#    if defined(_LIBCPP_USE_UTIMENSAT)
inline bool posix_utimensat(const path& p, std::array<TimeSpec, 2> const& TS, error_code& ec) {
  if (::utimensat(AT_FDCWD, p.c_str(), TS.data(), 0) == -1) {
    ec = capture_errno();
    return true;
  }
  return false;
}
#    endif

inline bool set_file_times(const path& p, std::array<TimeSpec, 2> const& TS, error_code& ec) {
#    if !defined(_LIBCPP_USE_UTIMENSAT)
  return posix_utimes(p, TS, ec);
#    else
  return posix_utimensat(p, TS, ec);
#    endif
}

#  endif // !_LIBCPP_WIN32API

inline file_time_type __extract_last_write_time(const path& p, const StatT& st, error_code* ec) {
  using detail::fs_time;
  ErrorHandler<file_time_type> err("last_write_time", ec, &p);

  auto ts = detail::extract_mtime(st);
  if (!fs_time::is_representable(ts))
    return err.report(errc::value_too_large);

  return fs_time::convert_from_timespec(ts);
}

#endif // !_LIBCPP_HAS_NO_FILESYSTEM

} // end namespace detail

_LIBCPP_END_NAMESPACE_FILESYSTEM

#endif // FILESYSTEM_TIME_UTILS_H
