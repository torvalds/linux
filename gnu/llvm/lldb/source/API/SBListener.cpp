//===-- SBListener.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBListener.h"
#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

SBListener::SBListener() { LLDB_INSTRUMENT_VA(this); }

SBListener::SBListener(const char *name)
    : m_opaque_sp(Listener::MakeListener(name)) {
  LLDB_INSTRUMENT_VA(this, name);
}

SBListener::SBListener(const SBListener &rhs) : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const lldb::SBListener &SBListener::operator=(const lldb::SBListener &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
    m_unused_ptr = nullptr;
  }
  return *this;
}

SBListener::SBListener(const lldb::ListenerSP &listener_sp)
    : m_opaque_sp(listener_sp) {}

SBListener::~SBListener() = default;

bool SBListener::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBListener::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp != nullptr;
}

void SBListener::AddEvent(const SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, event);

  EventSP &event_sp = event.GetSP();
  if (event_sp)
    m_opaque_sp->AddEvent(event_sp);
}

void SBListener::Clear() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    m_opaque_sp->Clear();
}

uint32_t SBListener::StartListeningForEventClass(SBDebugger &debugger,
                                                 const char *broadcaster_class,
                                                 uint32_t event_mask) {
  LLDB_INSTRUMENT_VA(this, debugger, broadcaster_class, event_mask);

  if (m_opaque_sp) {
    Debugger *lldb_debugger = debugger.get();
    if (!lldb_debugger)
      return 0;
    BroadcastEventSpec event_spec(ConstString(broadcaster_class), event_mask);
    return m_opaque_sp->StartListeningForEventSpec(
        lldb_debugger->GetBroadcasterManager(), event_spec);
  } else
    return 0;
}

bool SBListener::StopListeningForEventClass(SBDebugger &debugger,
                                            const char *broadcaster_class,
                                            uint32_t event_mask) {
  LLDB_INSTRUMENT_VA(this, debugger, broadcaster_class, event_mask);

  if (m_opaque_sp) {
    Debugger *lldb_debugger = debugger.get();
    if (!lldb_debugger)
      return false;
    BroadcastEventSpec event_spec(ConstString(broadcaster_class), event_mask);
    return m_opaque_sp->StopListeningForEventSpec(
        lldb_debugger->GetBroadcasterManager(), event_spec);
  } else
    return false;
}

uint32_t SBListener::StartListeningForEvents(const SBBroadcaster &broadcaster,
                                             uint32_t event_mask) {
  LLDB_INSTRUMENT_VA(this, broadcaster, event_mask);

  uint32_t acquired_event_mask = 0;
  if (m_opaque_sp && broadcaster.IsValid()) {
    acquired_event_mask =
        m_opaque_sp->StartListeningForEvents(broadcaster.get(), event_mask);
  }

  return acquired_event_mask;
}

bool SBListener::StopListeningForEvents(const SBBroadcaster &broadcaster,
                                        uint32_t event_mask) {
  LLDB_INSTRUMENT_VA(this, broadcaster, event_mask);

  if (m_opaque_sp && broadcaster.IsValid()) {
    return m_opaque_sp->StopListeningForEvents(broadcaster.get(), event_mask);
  }
  return false;
}

bool SBListener::WaitForEvent(uint32_t timeout_secs, SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, timeout_secs, event);

  bool success = false;

  if (m_opaque_sp) {
    Timeout<std::micro> timeout(std::nullopt);
    if (timeout_secs != UINT32_MAX) {
      assert(timeout_secs != 0); // Take this out after all calls with timeout
                                 // set to zero have been removed....
      timeout = std::chrono::seconds(timeout_secs);
    }
    EventSP event_sp;
    if (m_opaque_sp->GetEvent(event_sp, timeout)) {
      event.reset(event_sp);
      success = true;
    }
  }

  if (!success)
    event.reset(nullptr);
  return success;
}

