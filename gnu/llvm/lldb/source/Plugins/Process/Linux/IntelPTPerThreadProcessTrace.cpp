//===-- IntelPTPerThreadProcessTrace.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IntelPTPerThreadProcessTrace.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace process_linux;
using namespace llvm;

bool IntelPTPerThreadProcessTrace::TracesThread(lldb::tid_t tid) const {
  return m_thread_traces.TracesThread(tid);
}

Error IntelPTPerThreadProcessTrace::TraceStop(lldb::tid_t tid) {
  return m_thread_traces.TraceStop(tid);
}

Error IntelPTPerThreadProcessTrace::TraceStart(lldb::tid_t tid) {
  if (m_thread_traces.GetTotalBufferSize() + m_tracing_params.ipt_trace_size >
      static_cast<size_t>(*m_tracing_params.process_buffer_size_limit))
    return createStringError(
        inconvertibleErrorCode(),
        "Thread %" PRIu64 " can't be traced as the process trace size limit "
        "has been reached. Consider retracing with a higher "
        "limit.",
        tid);

  return m_thread_traces.TraceStart(tid, m_tracing_params);
}

TraceIntelPTGetStateResponse IntelPTPerThreadProcessTrace::GetState() {
  TraceIntelPTGetStateResponse state;
  m_thread_traces.ForEachThread(
      [&](lldb::tid_t tid, const IntelPTSingleBufferTrace &thread_trace) {
        state.traced_threads.push_back(
            {tid,
             {{IntelPTDataKinds::kIptTrace, thread_trace.GetIptTraceSize()}}});
      });
  return state;
}

Expected<std::optional<std::vector<uint8_t>>>
IntelPTPerThreadProcessTrace::TryGetBinaryData(
    const TraceGetBinaryDataRequest &request) {
  return m_thread_traces.TryGetBinaryData(request);
}

Expected<std::unique_ptr<IntelPTPerThreadProcessTrace>>
IntelPTPerThreadProcessTrace::Start(const TraceIntelPTStartRequest &request,
                                    ArrayRef<lldb::tid_t> current_tids) {
  std::unique_ptr<IntelPTPerThreadProcessTrace> trace(
      new IntelPTPerThreadProcessTrace(request));

  Error error = Error::success();
  for (lldb::tid_t tid : current_tids)
    error = joinErrors(std::move(error), trace->TraceStart(tid));
  if (error)
    return std::move(error);
  return std::move(trace);
}
