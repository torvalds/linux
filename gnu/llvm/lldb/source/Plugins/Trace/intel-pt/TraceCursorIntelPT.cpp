//===-- TraceCursorIntelPT.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TraceCursorIntelPT.h"
#include "DecodedThread.h"
#include "TraceIntelPT.h"
#include <cstdlib>
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::trace_intel_pt;
using namespace llvm;

TraceCursorIntelPT::TraceCursorIntelPT(
    ThreadSP thread_sp, DecodedThreadSP decoded_thread_sp,
    const std::optional<LinuxPerfZeroTscConversion> &tsc_conversion,
    std::optional<uint64_t> beginning_of_time_nanos)
    : TraceCursor(thread_sp), m_decoded_thread_sp(decoded_thread_sp),
      m_tsc_conversion(tsc_conversion),
      m_beginning_of_time_nanos(beginning_of_time_nanos) {
  Seek(0, lldb::eTraceCursorSeekTypeEnd);
}

void TraceCursorIntelPT::Next() {
  m_pos += IsForwards() ? 1 : -1;
  ClearTimingRangesIfInvalid();
}

void TraceCursorIntelPT::ClearTimingRangesIfInvalid() {
  if (m_tsc_range_calculated) {
    if (!m_tsc_range || m_pos < 0 || !m_tsc_range->InRange(m_pos)) {
      m_tsc_range = std::nullopt;
      m_tsc_range_calculated = false;
    }
  }

  if (m_nanoseconds_range_calculated) {
    if (!m_nanoseconds_range || m_pos < 0 ||
        !m_nanoseconds_range->InRange(m_pos)) {
      m_nanoseconds_range = std::nullopt;
      m_nanoseconds_range_calculated = false;
    }
  }
}

const std::optional<DecodedThread::TSCRange> &
TraceCursorIntelPT::GetTSCRange() const {
  if (!m_tsc_range_calculated) {
    m_tsc_range_calculated = true;
    m_tsc_range = m_decoded_thread_sp->GetTSCRangeByIndex(m_pos);
  }
  return m_tsc_range;
}

const std::optional<DecodedThread::NanosecondsRange> &
TraceCursorIntelPT::GetNanosecondsRange() const {
  if (!m_nanoseconds_range_calculated) {
    m_nanoseconds_range_calculated = true;
    m_nanoseconds_range =
        m_decoded_thread_sp->GetNanosecondsRangeByIndex(m_pos);
  }
  return m_nanoseconds_range;
}

bool TraceCursorIntelPT::Seek(int64_t offset,
                              lldb::TraceCursorSeekType origin) {
  switch (origin) {
  case lldb::eTraceCursorSeekTypeBeginning:
    m_pos = offset;
    break;
  case lldb::eTraceCursorSeekTypeEnd:
    m_pos = m_decoded_thread_sp->GetItemsCount() - 1 + offset;
    break;
  case lldb::eTraceCursorSeekTypeCurrent:
    m_pos += offset;
  }

  ClearTimingRangesIfInvalid();

  return HasValue();
}

bool TraceCursorIntelPT::HasValue() const {
  return m_pos >= 0 &&
         static_cast<uint64_t>(m_pos) < m_decoded_thread_sp->GetItemsCount();
}

lldb::TraceItemKind TraceCursorIntelPT::GetItemKind() const {
  return m_decoded_thread_sp->GetItemKindByIndex(m_pos);
}

llvm::StringRef TraceCursorIntelPT::GetError() const {
  return m_decoded_thread_sp->GetErrorByIndex(m_pos);
}

lldb::addr_t TraceCursorIntelPT::GetLoadAddress() const {
  return m_decoded_thread_sp->GetInstructionLoadAddress(m_pos);
}

std::optional<uint64_t> TraceCursorIntelPT::GetHWClock() const {
  if (const std::optional<DecodedThread::TSCRange> &range = GetTSCRange())
    return range->tsc;
  return std::nullopt;
}

std::optional<double> TraceCursorIntelPT::GetWallClockTime() const {
  if (const std::optional<DecodedThread::NanosecondsRange> &range =
          GetNanosecondsRange())
    return range->GetInterpolatedTime(m_pos, *m_beginning_of_time_nanos,
                                      *m_tsc_conversion);
  return std::nullopt;
}

lldb::cpu_id_t TraceCursorIntelPT::GetCPU() const {
  return m_decoded_thread_sp->GetCPUByIndex(m_pos);
}

lldb::TraceEvent TraceCursorIntelPT::GetEventType() const {
  return m_decoded_thread_sp->GetEventByIndex(m_pos);
}

bool TraceCursorIntelPT::GoToId(user_id_t id) {
  if (!HasId(id))
    return false;
  m_pos = id;
  ClearTimingRangesIfInvalid();
  return true;
}

bool TraceCursorIntelPT::HasId(lldb::user_id_t id) const {
  return id < m_decoded_thread_sp->GetItemsCount();
}

user_id_t TraceCursorIntelPT::GetId() const { return m_pos; }

std::optional<std::string> TraceCursorIntelPT::GetSyncPointMetadata() const {
  return formatv("offset = 0x{0:x}",
                 m_decoded_thread_sp->GetSyncPointOffsetByIndex(m_pos))
      .str();
}
