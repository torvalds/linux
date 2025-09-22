//===-- llvm/ADT/Statistic.h - Easy way to expose stats ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the 'Statistic' class, which is designed to be an easy way
/// to expose various metrics from passes.  These statistics are printed at the
/// end of a run (from llvm_shutdown), when the -stats command line option is
/// passed on the command line.
///
/// This is useful for reporting information like the number of instructions
/// simplified, optimized or removed by various transformations, like this:
///
/// static Statistic NumInstsKilled("gcse", "Number of instructions killed");
///
/// Later, in the code: ++NumInstsKilled;
///
/// NOTE: Statistics *must* be declared as global variables.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STATISTIC_H
#define LLVM_ADT_STATISTIC_H

#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Compiler.h"
#include <atomic>
#include <memory>
#include <vector>

// Determine whether statistics should be enabled. We must do it here rather
// than in CMake because multi-config generators cannot determine this at
// configure time.
#if !defined(NDEBUG) || LLVM_FORCE_ENABLE_STATS
#define LLVM_ENABLE_STATS 1
#else
#define LLVM_ENABLE_STATS 0
#endif

namespace llvm {

class raw_ostream;
class raw_fd_ostream;
class StringRef;

class TrackingStatistic {
public:
  const char *const DebugType;
  const char *const Name;
  const char *const Desc;

  std::atomic<uint64_t> Value;
  std::atomic<bool> Initialized;

  constexpr TrackingStatistic(const char *DebugType, const char *Name,
                              const char *Desc)
      : DebugType(DebugType), Name(Name), Desc(Desc), Value(0),
        Initialized(false) {}

  const char *getDebugType() const { return DebugType; }
  const char *getName() const { return Name; }
  const char *getDesc() const { return Desc; }

  uint64_t getValue() const { return Value.load(std::memory_order_relaxed); }

  // Allow use of this class as the value itself.
  operator uint64_t() const { return getValue(); }

  const TrackingStatistic &operator=(uint64_t Val) {
    Value.store(Val, std::memory_order_relaxed);
    return init();
  }

  const TrackingStatistic &operator++() {
    Value.fetch_add(1, std::memory_order_relaxed);
    return init();
  }

  uint64_t operator++(int) {
    init();
    return Value.fetch_add(1, std::memory_order_relaxed);
  }

  const TrackingStatistic &operator--() {
    Value.fetch_sub(1, std::memory_order_relaxed);
    return init();
  }

  uint64_t operator--(int) {
    init();
    return Value.fetch_sub(1, std::memory_order_relaxed);
  }

  const TrackingStatistic &operator+=(uint64_t V) {
    if (V == 0)
      return *this;
    Value.fetch_add(V, std::memory_order_relaxed);
    return init();
  }

  const TrackingStatistic &operator-=(uint64_t V) {
    if (V == 0)
      return *this;
    Value.fetch_sub(V, std::memory_order_relaxed);
    return init();
  }

  void updateMax(uint64_t V) {
    uint64_t PrevMax = Value.load(std::memory_order_relaxed);
    // Keep trying to update max until we succeed or another thread produces
    // a bigger max than us.
    while (V > PrevMax && !Value.compare_exchange_weak(
                              PrevMax, V, std::memory_order_relaxed)) {
    }
    init();
  }

protected:
  TrackingStatistic &init() {
    if (!Initialized.load(std::memory_order_acquire))
      RegisterStatistic();
    return *this;
  }

  void RegisterStatistic();
};

class NoopStatistic {
public:
  NoopStatistic(const char * /*DebugType*/, const char * /*Name*/,
                const char * /*Desc*/) {}

  uint64_t getValue() const { return 0; }

  // Allow use of this class as the value itself.
  operator uint64_t() const { return 0; }

  const NoopStatistic &operator=(uint64_t Val) { return *this; }

  const NoopStatistic &operator++() { return *this; }

  uint64_t operator++(int) { return 0; }

  const NoopStatistic &operator--() { return *this; }

  uint64_t operator--(int) { return 0; }

  const NoopStatistic &operator+=(const uint64_t &V) { return *this; }

  const NoopStatistic &operator-=(const uint64_t &V) { return *this; }

  void updateMax(uint64_t V) {}
};

#if LLVM_ENABLE_STATS
using Statistic = TrackingStatistic;
#else
using Statistic = NoopStatistic;
#endif

// STATISTIC - A macro to make definition of statistics really simple.  This
// automatically passes the DEBUG_TYPE of the file into the statistic.
#define STATISTIC(VARNAME, DESC)                                               \
  static llvm::Statistic VARNAME = {DEBUG_TYPE, #VARNAME, DESC}

// ALWAYS_ENABLED_STATISTIC - A macro to define a statistic like STATISTIC but
// it is enabled even if LLVM_ENABLE_STATS is off.
#define ALWAYS_ENABLED_STATISTIC(VARNAME, DESC)                                \
  static llvm::TrackingStatistic VARNAME = {DEBUG_TYPE, #VARNAME, DESC}

/// Enable the collection and printing of statistics.
void EnableStatistics(bool DoPrintOnExit = true);

/// Check if statistics are enabled.
bool AreStatisticsEnabled();

/// Return a file stream to print our output on.
std::unique_ptr<raw_fd_ostream> CreateInfoOutputFile();

/// Print statistics to the file returned by CreateInfoOutputFile().
void PrintStatistics();

/// Print statistics to the given output stream.
void PrintStatistics(raw_ostream &OS);

/// Print statistics in JSON format. This does include all global timers (\see
/// Timer, TimerGroup). Note that the timers are cleared after printing and will
/// not be printed in human readable form or in a second call of
/// PrintStatisticsJSON().
void PrintStatisticsJSON(raw_ostream &OS);

/// Get the statistics. This can be used to look up the value of
/// statistics without needing to parse JSON.
///
/// This function does not prevent statistics being updated by other threads
/// during it's execution. It will return the value at the point that it is
/// read. However, it will prevent new statistics from registering until it
/// completes.
std::vector<std::pair<StringRef, uint64_t>> GetStatistics();

/// Reset the statistics. This can be used to zero and de-register the
/// statistics in order to measure a compilation.
///
/// When this function begins to call destructors prior to returning, all
/// statistics will be zero and unregistered. However, that might not remain the
/// case by the time this function finishes returning. Whether update from other
/// threads are lost or merely deferred until during the function return is
/// timing sensitive.
///
/// Callers who intend to use this to measure statistics for a single
/// compilation should ensure that no compilations are in progress at the point
/// this function is called and that only one compilation executes until calling
/// GetStatistics().
void ResetStatistics();

} // end namespace llvm

#endif // LLVM_ADT_STATISTIC_H
