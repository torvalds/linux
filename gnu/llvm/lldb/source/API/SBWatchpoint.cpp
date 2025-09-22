//===-- SBWatchpoint.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBWatchpoint.h"
#include "lldb/API/SBAddress.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBStream.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Breakpoint/WatchpointList.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Stream.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-types.h"

using namespace lldb;
using namespace lldb_private;

SBWatchpoint::SBWatchpoint() { LLDB_INSTRUMENT_VA(this); }

SBWatchpoint::SBWatchpoint(const lldb::WatchpointSP &wp_sp)
    : m_opaque_wp(wp_sp) {
  LLDB_INSTRUMENT_VA(this, wp_sp);
}

SBWatchpoint::SBWatchpoint(const SBWatchpoint &rhs)
    : m_opaque_wp(rhs.m_opaque_wp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const SBWatchpoint &SBWatchpoint::operator=(const SBWatchpoint &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_wp = rhs.m_opaque_wp;
  return *this;
}

SBWatchpoint::~SBWatchpoint() = default;

watch_id_t SBWatchpoint::GetID() {
  LLDB_INSTRUMENT_VA(this);

  watch_id_t watch_id = LLDB_INVALID_WATCH_ID;
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp)
    watch_id = watchpoint_sp->GetID();

  return watch_id;
}

bool SBWatchpoint::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBWatchpoint::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return bool(m_opaque_wp.lock());
}

bool SBWatchpoint::operator==(const SBWatchpoint &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return GetSP() == rhs.GetSP();
}

bool SBWatchpoint::operator!=(const SBWatchpoint &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return !(*this == rhs);
}

SBError SBWatchpoint::GetError() {
  LLDB_INSTRUMENT_VA(this);

  SBError sb_error;
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    sb_error.SetError(watchpoint_sp->GetError());
  }
  return sb_error;
}

int32_t SBWatchpoint::GetHardwareIndex() {
  LLDB_INSTRUMENT_VA(this);

  // For processes using gdb remote protocol,
  // we cannot determine the hardware breakpoint
  // index reliably; providing possibly correct
  // guesses is not useful to anyone.
  return -1;
}

addr_t SBWatchpoint::GetWatchAddress() {
  LLDB_INSTRUMENT_VA(this);

  addr_t ret_addr = LLDB_INVALID_ADDRESS;

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    ret_addr = watchpoint_sp->GetLoadAddress();
  }

  return ret_addr;
}

size_t SBWatchpoint::GetWatchSize() {
  LLDB_INSTRUMENT_VA(this);

  size_t watch_size = 0;

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    watch_size = watchpoint_sp->GetByteSize();
  }

  return watch_size;
}

void SBWatchpoint::SetEnabled(bool enabled) {
  LLDB_INSTRUMENT_VA(this, enabled);

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    Target &target = watchpoint_sp->GetTarget();
    std::lock_guard<std::recursive_mutex> guard(target.GetAPIMutex());
    ProcessSP process_sp = target.GetProcessSP();
    const bool notify = true;
    if (process_sp) {
      if (enabled)
        process_sp->EnableWatchpoint(watchpoint_sp, notify);
      else
        process_sp->DisableWatchpoint(watchpoint_sp, notify);
    } else {
      watchpoint_sp->SetEnabled(enabled, notify);
    }
  }
}

bool SBWatchpoint::IsEnabled() {
  LLDB_INSTRUMENT_VA(this);

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    return watchpoint_sp->IsEnabled();
  } else
    return false;
}

uint32_t SBWatchpoint::GetHitCount() {
  LLDB_INSTRUMENT_VA(this);

  uint32_t count = 0;
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    count = watchpoint_sp->GetHitCount();
  }

  return count;
}

uint32_t SBWatchpoint::GetIgnoreCount() {
  LLDB_INSTRUMENT_VA(this);

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    return watchpoint_sp->GetIgnoreCount();
  } else
    return 0;
}

