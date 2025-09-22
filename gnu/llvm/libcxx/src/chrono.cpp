//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__MVS__)
// As part of monotonic clock support on z/OS we need macro _LARGE_TIME_API
// to be defined before any system header to include definition of struct timespec64.
#  define _LARGE_TIME_API
#endif

#include <__system_error/system_error.h>
#include <cerrno> // errno
#include <chrono>

#if defined(__MVS__)
#  include <__support/ibm/gettod_zos.h> // gettimeofdayMonotonic
#endif

#include "include/apple_availability.h"
#include <time.h> // clock_gettime and CLOCK_{MONOTONIC,REALTIME,MONOTONIC_RAW}

#if __has_include(<unistd.h>)
#  include <unistd.h> // _POSIX_TIMERS
#endif

#if __has_include(<sys/time.h>)
#  include <sys/time.h> // for gettimeofday and timeval
#endif

// OpenBSD does not have a fully conformant suite of POSIX timers, but
// it does have clock_gettime and CLOCK_MONOTONIC which is all we need.
#if defined(__APPLE__) || defined(__gnu_hurd__) || defined(__OpenBSD__) || (defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0)
#  define _LIBCPP_HAS_CLOCK_GETTIME
#endif

#if defined(_LIBCPP_WIN32API)
#  define WIN32_LEAN_AND_MEAN
#  define VC_EXTRA_LEAN
#  include <windows.h>
#  if _WIN32_WINNT >= _WIN32_WINNT_WIN8
#    include <winapifamily.h>
#  endif
#endif // defined(_LIBCPP_WIN32API)

#if defined(__Fuchsia__)
#  include <zircon/syscalls.h>
#endif

#if __has_include(<mach/mach_time.h>)
#  include <mach/mach_time.h>
#endif

#if defined(__ELF__) && defined(_LIBCPP_LINK_RT_LIB)
#  pragma comment(lib, "rt")
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

namespace chrono {

//
// system_clock
//

#if defined(_LIBCPP_WIN32API)

#  if _WIN32_WINNT < _WIN32_WINNT_WIN8

namespace {

typedef void(WINAPI* GetSystemTimeAsFileTimePtr)(LPFILETIME);

class GetSystemTimeInit {
public:
  GetSystemTimeInit() {
    fp = (GetSystemTimeAsFileTimePtr)(void*)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "GetSystemTimePreciseAsFileTime");
    if (fp == nullptr)
      fp = GetSystemTimeAsFileTime;
  }
  GetSystemTimeAsFileTimePtr fp;
};

// Pretend we're inside a system header so the compiler doesn't flag the use of the init_priority
// attribute with a value that's reserved for the implementation (we're the implementation).
#    include "chrono_system_time_init.h"
} // namespace

#  endif

static system_clock::time_point __libcpp_system_clock_now() {
  // FILETIME is in 100ns units
  using filetime_duration =
      std::chrono::duration<__int64, std::ratio_multiply<std::ratio<100, 1>, nanoseconds::period>>;

  // The Windows epoch is Jan 1 1601, the Unix epoch Jan 1 1970.
  static constexpr const seconds nt_to_unix_epoch{11644473600};

  FILETIME ft;
#  if (_WIN32_WINNT >= _WIN32_WINNT_WIN8 && WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)) ||                      \
      (_WIN32_WINNT >= _WIN32_WINNT_WIN10)
  GetSystemTimePreciseAsFileTime(&ft);
#  elif !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
  GetSystemTimeAsFileTime(&ft);
#  else
  GetSystemTimeAsFileTimeFunc.fp(&ft);
#  endif

  filetime_duration d{(static_cast<__int64>(ft.dwHighDateTime) << 32) | static_cast<__int64>(ft.dwLowDateTime)};
  return system_clock::time_point(duration_cast<system_clock::duration>(d - nt_to_unix_epoch));
}

#elif defined(_LIBCPP_HAS_CLOCK_GETTIME)

static system_clock::time_point __libcpp_system_clock_now() {
  struct timespec tp;
  if (0 != clock_gettime(CLOCK_REALTIME, &tp))
    __throw_system_error(errno, "clock_gettime(CLOCK_REALTIME) failed");
  return system_clock::time_point(seconds(tp.tv_sec) + microseconds(tp.tv_nsec / 1000));
}

#else

