//===-- TraceIntelPTMultiCpuDecoder.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TraceIntelPTMultiCpuDecoder.h"
#include "TraceIntelPT.h"
#include "llvm/Support/Error.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::trace_intel_pt;
using namespace llvm;

TraceIntelPTMultiCpuDecoder::TraceIntelPTMultiCpuDecoder(
    TraceIntelPTSP trace_sp)
    : m_trace_wp(trace_sp) {
  for (Process *proc : trace_sp->GetAllProcesses()) {
    for (ThreadSP thread_sp : proc->GetThreadList().Threads()) {
      m_tids.insert(thread_sp->GetID());
    }
  }
}

TraceIntelPTSP TraceIntelPTMultiCpuDecoder::GetTrace() {
  return m_trace_wp.lock();
}

bool TraceIntelPTMultiCpuDecoder::TracesThread(lldb::tid_t tid) const {
  return m_tids.count(tid);
}

Expected<std::optional<uint64_t>> TraceIntelPTMultiCpuDecoder::FindLowestTSC() {
  std::optional<uint64_t> lowest_tsc;
  TraceIntelPTSP trace_sp = GetTrace();

  Error err = GetTrace()->OnAllCpusBinaryDataRead(
      IntelPTDataKinds::kIptTrace,
      [&](const DenseMap<cpu_id_t, ArrayRef<uint8_t>> &buffers) -> Error {
        for (auto &cpu_id_to_buffer : buffers) {
          Expected<std::optional<uint64_t>> tsc =
              FindLowestTSCInTrace(*trace_sp, cpu_id_to_buffer.second);
          if (!tsc)
            return tsc.takeError();
          if (*tsc && (!lowest_tsc || *lowest_tsc > **tsc))
            lowest_tsc = **tsc;
        }
        return Error::success();
      });
  if (err)
    return std::move(err);
  return lowest_tsc;
}

Expected<DecodedThreadSP> TraceIntelPTMultiCpuDecoder::Decode(Thread &thread) {
  if (Error err = CorrelateContextSwitchesAndIntelPtTraces())
    return std::move(err);

  TraceIntelPTSP trace_sp = GetTrace();

  return trace_sp->GetThreadTimer(thread.GetID())
      .TimeTask("Decoding instructions", [&]() -> Expected<DecodedThreadSP> {
        auto it = m_decoded_threads.find(thread.GetID());
        if (it != m_decoded_threads.end())
          return it->second;

        DecodedThreadSP decoded_thread_sp = std::make_shared<DecodedThread>(
            thread.shared_from_this(), trace_sp->GetPerfZeroTscConversion());

        Error err = trace_sp->OnAllCpusBinaryDataRead(
            IntelPTDataKinds::kIptTrace,
            [&](const DenseMap<cpu_id_t, ArrayRef<uint8_t>> &buffers) -> Error {
              auto it =
                  m_continuous_executions_per_thread->find(thread.GetID());
              if (it != m_continuous_executions_per_thread->end())
                return DecodeSystemWideTraceForThread(
                    *decoded_thread_sp, *trace_sp, buffers, it->second);

              return Error::success();
            });
        if (err)
          return std::move(err);

        m_decoded_threads.try_emplace(thread.GetID(), decoded_thread_sp);
        return decoded_thread_sp;
      });
}

static Expected<std::vector<PSBBlock>> GetPSBBlocksForCPU(TraceIntelPT &trace,
                                                          cpu_id_t cpu_id) {
  std::vector<PSBBlock> psb_blocks;
  Error err = trace.OnCpuBinaryDataRead(
      cpu_id, IntelPTDataKinds::kIptTrace,
      [&](ArrayRef<uint8_t> data) -> Error {
        Expected<std::vector<PSBBlock>> split_trace =
            SplitTraceIntoPSBBlock(trace, data, /*expect_tscs=*/true);
        if (!split_trace)
          return split_trace.takeError();

        psb_blocks = std::move(*split_trace);
        return Error::success();
      });
  if (err)
    return std::move(err);
  return psb_blocks;
}