void SBWatchpoint::SetIgnoreCount(uint32_t n) {
  LLDB_INSTRUMENT_VA(this, n);

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    watchpoint_sp->SetIgnoreCount(n);
  }
}

const char *SBWatchpoint::GetCondition() {
  LLDB_INSTRUMENT_VA(this);

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (!watchpoint_sp)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      watchpoint_sp->GetTarget().GetAPIMutex());
  return ConstString(watchpoint_sp->GetConditionText()).GetCString();
}

void SBWatchpoint::SetCondition(const char *condition) {
  LLDB_INSTRUMENT_VA(this, condition);

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    watchpoint_sp->SetCondition(condition);
  }
}

bool SBWatchpoint::GetDescription(SBStream &description,
                                  DescriptionLevel level) {
  LLDB_INSTRUMENT_VA(this, description, level);

  Stream &strm = description.ref();

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    watchpoint_sp->GetDescription(&strm, level);
    strm.EOL();
  } else
    strm.PutCString("No value");

  return true;
}

void SBWatchpoint::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_wp.reset();
}

lldb::WatchpointSP SBWatchpoint::GetSP() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_wp.lock();
}

void SBWatchpoint::SetSP(const lldb::WatchpointSP &sp) {
  LLDB_INSTRUMENT_VA(this, sp);

  m_opaque_wp = sp;
}

bool SBWatchpoint::EventIsWatchpointEvent(const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  return Watchpoint::WatchpointEventData::GetEventDataFromEvent(event.get()) !=
         nullptr;
}

WatchpointEventType
SBWatchpoint::GetWatchpointEventTypeFromEvent(const SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  if (event.IsValid())
    return Watchpoint::WatchpointEventData::GetWatchpointEventTypeFromEvent(
        event.GetSP());
  return eWatchpointEventTypeInvalidType;
}

SBWatchpoint SBWatchpoint::GetWatchpointFromEvent(const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  SBWatchpoint sb_watchpoint;
  if (event.IsValid())
    sb_watchpoint =
        Watchpoint::WatchpointEventData::GetWatchpointFromEvent(event.GetSP());
  return sb_watchpoint;
}

lldb::SBType SBWatchpoint::GetType() {
  LLDB_INSTRUMENT_VA(this);

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    const CompilerType &type = watchpoint_sp->GetCompilerType();
    return lldb::SBType(type);
  }
  return lldb::SBType();
}

WatchpointValueKind SBWatchpoint::GetWatchValueKind() {
  LLDB_INSTRUMENT_VA(this);

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());
    if (watchpoint_sp->IsWatchVariable())
      return WatchpointValueKind::eWatchPointValueKindVariable;
    return WatchpointValueKind::eWatchPointValueKindExpression;
  }
  return WatchpointValueKind::eWatchPointValueKindInvalid;
}

const char *SBWatchpoint::GetWatchSpec() {
  LLDB_INSTRUMENT_VA(this);

  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (!watchpoint_sp)
    return nullptr;

  std::lock_guard<std::recursive_mutex> guard(
      watchpoint_sp->GetTarget().GetAPIMutex());
  // Store the result of `GetWatchSpec()` as a ConstString
  // so that the C string we return has a sufficiently long
  // lifetime. Note this a memory leak but should be fairly
  // low impact.
  return ConstString(watchpoint_sp->GetWatchSpec()).AsCString();
}

bool SBWatchpoint::IsWatchingReads() {
  LLDB_INSTRUMENT_VA(this);
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());

    return watchpoint_sp->WatchpointRead();
  }

  return false;
}

bool SBWatchpoint::IsWatchingWrites() {
  LLDB_INSTRUMENT_VA(this);
  lldb::WatchpointSP watchpoint_sp(GetSP());
  if (watchpoint_sp) {
    std::lock_guard<std::recursive_mutex> guard(
        watchpoint_sp->GetTarget().GetAPIMutex());

    return watchpoint_sp->WatchpointWrite() ||
           watchpoint_sp->WatchpointModify();
  }

  return false;
}
