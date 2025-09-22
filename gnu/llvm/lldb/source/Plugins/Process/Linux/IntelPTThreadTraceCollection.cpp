//===-- IntelPTThreadTraceCollection.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IntelPTThreadTraceCollection.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace process_linux;
using namespace llvm;

bool IntelPTThreadTraceCollection::TracesThread(lldb::tid_t tid) const {
  return m_thread_traces.count(tid);
}

Error IntelPTThreadTraceCollection::TraceStop(lldb::tid_t tid) {
  auto it = m_thread_traces.find(tid);
  if (it == m_thread_traces.end())
    return createStringError(inconvertibleErrorCode(),
                             "Thread %" PRIu64 " not currently traced", tid);
  m_total_buffer_size -= it->second.GetIptTraceSize();
  m_thread_traces.erase(tid);
  return Error::success();
}

Error IntelPTThreadTraceCollection::TraceStart(
    lldb::tid_t tid, const TraceIntelPTStartRequest &request) {
  if (TracesThread(tid))
    return createStringError(inconvertibleErrorCode(),
                             "Thread %" PRIu64 " already traced", tid);

  Expected<IntelPTSingleBufferTrace> trace =
      IntelPTSingleBufferTrace::Start(request, tid);
  if (!trace)
    return trace.takeError();

  m_total_buffer_size += trace->GetIptTraceSize();
  m_thread_traces.try_emplace(tid, std::move(*trace));
  return Error::success();
}

size_t IntelPTThreadTraceCollection::GetTotalBufferSize() const {
  return m_total_buffer_size;
}

void IntelPTThreadTraceCollection::ForEachThread(
    std::function<void(lldb::tid_t tid, IntelPTSingleBufferTrace &thread_trace)>
        callback) {
  for (auto &it : m_thread_traces)
    callback(it.first, it.second);
}

Expected<IntelPTSingleBufferTrace &>
IntelPTThreadTraceCollection::GetTracedThread(lldb::tid_t tid) {
  auto it = m_thread_traces.find(tid);
  if (it == m_thread_traces.end())
    return createStringError(inconvertibleErrorCode(),
                             "Thread %" PRIu64 " not currently traced", tid);
  return it->second;
}

void IntelPTThreadTraceCollection::Clear() {
  m_thread_traces.clear();
  m_total_buffer_size = 0;
}

size_t IntelPTThreadTraceCollection::GetTracedThreadsCount() const {
  return m_thread_traces.size();
}

llvm::Expected<std::optional<std::vector<uint8_t>>>
IntelPTThreadTraceCollection::TryGetBinaryData(
    const TraceGetBinaryDataRequest &request) {
  if (!request.tid)
    return std::nullopt;
  if (request.kind != IntelPTDataKinds::kIptTrace)
    return std::nullopt;

  if (!TracesThread(*request.tid))
    return std::nullopt;

  if (Expected<IntelPTSingleBufferTrace &> trace =
          GetTracedThread(*request.tid))
    return trace->GetIptTrace();
  else
    return trace.takeError();
}