Expected<DenseMap<lldb::tid_t, std::vector<IntelPTThreadContinousExecution>>>
TraceIntelPTMultiCpuDecoder::DoCorrelateContextSwitchesAndIntelPtTraces() {
  DenseMap<lldb::tid_t, std::vector<IntelPTThreadContinousExecution>>
      continuous_executions_per_thread;
  TraceIntelPTSP trace_sp = GetTrace();

  std::optional<LinuxPerfZeroTscConversion> conv_opt =
      trace_sp->GetPerfZeroTscConversion();
  if (!conv_opt)
    return createStringError(
        inconvertibleErrorCode(),
        "TSC to nanoseconds conversion values were not found");

  LinuxPerfZeroTscConversion tsc_conversion = *conv_opt;

  for (cpu_id_t cpu_id : trace_sp->GetTracedCpus()) {
    Expected<std::vector<PSBBlock>> psb_blocks =
        GetPSBBlocksForCPU(*trace_sp, cpu_id);
    if (!psb_blocks)
      return psb_blocks.takeError();

    m_total_psb_blocks += psb_blocks->size();
    // We'll be iterating through the thread continuous executions and the intel
    // pt subtraces sorted by time.
    auto it = psb_blocks->begin();
    auto on_new_thread_execution =
        [&](const ThreadContinuousExecution &thread_execution) {
          IntelPTThreadContinousExecution execution(thread_execution);

          for (; it != psb_blocks->end() &&
                 *it->tsc < thread_execution.GetEndTSC();
               it++) {
            if (*it->tsc > thread_execution.GetStartTSC()) {
              execution.psb_blocks.push_back(*it);
            } else {
              m_unattributed_psb_blocks++;
            }
          }
          continuous_executions_per_thread[thread_execution.tid].push_back(
              execution);
        };
    Error err = trace_sp->OnCpuBinaryDataRead(
        cpu_id, IntelPTDataKinds::kPerfContextSwitchTrace,
        [&](ArrayRef<uint8_t> data) -> Error {
          Expected<std::vector<ThreadContinuousExecution>> executions =
              DecodePerfContextSwitchTrace(data, cpu_id, tsc_conversion);
          if (!executions)
            return executions.takeError();
          for (const ThreadContinuousExecution &exec : *executions)
            on_new_thread_execution(exec);
          return Error::success();
        });
    if (err)
      return std::move(err);

    m_unattributed_psb_blocks += psb_blocks->end() - it;
  }
  // We now sort the executions of each thread to have them ready for
  // instruction decoding
  for (auto &tid_executions : continuous_executions_per_thread)
    std::sort(tid_executions.second.begin(), tid_executions.second.end());

  return continuous_executions_per_thread;
}

Error TraceIntelPTMultiCpuDecoder::CorrelateContextSwitchesAndIntelPtTraces() {
  if (m_setup_error)
    return createStringError(inconvertibleErrorCode(), m_setup_error->c_str());

  if (m_continuous_executions_per_thread)
    return Error::success();

  Error err = GetTrace()->GetGlobalTimer().TimeTask(
      "Context switch and Intel PT traces correlation", [&]() -> Error {
        if (auto correlation = DoCorrelateContextSwitchesAndIntelPtTraces()) {
          m_continuous_executions_per_thread.emplace(std::move(*correlation));
          return Error::success();
        } else {
          return correlation.takeError();
        }
      });
  if (err) {
    m_setup_error = toString(std::move(err));
    return createStringError(inconvertibleErrorCode(), m_setup_error->c_str());
  }
  return Error::success();
}

size_t TraceIntelPTMultiCpuDecoder::GetNumContinuousExecutionsForThread(
    lldb::tid_t tid) const {
  if (!m_continuous_executions_per_thread)
    return 0;
  auto it = m_continuous_executions_per_thread->find(tid);
  if (it == m_continuous_executions_per_thread->end())
    return 0;
  return it->second.size();
}

size_t TraceIntelPTMultiCpuDecoder::GetTotalContinuousExecutionsCount() const {
  if (!m_continuous_executions_per_thread)
    return 0;
  size_t count = 0;
  for (const auto &kv : *m_continuous_executions_per_thread)
    count += kv.second.size();
  return count;
}

size_t
TraceIntelPTMultiCpuDecoder::GePSBBlocksCountForThread(lldb::tid_t tid) const {
  if (!m_continuous_executions_per_thread)
    return 0;
  size_t count = 0;
  auto it = m_continuous_executions_per_thread->find(tid);
  if (it == m_continuous_executions_per_thread->end())
    return 0;
  for (const IntelPTThreadContinousExecution &execution : it->second)
    count += execution.psb_blocks.size();
  return count;
}

size_t TraceIntelPTMultiCpuDecoder::GetUnattributedPSBBlocksCount() const {
  return m_unattributed_psb_blocks;
}

size_t TraceIntelPTMultiCpuDecoder::GetTotalPSBBlocksCount() const {
  return m_total_psb_blocks;
}
