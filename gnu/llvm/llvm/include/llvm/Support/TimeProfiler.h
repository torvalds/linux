//===- llvm/Support/TimeProfiler.h - Hierarchical Time Profiler -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides lightweight and dependency-free machinery to trace execution
// time around arbitrary code. Two API flavors are available.
//
// The primary API uses a RAII object to trigger tracing:
//
// \code
//   {
//     TimeTraceScope scope("my_event_name");
//     ...my code...
//   }
// \endcode
//
// If the code to be profiled does not have a natural lexical scope then
// it is also possible to start and end events with respect to an implicit
// per-thread stack of profiling entries:
//
// \code
//   timeTraceProfilerBegin("my_event_name");
//   ...my code...
//   timeTraceProfilerEnd();  // must be called on all control flow paths
// \endcode
//
// Time profiling entries can be given an arbitrary name and, optionally,
// an arbitrary 'detail' string. The resulting trace will include 'Total'
// entries summing the time spent for each name. Thus, it's best to choose
// names to be fairly generic, and rely on the detail field to capture
// everything else of interest.
//
// To avoid lifetime issues name and detail strings are copied into the event
// entries at their time of creation. Care should be taken to make string
// construction cheap to prevent 'Heisenperf' effects. In particular, the
// 'detail' argument may be a string-returning closure:
//
// \code
//   int n;
//   {
//     TimeTraceScope scope("my_event_name",
//                          [n]() { return (Twine("x=") + Twine(n)).str(); });
//     ...my code...
//   }
// \endcode
// The closure will not be called if tracing is disabled. Otherwise, the
// resulting string will be directly moved into the entry.
//
// The main process should begin with a timeTraceProfilerInitialize, and
// finish with timeTraceProfileWrite and timeTraceProfilerCleanup calls.
// Each new thread should begin with a timeTraceProfilerInitialize, and
// finish with a timeTraceProfilerFinishThread call.
//
// Timestamps come from std::chrono::stable_clock. Note that threads need
// not see the same time from that clock, and the resolution may not be
// the best available.
//
// Currently, there are a number of compatible viewers:
//  - chrome://tracing is the original chromium trace viewer.
//  - http://ui.perfetto.dev is the replacement for the above, under active
//    development by Google as part of the 'Perfetto' project.
//  - https://www.speedscope.app/ has also been reported as an option.
//
// Future work:
//  - Support akin to LLVM_DEBUG for runtime enable/disable of named tracing
//    families for non-debug builds which wish to support optional tracing.
//  - Evaluate the detail closures at profile write time to avoid
//    stringification costs interfering with tracing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TIMEPROFILER_H
#define LLVM_SUPPORT_TIMEPROFILER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/Error.h"

namespace llvm {

class raw_pwrite_stream;

struct TimeTraceMetadata {
  std::string Detail;
  // Source file and line number information for the event.
  std::string File;
  int Line = 0;

  bool isEmpty() const { return Detail.empty() && File.empty(); }
};

struct TimeTraceProfiler;
TimeTraceProfiler *getTimeTraceProfilerInstance();

bool isTimeTraceVerbose();

struct TimeTraceProfilerEntry;

/// Initialize the time trace profiler.
/// This sets up the global \p TimeTraceProfilerInstance
/// variable to be the profiler instance.
void timeTraceProfilerInitialize(unsigned TimeTraceGranularity,
                                 StringRef ProcName,
                                 bool TimeTraceVerbose = false);

/// Cleanup the time trace profiler, if it was initialized.
void timeTraceProfilerCleanup();

/// Finish a time trace profiler running on a worker thread.
void timeTraceProfilerFinishThread();

/// Is the time trace profiler enabled, i.e. initialized?
inline bool timeTraceProfilerEnabled() {
  return getTimeTraceProfilerInstance() != nullptr;
}

/// Write profiling data to output stream.
/// Data produced is JSON, in Chrome "Trace Event" format, see
/// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview
void timeTraceProfilerWrite(raw_pwrite_stream &OS);

/// Write profiling data to a file.
/// The function will write to \p PreferredFileName if provided, if not
/// then will write to \p FallbackFileName appending .time-trace.
/// Returns a StringError indicating a failure if the function is
/// unable to open the file for writing.
Error timeTraceProfilerWrite(StringRef PreferredFileName,
                             StringRef FallbackFileName);

/// Manually begin a time section, with the given \p Name and \p Detail.
/// Profiler copies the string data, so the pointers can be given into
/// temporaries. Time sections can be hierarchical; every Begin must have a
/// matching End pair but they can nest.
TimeTraceProfilerEntry *timeTraceProfilerBegin(StringRef Name,
                                               StringRef Detail);
TimeTraceProfilerEntry *
timeTraceProfilerBegin(StringRef Name,
                       llvm::function_ref<std::string()> Detail);

TimeTraceProfilerEntry *
timeTraceProfilerBegin(StringRef Name,
                       llvm::function_ref<TimeTraceMetadata()> MetaData);

/// Manually begin a time section, with the given \p Name and \p Detail.
/// This starts Async Events having \p Name as a category which is shown
/// separately from other traces. See
/// https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview#heading=h.jh64i9l3vwa1
/// for more details.
TimeTraceProfilerEntry *timeTraceAsyncProfilerBegin(StringRef Name,
                                                    StringRef Detail);

/// Manually end the last time section.
void timeTraceProfilerEnd();
void timeTraceProfilerEnd(TimeTraceProfilerEntry *E);

/// The TimeTraceScope is a helper class to call the begin and end functions
/// of the time trace profiler.  When the object is constructed, it begins
/// the section; and when it is destroyed, it stops it. If the time profiler
/// is not initialized, the overhead is a single branch.
class TimeTraceScope {
public:
  TimeTraceScope() = delete;
  TimeTraceScope(const TimeTraceScope &) = delete;
  TimeTraceScope &operator=(const TimeTraceScope &) = delete;
  TimeTraceScope(TimeTraceScope &&) = delete;
  TimeTraceScope &operator=(TimeTraceScope &&) = delete;

  TimeTraceScope(StringRef Name) {
    if (getTimeTraceProfilerInstance() != nullptr)
      Entry = timeTraceProfilerBegin(Name, StringRef(""));
  }
  TimeTraceScope(StringRef Name, StringRef Detail) {
    if (getTimeTraceProfilerInstance() != nullptr)
      Entry = timeTraceProfilerBegin(Name, Detail);
  }
  TimeTraceScope(StringRef Name, llvm::function_ref<std::string()> Detail) {
    if (getTimeTraceProfilerInstance() != nullptr)
      Entry = timeTraceProfilerBegin(Name, Detail);
  }
  TimeTraceScope(StringRef Name,
                 llvm::function_ref<TimeTraceMetadata()> Metadata) {
    if (getTimeTraceProfilerInstance() != nullptr)
      Entry = timeTraceProfilerBegin(Name, Metadata);
  }
  ~TimeTraceScope() {
    if (getTimeTraceProfilerInstance() != nullptr)
      timeTraceProfilerEnd(Entry);
  }

private:
  TimeTraceProfilerEntry *Entry = nullptr;
};

} // end namespace llvm

#endif
