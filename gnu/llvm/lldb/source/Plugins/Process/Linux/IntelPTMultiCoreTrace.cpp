//===-- IntelPTMultiCoreTrace.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IntelPTMultiCoreTrace.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "Procfs.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace process_linux;
using namespace llvm;

static bool IsTotalBufferLimitReached(ArrayRef<cpu_id_t> cores,
                                      const TraceIntelPTStartRequest &request) {
  uint64_t required = cores.size() * request.ipt_trace_size;
  uint64_t limit = request.process_buffer_size_limit.value_or(
      std::numeric_limits<uint64_t>::max());
  return required > limit;
}

static Error IncludePerfEventParanoidMessageInError(Error &&error) {
  return createStringError(
      inconvertibleErrorCode(),
      "%s\nYou might need to rerun as sudo or to set "
      "/proc/sys/kernel/perf_event_paranoid to a value of 0 or -1. You can use "
      "`sudo sysctl -w kernel.perf_event_paranoid=-1` for that.",
      toString(std::move(error)).c_str());
}

Expected<std::unique_ptr<IntelPTMultiCoreTrace>>
IntelPTMultiCoreTrace::StartOnAllCores(const TraceIntelPTStartRequest &request,
                                       NativeProcessProtocol &process,
                                       std::optional<int> cgroup_fd) {
  Expected<ArrayRef<cpu_id_t>> cpu_ids = GetAvailableLogicalCoreIDs();
  if (!cpu_ids)
    return cpu_ids.takeError();

  if (IsTotalBufferLimitReached(*cpu_ids, request))
    return createStringError(
        inconvertibleErrorCode(),
        "The process can't be traced because the process trace size limit "
        "has been reached. Consider retracing with a higher limit.");

  DenseMap<cpu_id_t, std::pair<IntelPTSingleBufferTrace, ContextSwitchTrace>>
      traces;

  for (cpu_id_t cpu_id : *cpu_ids) {
    Expected<IntelPTSingleBufferTrace> core_trace =
        IntelPTSingleBufferTrace::Start(request, /*tid=*/std::nullopt, cpu_id,
                                        /*disabled=*/true, cgroup_fd);
    if (!core_trace)
      return IncludePerfEventParanoidMessageInError(core_trace.takeError());

    if (Expected<PerfEvent> context_switch_trace =
            CreateContextSwitchTracePerfEvent(cpu_id,
                                              &core_trace->GetPerfEvent())) {
      traces.try_emplace(cpu_id,
                         std::make_pair(std::move(*core_trace),
                                        std::move(*context_switch_trace)));
    } else {
      return context_switch_trace.takeError();
    }
  }

  return std::unique_ptr<IntelPTMultiCoreTrace>(
      new IntelPTMultiCoreTrace(std::move(traces), process, (bool)cgroup_fd));
}

void IntelPTMultiCoreTrace::ForEachCore(
    std::function<void(cpu_id_t cpu_id, IntelPTSingleBufferTrace &core_trace)>
        callback) {
  for (auto &it : m_traces_per_core)
    callback(it.first, it.second.first);
}

void IntelPTMultiCoreTrace::ForEachCore(
    std::function<void(cpu_id_t cpu_id, IntelPTSingleBufferTrace &intelpt_trace,
                       ContextSwitchTrace &context_switch_trace)>
        callback) {
  for (auto &it : m_traces_per_core)
    callback(it.first, it.second.first, it.second.second);
}

void IntelPTMultiCoreTrace::ProcessDidStop() {
  ForEachCore([](cpu_id_t cpu_id, IntelPTSingleBufferTrace &core_trace) {
    if (Error err = core_trace.Pause()) {
      LLDB_LOG_ERROR(GetLog(POSIXLog::Trace), std::move(err),
                     "Unable to pause the core trace for core {0}", cpu_id);
    }
  });
}

void IntelPTMultiCoreTrace::ProcessWillResume() {
  ForEachCore([](cpu_id_t cpu_id, IntelPTSingleBufferTrace &core_trace) {
    if (Error err = core_trace.Resume()) {
      LLDB_LOG_ERROR(GetLog(POSIXLog::Trace), std::move(err),
                     "Unable to resume the core trace for core {0}", cpu_id);
    }
  });
}

TraceIntelPTGetStateResponse IntelPTMultiCoreTrace::GetState() {
  TraceIntelPTGetStateResponse state;
  state.using_cgroup_filtering = m_using_cgroup_filtering;

  for (NativeThreadProtocol &thread : m_process.Threads())
    state.traced_threads.push_back(TraceThreadState{thread.GetID(), {}});

  state.cpus.emplace();
  ForEachCore([&](lldb::cpu_id_t cpu_id,
                  const IntelPTSingleBufferTrace &core_trace,
                  const ContextSwitchTrace &context_switch_trace) {
    state.cpus->push_back(
        {cpu_id,
         {{IntelPTDataKinds::kIptTrace, core_trace.GetIptTraceSize()},
          {IntelPTDataKinds::kPerfContextSwitchTrace,
           context_switch_trace.GetEffectiveDataBufferSize()}}});
  });

  return state;
}

bool IntelPTMultiCoreTrace::TracesThread(lldb::tid_t tid) const {
  // All the process' threads are being traced automatically.
  return (bool)m_process.GetThreadByID(tid);
}

llvm::Error IntelPTMultiCoreTrace::TraceStart(lldb::tid_t tid) {
  // All the process' threads are being traced automatically.
  if (!TracesThread(tid))
    return createStringError(
        inconvertibleErrorCode(),
        "Thread %" PRIu64 " is not part of the target process", tid);
  return Error::success();
}

Error IntelPTMultiCoreTrace::TraceStop(lldb::tid_t tid) {
  return createStringError(inconvertibleErrorCode(),
                           "Can't stop tracing an individual thread when "
                           "per-cpu process tracing is enabled.");
}

Expected<std::optional<std::vector<uint8_t>>>
IntelPTMultiCoreTrace::TryGetBinaryData(
    const TraceGetBinaryDataRequest &request) {
  if (!request.cpu_id)
    return std::nullopt;
  auto it = m_traces_per_core.find(*request.cpu_id);
  if (it == m_traces_per_core.end())
    return createStringError(
        inconvertibleErrorCode(),
        formatv("Core {0} is not being traced", *request.cpu_id));

  if (request.kind == IntelPTDataKinds::kIptTrace)
    return it->second.first.GetIptTrace();
  if (request.kind == IntelPTDataKinds::kPerfContextSwitchTrace)
    return it->second.second.GetReadOnlyDataBuffer();
  return std::nullopt;
}