static system_clock::time_point __libcpp_system_clock_now() {
  timeval tv;
  gettimeofday(&tv, 0);
  return system_clock::time_point(seconds(tv.tv_sec) + microseconds(tv.tv_usec));
}

#endif

const bool system_clock::is_steady;

system_clock::time_point system_clock::now() noexcept { return __libcpp_system_clock_now(); }

time_t system_clock::to_time_t(const time_point& t) noexcept {
  return time_t(duration_cast<seconds>(t.time_since_epoch()).count());
}

system_clock::time_point system_clock::from_time_t(time_t t) noexcept { return system_clock::time_point(seconds(t)); }

//
// steady_clock
//
// Warning:  If this is not truly steady, then it is non-conforming.  It is
//  better for it to not exist and have the rest of libc++ use system_clock
//  instead.
//

#ifndef _LIBCPP_HAS_NO_MONOTONIC_CLOCK

#  if defined(__APPLE__)

// On Apple platforms, only CLOCK_UPTIME_RAW, CLOCK_MONOTONIC_RAW or
// mach_absolute_time are able to time functions in the nanosecond range.
// Furthermore, only CLOCK_MONOTONIC_RAW is truly monotonic, because it
// also counts cycles when the system is asleep. Thus, it is the only
// acceptable implementation of steady_clock.
static steady_clock::time_point __libcpp_steady_clock_now() {
  struct timespec tp;
  if (0 != clock_gettime(CLOCK_MONOTONIC_RAW, &tp))
    __throw_system_error(errno, "clock_gettime(CLOCK_MONOTONIC_RAW) failed");
  return steady_clock::time_point(seconds(tp.tv_sec) + nanoseconds(tp.tv_nsec));
}

#  elif defined(_LIBCPP_WIN32API)

// https://msdn.microsoft.com/en-us/library/windows/desktop/ms644905(v=vs.85).aspx says:
//    If the function fails, the return value is zero. <snip>
//    On systems that run Windows XP or later, the function will always succeed
//      and will thus never return zero.

static LARGE_INTEGER __QueryPerformanceFrequency() {
  LARGE_INTEGER val;
  (void)QueryPerformanceFrequency(&val);
  return val;
}

static steady_clock::time_point __libcpp_steady_clock_now() {
  static const LARGE_INTEGER freq = __QueryPerformanceFrequency();

  LARGE_INTEGER counter;
  (void)QueryPerformanceCounter(&counter);
  auto seconds   = counter.QuadPart / freq.QuadPart;
  auto fractions = counter.QuadPart % freq.QuadPart;
  auto dur       = seconds * nano::den + fractions * nano::den / freq.QuadPart;
  return steady_clock::time_point(steady_clock::duration(dur));
}

#  elif defined(__MVS__)

static steady_clock::time_point __libcpp_steady_clock_now() {
  struct timespec64 ts;
  if (0 != gettimeofdayMonotonic(&ts))
    __throw_system_error(errno, "failed to obtain time of day");

  return steady_clock::time_point(seconds(ts.tv_sec) + nanoseconds(ts.tv_nsec));
}

#  elif defined(__Fuchsia__)

static steady_clock::time_point __libcpp_steady_clock_now() noexcept {
  // Implicitly link against the vDSO system call ABI without
  // requiring the final link to specify -lzircon explicitly when
  // statically linking libc++.
#    pragma comment(lib, "zircon")

  return steady_clock::time_point(nanoseconds(_zx_clock_get_monotonic()));
}

#  elif defined(_LIBCPP_HAS_CLOCK_GETTIME)

static steady_clock::time_point __libcpp_steady_clock_now() {
  struct timespec tp;
  if (0 != clock_gettime(CLOCK_MONOTONIC, &tp))
    __throw_system_error(errno, "clock_gettime(CLOCK_MONOTONIC) failed");
  return steady_clock::time_point(seconds(tp.tv_sec) + nanoseconds(tp.tv_nsec));
}

#  else
#    error "Monotonic clock not implemented on this platform"
#  endif

const bool steady_clock::is_steady;

steady_clock::time_point steady_clock::now() noexcept { return __libcpp_steady_clock_now(); }

#endif // !_LIBCPP_HAS_NO_MONOTONIC_CLOCK

} // namespace chrono

_LIBCPP_END_NAMESPACE_STD