bool SBListener::WaitForEventForBroadcaster(uint32_t num_seconds,
                                            const SBBroadcaster &broadcaster,
                                            SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, num_seconds, broadcaster, event);

  if (m_opaque_sp && broadcaster.IsValid()) {
    Timeout<std::micro> timeout(std::nullopt);
    if (num_seconds != UINT32_MAX)
      timeout = std::chrono::seconds(num_seconds);
    EventSP event_sp;
    if (m_opaque_sp->GetEventForBroadcaster(broadcaster.get(), event_sp,
                                            timeout)) {
      event.reset(event_sp);
      return true;
    }
  }
  event.reset(nullptr);
  return false;
}

bool SBListener::WaitForEventForBroadcasterWithType(
    uint32_t num_seconds, const SBBroadcaster &broadcaster,
    uint32_t event_type_mask, SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, num_seconds, broadcaster, event_type_mask, event);

  if (m_opaque_sp && broadcaster.IsValid()) {
    Timeout<std::micro> timeout(std::nullopt);
    if (num_seconds != UINT32_MAX)
      timeout = std::chrono::seconds(num_seconds);
    EventSP event_sp;
    if (m_opaque_sp->GetEventForBroadcasterWithType(
            broadcaster.get(), event_type_mask, event_sp, timeout)) {
      event.reset(event_sp);
      return true;
    }
  }
  event.reset(nullptr);
  return false;
}

bool SBListener::PeekAtNextEvent(SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, event);

  if (m_opaque_sp) {
    event.reset(m_opaque_sp->PeekAtNextEvent());
    return event.IsValid();
  }
  event.reset(nullptr);
  return false;
}

bool SBListener::PeekAtNextEventForBroadcaster(const SBBroadcaster &broadcaster,
                                               SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, broadcaster, event);

  if (m_opaque_sp && broadcaster.IsValid()) {
    event.reset(m_opaque_sp->PeekAtNextEventForBroadcaster(broadcaster.get()));
    return event.IsValid();
  }
  event.reset(nullptr);
  return false;
}

bool SBListener::PeekAtNextEventForBroadcasterWithType(
    const SBBroadcaster &broadcaster, uint32_t event_type_mask,
    SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, broadcaster, event_type_mask, event);

  if (m_opaque_sp && broadcaster.IsValid()) {
    event.reset(m_opaque_sp->PeekAtNextEventForBroadcasterWithType(
        broadcaster.get(), event_type_mask));
    return event.IsValid();
  }
  event.reset(nullptr);
  return false;
}

bool SBListener::GetNextEvent(SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, event);

  if (m_opaque_sp) {
    EventSP event_sp;
    if (m_opaque_sp->GetEvent(event_sp, std::chrono::seconds(0))) {
      event.reset(event_sp);
      return true;
    }
  }
  event.reset(nullptr);
  return false;
}

bool SBListener::GetNextEventForBroadcaster(const SBBroadcaster &broadcaster,
                                            SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, broadcaster, event);

  if (m_opaque_sp && broadcaster.IsValid()) {
    EventSP event_sp;
    if (m_opaque_sp->GetEventForBroadcaster(broadcaster.get(), event_sp,
                                            std::chrono::seconds(0))) {
      event.reset(event_sp);
      return true;
    }
  }
  event.reset(nullptr);
  return false;
}

bool SBListener::GetNextEventForBroadcasterWithType(
    const SBBroadcaster &broadcaster, uint32_t event_type_mask,
    SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, broadcaster, event_type_mask, event);

  if (m_opaque_sp && broadcaster.IsValid()) {
    EventSP event_sp;
    if (m_opaque_sp->GetEventForBroadcasterWithType(broadcaster.get(),
                                                    event_type_mask, event_sp,
                                                    std::chrono::seconds(0))) {
      event.reset(event_sp);
      return true;
    }
  }
  event.reset(nullptr);
  return false;
}

bool SBListener::HandleBroadcastEvent(const SBEvent &event) {
  LLDB_INSTRUMENT_VA(this, event);

  if (m_opaque_sp)
    return m_opaque_sp->HandleBroadcastEvent(event.GetSP());
  return false;
}

lldb::ListenerSP SBListener::GetSP() { return m_opaque_sp; }

Listener *SBListener::operator->() const { return m_opaque_sp.get(); }

Listener *SBListener::get() const { return m_opaque_sp.get(); }

void SBListener::reset(ListenerSP listener_sp) {
  m_opaque_sp = listener_sp;
  m_unused_ptr = nullptr;
}
