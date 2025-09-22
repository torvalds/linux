//===-- LibiptDecoder.h --======---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_LIBIPT_DECODER_H
#define LLDB_SOURCE_PLUGINS_TRACE_LIBIPT_DECODER_H

#include "DecodedThread.h"
#include "PerfContextSwitchDecoder.h"
#include "forward-declarations.h"
#include "intel-pt.h"
#include <optional>

namespace lldb_private {
namespace trace_intel_pt {

/// This struct represents a contiguous section of a trace that starts at a PSB
/// and ends right before the next PSB or the end of the trace.
struct PSBBlock {
  /// The memory offset of a PSB packet that is a synchronization point for the
  /// decoder. A decoder normally looks first for a PSB packet and then it
  /// starts decoding.
  uint64_t psb_offset;
  /// The timestamp associated with the PSB packet above.
  std::optional<uint64_t> tsc;
  /// Size in bytes of this block
  uint64_t size;
  /// The first ip for this PSB block.
  /// This is \a std::nullopt if tracing was disabled when the PSB block was
  /// emitted.  This means that eventually there's be an enablement event that
  /// will come with an ip.
  std::optional<lldb::addr_t> starting_ip;
};

/// This struct represents a continuous execution of a thread in a cpu,
/// delimited by a context switch in and out, and a list of Intel PT subtraces
/// that belong to this execution.
struct IntelPTThreadContinousExecution {
  ThreadContinuousExecution thread_execution;
  std::vector<PSBBlock> psb_blocks;

  IntelPTThreadContinousExecution(
      const ThreadContinuousExecution &thread_execution)
      : thread_execution(thread_execution) {}

  /// Comparator by time
  bool operator<(const IntelPTThreadContinousExecution &o) const;
};

/// Decode a raw Intel PT trace for a single thread given in \p buffer and
/// append the decoded instructions and errors in \p decoded_thread. It uses the
/// low level libipt library underneath.
///
/// \return
///   An \a llvm::Error if the decoder couldn't be properly set up.
llvm::Error DecodeSingleTraceForThread(DecodedThread &decoded_thread,
                                       TraceIntelPT &trace_intel_pt,
                                       llvm::ArrayRef<uint8_t> buffer);

/// Decode a raw Intel PT trace for a single thread that was collected in a per
/// cpu core basis.
///
/// \param[out] decoded_thread
///   All decoded instructions, errors and events will be appended to this
///   object.
///
/// \param[in] trace_intel_pt
///   The main Trace object that contains all the information related to the
///   trace session.
///
/// \param[in] buffers
///   A map from cpu core id to raw intel pt buffers.
///
/// \param[in] executions
///   A list of chunks of timed executions of the same given thread. It is used
///   to identify if some executions have missing intel pt data and also to
///   determine in which core a certain part of the execution ocurred.
///
/// \return
///   An \a llvm::Error if the decoder couldn't be properly set up, i.e. no
///   instructions were attempted to be decoded.
llvm::Error DecodeSystemWideTraceForThread(
    DecodedThread &decoded_thread, TraceIntelPT &trace_intel_pt,
    const llvm::DenseMap<lldb::cpu_id_t, llvm::ArrayRef<uint8_t>> &buffers,
    const std::vector<IntelPTThreadContinousExecution> &executions);

/// Given an intel pt trace, split it in chunks delimited by PSB packets. Each
/// of these chunks is guaranteed to have been executed continuously.
///
/// \param[in] trace_intel_pt
///   The main Trace object that contains all the information related to the
///   trace session.
///
/// \param[in] buffer
///   The intel pt buffer that belongs to a single thread or to a single cpu
///   core.
///
/// \param[in] expect_tscs
///   If \b true, an error is return if a packet without TSC is found.
///
/// \return
///   A list of continuous executions sorted by time, or an \a llvm::Error in
///   case of failures.
llvm::Expected<std::vector<PSBBlock>>
SplitTraceIntoPSBBlock(TraceIntelPT &trace_intel_pt,
                       llvm::ArrayRef<uint8_t> buffer, bool expect_tscs);

/// Find the lowest TSC in the given trace.
///
/// \return
///     The lowest TSC value in this trace if available, \a std::nullopt if the
///     trace is empty or the trace contains no timing information, or an \a
///     llvm::Error if it was not possible to set up the decoder.
llvm::Expected<std::optional<uint64_t>>
FindLowestTSCInTrace(TraceIntelPT &trace_intel_pt,
                     llvm::ArrayRef<uint8_t> buffer);

} // namespace trace_intel_pt
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_LIBIPT_DECODER_H
