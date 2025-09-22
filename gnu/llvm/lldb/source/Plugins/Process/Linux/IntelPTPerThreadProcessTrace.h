//===-- IntelPTPerThreadProcessTrace.h ------------------------ -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_IntelPTPerThreadProcessTrace_H_
#define liblldb_IntelPTPerThreadProcessTrace_H_

#include "IntelPTProcessTrace.h"
#include "IntelPTSingleBufferTrace.h"
#include "IntelPTThreadTraceCollection.h"
#include <optional>

namespace lldb_private {
namespace process_linux {

/// Manages a "process trace" instance by tracing each thread individually.
class IntelPTPerThreadProcessTrace : public IntelPTProcessTrace {
public:
  /// Start tracing the current process by tracing each of its tids
  /// individually.
  ///
  /// \param[in] request
  ///   Intel PT configuration parameters.
  ///
  /// \param[in] current_tids
  ///   List of tids currently alive. In the future, whenever a new thread is
  ///   spawned, they should be traced by calling the \a TraceStart(tid) method.
  ///
  /// \return
  ///   An \a IntelPTMultiCoreTrace instance if tracing was successful, or
  ///   an \a llvm::Error otherwise.
  static llvm::Expected<std::unique_ptr<IntelPTPerThreadProcessTrace>>
  Start(const TraceIntelPTStartRequest &request,
        llvm::ArrayRef<lldb::tid_t> current_tids);

  bool TracesThread(lldb::tid_t tid) const override;

  llvm::Error TraceStart(lldb::tid_t tid) override;

  llvm::Error TraceStop(lldb::tid_t tid) override;

  TraceIntelPTGetStateResponse GetState() override;

  llvm::Expected<std::optional<std::vector<uint8_t>>>
  TryGetBinaryData(const TraceGetBinaryDataRequest &request) override;

private:
  IntelPTPerThreadProcessTrace(const TraceIntelPTStartRequest &request)
      : m_tracing_params(request) {}

  IntelPTThreadTraceCollection m_thread_traces;
  /// Params used to trace threads when the user started "process tracing".
  TraceIntelPTStartRequest m_tracing_params;
};

} // namespace process_linux
} // namespace lldb_private

#endif // liblldb_IntelPTPerThreadProcessTrace_H_
