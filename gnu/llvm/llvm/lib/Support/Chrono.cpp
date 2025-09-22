//===- Support/Chrono.cpp - Utilities for Timing Manipulation ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Chrono.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

using namespace sys;

const char llvm::detail::unit<std::ratio<3600>>::value[] = "h";
const char llvm::detail::unit<std::ratio<60>>::value[] = "m";
const char llvm::detail::unit<std::ratio<1>>::value[] = "s";
const char llvm::detail::unit<std::milli>::value[] = "ms";
const char llvm::detail::unit<std::micro>::value[] = "us";
const char llvm::detail::unit<std::nano>::value[] = "ns";

static inline struct tm getStructTM(TimePoint<> TP) {
  struct tm Storage;
  std::time_t OurTime = toTimeT(TP);

#if defined(LLVM_ON_UNIX)
  struct tm *LT = ::localtime_r(&OurTime, &Storage);
  assert(LT);
  (void)LT;
#endif
#if defined(_WIN32)
  int Error = ::localtime_s(&Storage, &OurTime);
  assert(!Error);
  (void)Error;
#endif

  return Storage;
}

static inline struct tm getStructTMUtc(UtcTime<> TP) {
  struct tm Storage;
  std::time_t OurTime = toTimeT(TP);

#if defined(LLVM_ON_UNIX)
  struct tm *LT = ::gmtime_r(&OurTime, &Storage);
  assert(LT);
  (void)LT;
#endif
#if defined(_WIN32)
  int Error = ::gmtime_s(&Storage, &OurTime);
  assert(!Error);
  (void)Error;
#endif

  return Storage;
}

raw_ostream &operator<<(raw_ostream &OS, TimePoint<> TP) {
  struct tm LT = getStructTM(TP);
  char Buffer[sizeof("YYYY-MM-DD HH:MM:SS")];
  strftime(Buffer, sizeof(Buffer), "%Y-%m-%d %H:%M:%S", &LT);
  return OS << Buffer << '.'
            << format("%.9lu",
                      long((TP.time_since_epoch() % std::chrono::seconds(1))
                               .count()));
}

template <class T>
static void format(const T &Fractional, struct tm &LT, raw_ostream &OS,
                   StringRef Style) {
  using namespace std::chrono;
  // Handle extensions first. strftime mangles unknown %x on some platforms.
  if (Style.empty()) Style = "%Y-%m-%d %H:%M:%S.%N";
  std::string Format;
  raw_string_ostream FStream(Format);
  for (unsigned I = 0; I < Style.size(); ++I) {
    if (Style[I] == '%' && Style.size() > I + 1) switch (Style[I + 1]) {
        case 'L':  // Milliseconds, from Ruby.
          FStream << llvm::format(
              "%.3lu", (long)duration_cast<milliseconds>(Fractional).count());
          ++I;
          continue;
        case 'f':  // Microseconds, from Python.
          FStream << llvm::format(
              "%.6lu", (long)duration_cast<microseconds>(Fractional).count());
          ++I;
          continue;
        case 'N':  // Nanoseconds, from date(1).
          FStream << llvm::format(
              "%.9lu", (long)duration_cast<nanoseconds>(Fractional).count());
          ++I;
          continue;
        case '%':  // Consume %%, so %%f parses as (%%)f not %(%f)
          FStream << "%%";
          ++I;
          continue;
      }
    FStream << Style[I];
  }
  FStream.flush();
  char Buffer[256];  // Should be enough for anywhen.
  size_t Len = strftime(Buffer, sizeof(Buffer), Format.c_str(), &LT);
  OS << (Len ? Buffer : "BAD-DATE-FORMAT");
}

void format_provider<UtcTime<std::chrono::seconds>>::format(
    const UtcTime<std::chrono::seconds> &T, raw_ostream &OS, StringRef Style) {
  using namespace std::chrono;
  UtcTime<seconds> Truncated =
      UtcTime<seconds>(duration_cast<seconds>(T.time_since_epoch()));
  auto Fractional = T - Truncated;
  struct tm LT = getStructTMUtc(Truncated);
  llvm::format(Fractional, LT, OS, Style);
}

void format_provider<TimePoint<>>::format(const TimePoint<> &T, raw_ostream &OS,
                                          StringRef Style) {
  using namespace std::chrono;
  TimePoint<seconds> Truncated = time_point_cast<seconds>(T);
  auto Fractional = T - Truncated;
  struct tm LT = getStructTM(Truncated);
  llvm::format(Fractional, LT, OS, Style);
}

} // namespace llvm
