//===-- TraceIntelPTMultiCpuDecoder.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTMULTICPUDECODER_H
#define LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTMULTICPUDECODER_H

#include "LibiptDecoder.h"
#include "PerfContextSwitchDecoder.h"
#include "ThreadDecoder.h"
#include "forward-declarations.h"
#include <optional>

namespace lldb_private {
namespace trace_intel_pt {

/// Class used to decode a multi-cpu Intel PT trace. It assumes that each
/// thread could have potentially been executed on different cpu cores. It uses
/// a context switch trace per CPU with timestamps to identify which thread owns
/// each Intel PT decoded instruction and in which order. It also assumes that
/// the Intel PT data and context switches might have gaps in their traces due
/// to contention or race conditions. Finally, it assumes that a tid is not
/// repeated twice for two different threads because of the shortness of the
/// intel pt trace.
///
/// This object should be recreated after every stop in the case of live
/// processes.
class TraceIntelPTMultiCpuDecoder {
public:
  /// \param[in] TraceIntelPT
  ///   The trace object to be decoded
  TraceIntelPTMultiCpuDecoder(TraceIntelPTSP trace_sp);

  /// \return
  ///   A \a DecodedThread for the \p thread by decoding its instructions on all
  ///   CPUs, sorted by TSCs. An \a llvm::Error is returned if the decoder
  ///   couldn't be properly set up.
  llvm::Expected<DecodedThreadSP> Decode(Thread &thread);

  /// \return
  ///   \b true if the given \p tid is managed by this decoder, regardless of
  ///   whether there's tracing data associated to it or not.
  bool TracesThread(lldb::tid_t tid) const;

  /// \return
  ///   The number of continuous executions found for the given \p tid.
  size_t GetNumContinuousExecutionsForThread(lldb::tid_t tid) const;

  /// \return
  ///   The number of PSB blocks for a given thread in all cores.
  size_t GePSBBlocksCountForThread(lldb::tid_t tid) const;

  /// \return
  ///   The total number of continuous executions found across CPUs.
  size_t GetTotalContinuousExecutionsCount() const;

  /// \return
  ///   The number of psb blocks in all cores that couldn't be matched with a
  ///   thread execution coming from context switch traces.
  size_t GetUnattributedPSBBlocksCount() const;

  /// \return
  ///   The total number of PSB blocks in all cores.
  size_t GetTotalPSBBlocksCount() const;

  /// \return
  ///     The lowest TSC value in this trace if available, \a std::nullopt if
  ///     the trace is empty or the trace contains no timing information, or an
  ///     \a llvm::Error if it was not possible to set up the decoder.
  llvm::Expected<std::optional<uint64_t>> FindLowestTSC();

private:
  /// Traverse the context switch traces and the basic intel pt continuous
  /// subtraces and produce a list of continuous executions for each process and
  /// thread.
  ///
  /// See \a DoCorrelateContextSwitchesAndIntelPtTraces.
  ///
  /// Any errors are stored in \a m_setup_error.
  llvm::Error CorrelateContextSwitchesAndIntelPtTraces();

  /// Produce a mapping from thread ids to the list of continuos executions with
  /// their associated intel pt subtraces.
  llvm::Expected<
      llvm::DenseMap<lldb::tid_t, std::vector<IntelPTThreadContinousExecution>>>
  DoCorrelateContextSwitchesAndIntelPtTraces();

  TraceIntelPTSP GetTrace();

  std::weak_ptr<TraceIntelPT> m_trace_wp;
  std::set<lldb::tid_t> m_tids;
  std::optional<
      llvm::DenseMap<lldb::tid_t, std::vector<IntelPTThreadContinousExecution>>>
      m_continuous_executions_per_thread;
  llvm::DenseMap<lldb::tid_t, DecodedThreadSP> m_decoded_threads;
  /// This variable will not be std::nullopt if a severe error happened during
  /// the setup of the decoder and we don't want decoding to be reattempted.
  std::optional<std::string> m_setup_error;
  uint64_t m_unattributed_psb_blocks = 0;
  uint64_t m_total_psb_blocks = 0;
};

} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_INTEL_PT_TRACEINTELPTMULTICPUDECODER_H
