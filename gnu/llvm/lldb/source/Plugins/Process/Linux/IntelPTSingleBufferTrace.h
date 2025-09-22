//===-- IntelPTSingleBufferTrace.h ---------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_IntelPTSingleBufferTrace_H_
#define liblldb_IntelPTSingleBufferTrace_H_

#include "Perf.h"
#include "lldb/Utility/TraceIntelPTGDBRemotePackets.h"
#include "lldb/lldb-types.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace lldb_private {
namespace process_linux {

llvm::Expected<uint32_t> GetIntelPTOSEventType();

/// This class wraps a single perf event collecting intel pt data in a single
/// buffer.
class IntelPTSingleBufferTrace {
public:
  /// Start tracing using a single Intel PT trace buffer.
  ///
  /// \param[in] request
  ///     Intel PT configuration parameters.
  ///
  /// \param[in] tid
  ///     The tid of the thread to be traced. If \b None, then this traces all
  ///     threads of all processes.
  ///
  /// \param[in] cpu_id
  ///     The CPU core id where to trace. If \b None, then this traces all CPUs.
  ///
  /// \param[in] disabled
  ///     If \b true, then no data is collected until \a Resume is invoked.
  ///     Similarly, if \b false, data is collected right away until \a Pause is
  ///     invoked.
  ///
  ///  \param[in] cgroup_fd
  ///   A file descriptor in /sys/fs associated with the cgroup of the process
  ///   to trace. If not \a std::nullopt, then the trace sesion will use cgroup
  ///   filtering.
  ///
  /// \return
  ///   A \a IntelPTSingleBufferTrace instance if tracing was successful, or
  ///   an \a llvm::Error otherwise.
  static llvm::Expected<IntelPTSingleBufferTrace>
  Start(const TraceIntelPTStartRequest &request, std::optional<lldb::tid_t> tid,
        std::optional<lldb::cpu_id_t> cpu_id = std::nullopt,
        bool disabled = false, std::optional<int> cgroup_fd = std::nullopt);

  /// \return
  ///    The bytes requested by a jLLDBTraceGetBinaryData packet that was routed
  ///    to this trace instace.
  llvm::Expected<std::vector<uint8_t>>
  GetBinaryData(const TraceGetBinaryDataRequest &request) const;

  /// Read the intel pt trace buffer managed by this trace instance. To ensure
  /// that the data is up-to-date and is not corrupted by read-write race
  /// conditions, the underlying perf_event is paused during read, and later
  /// it's returned to its initial state.
  ///
  /// \return
  ///     A vector with the requested binary data.
  llvm::Expected<std::vector<uint8_t>> GetIptTrace();

  /// \return
  ///     The total the size in bytes used by the intel pt trace buffer managed
  ///     by this trace instance.
  size_t GetIptTraceSize() const;

  /// Resume the collection of this trace.
  ///
  /// \return
  ///     An error if the trace couldn't be resumed. If the trace is already
  ///     running, this returns \a Error::success().
  llvm::Error Resume();

  /// Pause the collection of this trace.
  ///
  /// \return
  ///     An error if the trace couldn't be paused. If the trace is already
  ///     paused, this returns \a Error::success().
  llvm::Error Pause();

  /// \return
  ///     The underlying PerfEvent for this trace.
  const PerfEvent &GetPerfEvent() const;

private:
  /// Construct new \a IntelPTSingleBufferThreadTrace. Users are supposed to
  /// create instances of this class via the \a Start() method and not invoke
  /// this one directly.
  ///
  /// \param[in] perf_event
  ///   perf event configured for IntelPT.
  ///
  /// \param[in] collection_state
  ///   The initial collection state for the provided perf_event.
  IntelPTSingleBufferTrace(PerfEvent &&perf_event)
      : m_perf_event(std::move(perf_event)) {}

  /// perf event configured for IntelPT.
  PerfEvent m_perf_event;
};

} // namespace process_linux
} // namespace lldb_private

#endif // liblldb_IntelPTSingleBufferTrace_H_
