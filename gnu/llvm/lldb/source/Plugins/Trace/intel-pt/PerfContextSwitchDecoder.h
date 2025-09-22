//===-- PerfContextSwitchDecoder.h --======----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_PERFCONTEXTSWITCHDECODER_H
#define LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_PERFCONTEXTSWITCHDECODER_H

#include "lldb/Utility/TraceIntelPTGDBRemotePackets.h"
#include "lldb/lldb-types.h"
#include "llvm/Support/Error.h"
#include <set>
#include <vector>

namespace lldb_private {
namespace trace_intel_pt {

/// This class indicates the time interval in which a thread was running
/// continuously on a cpu core.
struct ThreadContinuousExecution {

  /// In most cases both the start and end of a continuous execution can be
  /// accurately recovered from the context switch trace, but in some cases one
  /// of these endpoints might be guessed or not known at all, due to contention
  /// problems in the trace or because tracing was interrupted, e.g. with ioctl
  /// calls, which causes gaps in the trace. Because of that, we identify which
  /// situation we fall into with the following variants.
  enum class Variant {
    /// Both endpoints are known.
    Complete,
    /// The end is known and we have a lower bound for the start, i.e. the
    /// previous execution in the same cpu happens strictly before the hinted
    /// start.
    HintedStart,
    /// The start is known and we have an upper bound for the end, i.e. the next
    /// execution in the same cpu happens strictly after the hinted end.
    HintedEnd,
    /// We only know the start. This might be the last entry of a cpu trace.
    OnlyStart,
    /// We only know the end. This might be the first entry or a cpu trace.
    OnlyEnd,
  } variant;

  /// \return
  ///   The lowest tsc that we are sure of, i.e. not hinted.
  uint64_t GetLowestKnownTSC() const;

  /// \return
  ///   The known or hinted start tsc, or 0 if the variant is \a OnlyEnd.
  uint64_t GetStartTSC() const;

  /// \return
  ///   The known or hinted end tsc, or max \a uint64_t if the variant is \a
  ///   OnlyStart.
  uint64_t GetEndTSC() const;

  /// Constructors for the different variants of this object
  ///
  /// \{
  static ThreadContinuousExecution
  CreateCompleteExecution(lldb::cpu_id_t cpu_id, lldb::tid_t tid,
                          lldb::pid_t pid, uint64_t start, uint64_t end);

  static ThreadContinuousExecution
  CreateHintedStartExecution(lldb::cpu_id_t cpu_id, lldb::tid_t tid,
                             lldb::pid_t pid, uint64_t hinted_start,
                             uint64_t end);

  static ThreadContinuousExecution
  CreateHintedEndExecution(lldb::cpu_id_t cpu_id, lldb::tid_t tid,
                           lldb::pid_t pid, uint64_t start,
                           uint64_t hinted_end);

  static ThreadContinuousExecution CreateOnlyEndExecution(lldb::cpu_id_t cpu_id,
                                                          lldb::tid_t tid,
                                                          lldb::pid_t pid,
                                                          uint64_t end);

  static ThreadContinuousExecution
  CreateOnlyStartExecution(lldb::cpu_id_t cpu_id, lldb::tid_t tid,
                           lldb::pid_t pid, uint64_t start);
  /// \}

  union {
    struct {
      uint64_t start;
      uint64_t end;
    } complete;
    struct {
      uint64_t start;
    } only_start;
    struct {
      uint64_t end;
    } only_end;
    /// The following 'hinted' structures are useful when there are contention
    /// problems in the trace
    struct {
      uint64_t hinted_start;
      uint64_t end;
    } hinted_start;
    struct {
      uint64_t start;
      uint64_t hinted_end;
    } hinted_end;
  } tscs;

  lldb::cpu_id_t cpu_id;
  lldb::tid_t tid;
  lldb::pid_t pid;

private:
  /// We keep this constructor private to force the usage of the static named
  /// constructors.
  ThreadContinuousExecution(lldb::cpu_id_t cpu_id, lldb::tid_t tid,
                            lldb::pid_t pid)
      : cpu_id(cpu_id), tid(tid), pid(pid) {}
};

/// Decodes a context switch trace collected with perf_event_open.
///
/// \param[in] data
///   The context switch trace in binary format.
///
/// \param[i] cpu_id
///   The cpu_id where the trace were gotten from.
///
/// \param[in] tsc_conversion
///   The conversion values used to confert nanoseconds to TSC.
///
/// \return
///   A list of continuous executions recovered from the raw trace sorted by
///   time, or an \a llvm::Error if the data is malformed.
llvm::Expected<std::vector<ThreadContinuousExecution>>
DecodePerfContextSwitchTrace(llvm::ArrayRef<uint8_t> data,
                             lldb::cpu_id_t cpu_id,
                             const LinuxPerfZeroTscConversion &tsc_conversion);

llvm::Expected<std::vector<uint8_t>>
FilterProcessesFromContextSwitchTrace(llvm::ArrayRef<uint8_t> data,
                                      const std::set<lldb::pid_t> &pids);

} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_PERFCONTEXTSWITCHDECODER_H
