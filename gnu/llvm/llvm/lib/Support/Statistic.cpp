//===-- Statistic.cpp - Easy way to expose stats information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the 'Statistic' class, which is designed to be an easy
// way to expose various success metrics from passes.  These statistics are
// printed at the end of a run, when the -stats command line option is enabled
// on the command line.
//
// This is useful for reporting information like the number of instructions
// simplified, optimized or removed by various transformations, like this:
//
// static Statistic NumInstEliminated("GCSE", "Number of instructions killed");
//
// Later, in the code: ++NumInstEliminated;
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"

#include "DebugOptions.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstring>
using namespace llvm;

/// -stats - Command line option to cause transformations to emit stats about
/// what they did.
///
static bool EnableStats;
static bool StatsAsJSON;
static bool Enabled;
static bool PrintOnExit;

void llvm::initStatisticOptions() {
  static cl::opt<bool, true> registerEnableStats{
      "stats",
      cl::desc(
          "Enable statistics output from program (available with Asserts)"),
      cl::location(EnableStats), cl::Hidden};
  static cl::opt<bool, true> registerStatsAsJson{
      "stats-json", cl::desc("Display statistics as json data"),
      cl::location(StatsAsJSON), cl::Hidden};
}

namespace {
/// This class is used in a ManagedStatic so that it is created on demand (when
/// the first statistic is bumped) and destroyed only when llvm_shutdown is
/// called. We print statistics from the destructor.
/// This class is also used to look up statistic values from applications that
/// use LLVM.
class StatisticInfo {
  std::vector<TrackingStatistic *> Stats;

  friend void llvm::PrintStatistics();
  friend void llvm::PrintStatistics(raw_ostream &OS);
  friend void llvm::PrintStatisticsJSON(raw_ostream &OS);

  /// Sort statistics by debugtype,name,description.
  void sort();
public:
  using const_iterator = std::vector<TrackingStatistic *>::const_iterator;

  StatisticInfo();
  ~StatisticInfo();

  void addStatistic(TrackingStatistic *S) { Stats.push_back(S); }

  const_iterator begin() const { return Stats.begin(); }
  const_iterator end() const { return Stats.end(); }
  iterator_range<const_iterator> statistics() const {
    return {begin(), end()};
  }

  void reset();
};
} // end anonymous namespace

static ManagedStatic<StatisticInfo> StatInfo;
static ManagedStatic<sys::SmartMutex<true> > StatLock;

/// RegisterStatistic - The first time a statistic is bumped, this method is
/// called.
void TrackingStatistic::RegisterStatistic() {
  // If stats are enabled, inform StatInfo that this statistic should be
  // printed.
  // llvm_shutdown calls destructors while holding the ManagedStatic mutex.
  // These destructors end up calling PrintStatistics, which takes StatLock.
  // Since dereferencing StatInfo and StatLock can require taking the
  // ManagedStatic mutex, doing so with StatLock held would lead to a lock
  // order inversion. To avoid that, we dereference the ManagedStatics first,
  // and only take StatLock afterwards.
  if (!Initialized.load(std::memory_order_relaxed)) {
    sys::SmartMutex<true> &Lock = *StatLock;
    StatisticInfo &SI = *StatInfo;
    sys::SmartScopedLock<true> Writer(Lock);
    // Check Initialized again after acquiring the lock.
    if (Initialized.load(std::memory_order_relaxed))
      return;
    if (EnableStats || Enabled)
      SI.addStatistic(this);

    // Remember we have been registered.
    Initialized.store(true, std::memory_order_release);
  }
}

StatisticInfo::StatisticInfo() {
  // Ensure that necessary timer global objects are created first so they are
  // destructed after us.
  TimerGroup::constructForStatistics();
}

// Print information when destroyed, iff command line option is specified.
StatisticInfo::~StatisticInfo() {
  if (EnableStats || PrintOnExit)
    llvm::PrintStatistics();
}

void llvm::EnableStatistics(bool DoPrintOnExit) {
  Enabled = true;
  PrintOnExit = DoPrintOnExit;
}

bool llvm::AreStatisticsEnabled() { return Enabled || EnableStats; }

void StatisticInfo::sort() {
  llvm::stable_sort(
      Stats, [](const TrackingStatistic *LHS, const TrackingStatistic *RHS) {
        if (int Cmp = std::strcmp(LHS->getDebugType(), RHS->getDebugType()))
          return Cmp < 0;

        if (int Cmp = std::strcmp(LHS->getName(), RHS->getName()))
          return Cmp < 0;

        return std::strcmp(LHS->getDesc(), RHS->getDesc()) < 0;
      });
}

