//===-- IntelPTMultiCoreTrace.h ------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_IntelPTMultiCoreTrace_H_
#define liblldb_IntelPTMultiCoreTrace_H_

#include "IntelPTProcessTrace.h"
#include "IntelPTSingleBufferTrace.h"
#include "lldb/Host/common/NativeProcessProtocol.h"
#include "lldb/Utility/TraceIntelPTGDBRemotePackets.h"
#include "lldb/lldb-types.h"
#include "llvm/Support/Error.h"
#include <memory>
#include <optional>

namespace lldb_private {
namespace process_linux {

class IntelPTMultiCoreTrace : public IntelPTProcessTrace {
  using ContextSwitchTrace = PerfEvent;

public:
  /// Start tracing all CPU cores.
  ///
  /// \param[in] request
  ///   Intel PT configuration parameters.
  ///
  /// \param[in] process
  ///   The process being debugged.
  ///
  ///  \param[in] cgroup_fd
  ///  A file descriptor in /sys/fs associated with the cgroup of the process to
  ///  trace. If not \a std::nullopt, then the trace sesion will use cgroup
  ///  filtering.
  ///
  /// \return
  ///   An \a IntelPTMultiCoreTrace instance if tracing was successful, or
  ///   an \a llvm::Error otherwise.
  static llvm::Expected<std::unique_ptr<IntelPTMultiCoreTrace>>
  StartOnAllCores(const TraceIntelPTStartRequest &request,
                  NativeProcessProtocol &process,
                  std::optional<int> cgroup_fd = std::nullopt);

  /// Execute the provided callback on each core that is being traced.
  ///
  /// \param[in] callback.cpu_id
  ///   The core id that is being traced.
  ///
  /// \param[in] callback.core_trace
  ///   The single-buffer trace instance for the given core.
  void ForEachCore(std::function<void(lldb::cpu_id_t cpu_id,
                                      IntelPTSingleBufferTrace &core_trace)>
                       callback);

  /// Execute the provided callback on each core that is being traced.
  ///
  /// \param[in] callback.cpu_id
  ///   The core id that is being traced.
  ///
  /// \param[in] callback.intelpt_trace
  ///   The single-buffer intel pt trace instance for the given core.
  ///
  /// \param[in] callback.context_switch_trace
  ///   The perf event collecting context switches for the given core.
  void ForEachCore(std::function<void(lldb::cpu_id_t cpu_id,
                                      IntelPTSingleBufferTrace &intelpt_trace,
                                      ContextSwitchTrace &context_switch_trace)>
                       callback);

  void ProcessDidStop() override;

  void ProcessWillResume() override;

  TraceIntelPTGetStateResponse GetState() override;

  bool TracesThread(lldb::tid_t tid) const override;

  llvm::Error TraceStart(lldb::tid_t tid) override;

  llvm::Error TraceStop(lldb::tid_t tid) override;

  llvm::Expected<std::optional<std::vector<uint8_t>>>
  TryGetBinaryData(const TraceGetBinaryDataRequest &request) override;

private:
  /// This assumes that all underlying perf_events for each core are part of the
  /// same perf event group.
  IntelPTMultiCoreTrace(
      llvm::DenseMap<lldb::cpu_id_t,
                     std::pair<IntelPTSingleBufferTrace, ContextSwitchTrace>>
          &&traces_per_core,
      NativeProcessProtocol &process, bool using_cgroup_filtering)
      : m_traces_per_core(std::move(traces_per_core)), m_process(process),
        m_using_cgroup_filtering(using_cgroup_filtering) {}

  llvm::DenseMap<lldb::cpu_id_t,
                 std::pair<IntelPTSingleBufferTrace, ContextSwitchTrace>>
      m_traces_per_core;

  /// The target process.
  NativeProcessProtocol &m_process;
  bool m_using_cgroup_filtering;
};

} // namespace process_linux
} // namespace lldb_private

#endif // liblldb_IntelPTMultiCoreTrace_H_
