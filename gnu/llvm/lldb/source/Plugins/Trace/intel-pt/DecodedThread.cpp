//===-- DecodedThread.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DecodedThread.h"
#include "TraceCursorIntelPT.h"
#include <intel-pt.h>
#include <memory>
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::trace_intel_pt;
using namespace llvm;

char IntelPTError::ID;

IntelPTError::IntelPTError(int libipt_error_code, lldb::addr_t address)
    : m_libipt_error_code(libipt_error_code), m_address(address) {
  assert(libipt_error_code < 0);
}

void IntelPTError::log(llvm::raw_ostream &OS) const {
  OS << pt_errstr(pt_errcode(m_libipt_error_code));
  if (m_address != LLDB_INVALID_ADDRESS && m_address > 0)
    OS << formatv(": {0:x+16}", m_address);
}

bool DecodedThread::TSCRange::InRange(uint64_t item_index) const {
  return item_index >= first_item_index &&
         item_index < first_item_index + items_count;
}

bool DecodedThread::NanosecondsRange::InRange(uint64_t item_index) const {
  return item_index >= first_item_index &&
         item_index < first_item_index + items_count;
}

double DecodedThread::NanosecondsRange::GetInterpolatedTime(
    uint64_t item_index, uint64_t begin_of_time_nanos,
    const LinuxPerfZeroTscConversion &tsc_conversion) const {
  uint64_t items_since_last_tsc = item_index - first_item_index;

  auto interpolate = [&](uint64_t next_range_start_ns) {
    if (next_range_start_ns == nanos) {
      // If the resolution of the conversion formula is bad enough to consider
      // these two timestamps as equal, then we just increase the next one by 1
      // for correction
      next_range_start_ns++;
    }
    long double item_duration =
        static_cast<long double>(items_count) / (next_range_start_ns - nanos);
    return (nanos - begin_of_time_nanos) + items_since_last_tsc * item_duration;
  };

  if (!next_range) {
    // If this is the last TSC range, so we have to extrapolate. In this case,
    // we assume that each instruction took one TSC, which is what an
    // instruction would take if no parallelism is achieved and the frequency
    // multiplier is 1.
    return interpolate(tsc_conversion.ToNanos(tsc + items_count));
  }
  if (items_count < (next_range->tsc - tsc)) {
    // If the numbers of items in this range is less than the total TSC duration
    // of this range, i.e. each instruction taking longer than 1 TSC, then we
    // can assume that something else happened between these TSCs (e.g. a
    // context switch, change to kernel, decoding errors, etc). In this case, we
    // also assume that each instruction took 1 TSC. A proper way to improve
    // this would be to analize the next events in the trace looking for context
    // switches or trace disablement events, but for now, as we only want an
    // approximation, we keep it simple. We are also guaranteed that the time in
    // nanos of the next range is different to the current one, just because of
    // the definition of a NanosecondsRange.
    return interpolate(
        std::min(tsc_conversion.ToNanos(tsc + items_count), next_range->nanos));
  }

  // In this case, each item took less than 1 TSC, so some parallelism was
  // achieved, which is an indication that we didn't suffered of any kind of
  // interruption.
  return interpolate(next_range->nanos);
}

uint64_t DecodedThread::GetItemsCount() const { return m_item_data.size(); }

lldb::addr_t
DecodedThread::GetInstructionLoadAddress(uint64_t item_index) const {
  return std::get<lldb::addr_t>(m_item_data[item_index]);
}

lldb::addr_t
DecodedThread::GetSyncPointOffsetByIndex(uint64_t item_index) const {
  return m_psb_offsets.find(item_index)->second;
}

ThreadSP DecodedThread::GetThread() { return m_thread_sp; }

template <typename Data>
DecodedThread::TraceItemStorage &
DecodedThread::CreateNewTraceItem(lldb::TraceItemKind kind, Data &&data) {
  m_item_data.emplace_back(data);

  if (m_last_tsc)
    (*m_last_tsc)->second.items_count++;
  if (m_last_nanoseconds)
    (*m_last_nanoseconds)->second.items_count++;

  return m_item_data.back();
}

void DecodedThread::NotifySyncPoint(lldb::addr_t psb_offset) {
  m_psb_offsets.try_emplace(GetItemsCount(), psb_offset);
  AppendEvent(lldb::eTraceEventSyncPoint);
}

void DecodedThread::NotifyTsc(TSC tsc) {
  if (m_last_tsc && (*m_last_tsc)->second.tsc == tsc)
    return;
  if (m_last_tsc)
    assert(tsc >= (*m_last_tsc)->second.tsc &&
           "We can't have decreasing times");

  m_last_tsc =
      m_tscs.emplace(GetItemsCount(), TSCRange{tsc, 0, GetItemsCount()}).first;

  if (m_tsc_conversion) {
    uint64_t nanos = m_tsc_conversion->ToNanos(tsc);
    if (!m_last_nanoseconds || (*m_last_nanoseconds)->second.nanos != nanos) {
      m_last_nanoseconds =
          m_nanoseconds
              .emplace(GetItemsCount(), NanosecondsRange{nanos, tsc, nullptr, 0,
                                                         GetItemsCount()})
              .first;
      if (*m_last_nanoseconds != m_nanoseconds.begin()) {
        auto prev_range = prev(*m_last_nanoseconds);
        prev_range->second.next_range = &(*m_last_nanoseconds)->second;
      }
    }
  }
  AppendEvent(lldb::eTraceEventHWClockTick);
}