void StatisticInfo::reset() {
  sys::SmartScopedLock<true> Writer(*StatLock);

  // Tell each statistic that it isn't registered so it has to register
  // again. We're holding the lock so it won't be able to do so until we're
  // finished. Once we've forced it to re-register (after we return), then zero
  // the value.
  for (auto *Stat : Stats) {
    // Value updates to a statistic that complete before this statement in the
    // iteration for that statistic will be lost as intended.
    Stat->Initialized = false;
    Stat->Value = 0;
  }

  // Clear the registration list and release the lock once we're done. Any
  // pending updates from other threads will safely take effect after we return.
  // That might not be what the user wants if they're measuring a compilation
  // but it's their responsibility to prevent concurrent compilations to make
  // a single compilation measurable.
  Stats.clear();
}

void llvm::PrintStatistics(raw_ostream &OS) {
  StatisticInfo &Stats = *StatInfo;

  // Figure out how long the biggest Value and Name fields are.
  unsigned MaxDebugTypeLen = 0, MaxValLen = 0;
  for (TrackingStatistic *Stat : Stats.Stats) {
    MaxValLen = std::max(MaxValLen, (unsigned)utostr(Stat->getValue()).size());
    MaxDebugTypeLen =
        std::max(MaxDebugTypeLen, (unsigned)std::strlen(Stat->getDebugType()));
  }

  Stats.sort();

  // Print out the statistics header...
  OS << "===" << std::string(73, '-') << "===\n"
     << "                          ... Statistics Collected ...\n"
     << "===" << std::string(73, '-') << "===\n\n";

  // Print all of the statistics.
  for (TrackingStatistic *Stat : Stats.Stats)
    OS << format("%*" PRIu64 " %-*s - %s\n", MaxValLen, Stat->getValue(),
                 MaxDebugTypeLen, Stat->getDebugType(), Stat->getDesc());

  OS << '\n';  // Flush the output stream.
  OS.flush();
}

void llvm::PrintStatisticsJSON(raw_ostream &OS) {
  sys::SmartScopedLock<true> Reader(*StatLock);
  StatisticInfo &Stats = *StatInfo;

  Stats.sort();

  // Print all of the statistics.
  OS << "{\n";
  const char *delim = "";
  for (const TrackingStatistic *Stat : Stats.Stats) {
    OS << delim;
    assert(yaml::needsQuotes(Stat->getDebugType()) == yaml::QuotingType::None &&
           "Statistic group/type name is simple.");
    assert(yaml::needsQuotes(Stat->getName()) == yaml::QuotingType::None &&
           "Statistic name is simple");
    OS << "\t\"" << Stat->getDebugType() << '.' << Stat->getName() << "\": "
       << Stat->getValue();
    delim = ",\n";
  }
  // Print timers.
  TimerGroup::printAllJSONValues(OS, delim);

  OS << "\n}\n";
  OS.flush();
}

void llvm::PrintStatistics() {
#if LLVM_ENABLE_STATS
  sys::SmartScopedLock<true> Reader(*StatLock);
  StatisticInfo &Stats = *StatInfo;

  // Statistics not enabled?
  if (Stats.Stats.empty()) return;

  // Get the stream to write to.
  std::unique_ptr<raw_ostream> OutStream = CreateInfoOutputFile();
  if (StatsAsJSON)
    PrintStatisticsJSON(*OutStream);
  else
    PrintStatistics(*OutStream);

#else
  // Check if the -stats option is set instead of checking
  // !Stats.Stats.empty().  In release builds, Statistics operators
  // do nothing, so stats are never Registered.
  if (EnableStats) {
    // Get the stream to write to.
    std::unique_ptr<raw_ostream> OutStream = CreateInfoOutputFile();
    (*OutStream) << "Statistics are disabled.  "
                 << "Build with asserts or with -DLLVM_FORCE_ENABLE_STATS\n";
  }
#endif
}

std::vector<std::pair<StringRef, uint64_t>> llvm::GetStatistics() {
  sys::SmartScopedLock<true> Reader(*StatLock);
  std::vector<std::pair<StringRef, uint64_t>> ReturnStats;

  for (const auto &Stat : StatInfo->statistics())
    ReturnStats.emplace_back(Stat->getName(), Stat->getValue());
  return ReturnStats;
}

void llvm::ResetStatistics() {
  StatInfo->reset();
}