void DecodedThread::NotifyCPU(lldb::cpu_id_t cpu_id) {
  if (!m_last_cpu || *m_last_cpu != cpu_id) {
    m_cpus.emplace(GetItemsCount(), cpu_id);
    m_last_cpu = cpu_id;
    AppendEvent(lldb::eTraceEventCPUChanged);
  }
}

lldb::cpu_id_t DecodedThread::GetCPUByIndex(uint64_t item_index) const {
  auto it = m_cpus.upper_bound(item_index);
  return it == m_cpus.begin() ? LLDB_INVALID_CPU_ID : prev(it)->second;
}

std::optional<DecodedThread::TSCRange>
DecodedThread::GetTSCRangeByIndex(uint64_t item_index) const {
  auto next_it = m_tscs.upper_bound(item_index);
  if (next_it == m_tscs.begin())
    return std::nullopt;
  return prev(next_it)->second;
}

std::optional<DecodedThread::NanosecondsRange>
DecodedThread::GetNanosecondsRangeByIndex(uint64_t item_index) {
  auto next_it = m_nanoseconds.upper_bound(item_index);
  if (next_it == m_nanoseconds.begin())
    return std::nullopt;
  return prev(next_it)->second;
}

uint64_t DecodedThread::GetTotalInstructionCount() const {
  return m_insn_count;
}

void DecodedThread::AppendEvent(lldb::TraceEvent event) {
  CreateNewTraceItem(lldb::eTraceItemKindEvent, event);
  m_events_stats.RecordEvent(event);
}

void DecodedThread::AppendInstruction(const pt_insn &insn) {
  CreateNewTraceItem(lldb::eTraceItemKindInstruction, insn.ip);
  m_insn_count++;
}

void DecodedThread::AppendError(const IntelPTError &error) {
  CreateNewTraceItem(lldb::eTraceItemKindError, error.message());
  m_error_stats.RecordError(/*fatal=*/false);
}

void DecodedThread::AppendCustomError(StringRef err, bool fatal) {
  CreateNewTraceItem(lldb::eTraceItemKindError, err.str());
  m_error_stats.RecordError(fatal);
}

lldb::TraceEvent DecodedThread::GetEventByIndex(int item_index) const {
  return std::get<lldb::TraceEvent>(m_item_data[item_index]);
}

const DecodedThread::EventsStats &DecodedThread::GetEventsStats() const {
  return m_events_stats;
}

void DecodedThread::EventsStats::RecordEvent(lldb::TraceEvent event) {
  events_counts[event]++;
  total_count++;
}

uint64_t DecodedThread::ErrorStats::GetTotalCount() const {
  uint64_t total = 0;
  for (const auto &[kind, count] : libipt_errors)
    total += count;

  return total + other_errors + fatal_errors;
}

void DecodedThread::ErrorStats::RecordError(bool fatal) {
  if (fatal)
    fatal_errors++;
  else
    other_errors++;
}

void DecodedThread::ErrorStats::RecordError(int libipt_error_code) {
  libipt_errors[pt_errstr(pt_errcode(libipt_error_code))]++;
}

const DecodedThread::ErrorStats &DecodedThread::GetErrorStats() const {
  return m_error_stats;
}

lldb::TraceItemKind
DecodedThread::GetItemKindByIndex(uint64_t item_index) const {
  return std::visit(
      llvm::makeVisitor(
          [](const std::string &) { return lldb::eTraceItemKindError; },
          [](lldb::TraceEvent) { return lldb::eTraceItemKindEvent; },
          [](lldb::addr_t) { return lldb::eTraceItemKindInstruction; }),
      m_item_data[item_index]);
}

llvm::StringRef DecodedThread::GetErrorByIndex(uint64_t item_index) const {
  if (item_index >= m_item_data.size())
    return llvm::StringRef();
  return std::get<std::string>(m_item_data[item_index]);
}

DecodedThread::DecodedThread(
    ThreadSP thread_sp,
    const std::optional<LinuxPerfZeroTscConversion> &tsc_conversion)
    : m_thread_sp(thread_sp), m_tsc_conversion(tsc_conversion) {}

size_t DecodedThread::CalculateApproximateMemoryUsage() const {
  return sizeof(TraceItemStorage) * m_item_data.size() +
         (sizeof(uint64_t) + sizeof(TSC)) * m_tscs.size() +
         (sizeof(uint64_t) + sizeof(uint64_t)) * m_nanoseconds.size() +
         (sizeof(uint64_t) + sizeof(lldb::cpu_id_t)) * m_cpus.size();
}
