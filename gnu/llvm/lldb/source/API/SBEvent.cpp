//===-- SBEvent.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBEvent.h"
#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBStream.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/Breakpoint/Breakpoint.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

SBEvent::SBEvent() { LLDB_INSTRUMENT_VA(this); }

SBEvent::SBEvent(uint32_t event_type, const char *cstr, uint32_t cstr_len)
    : m_event_sp(new Event(
          event_type, new EventDataBytes(llvm::StringRef(cstr, cstr_len)))),
      m_opaque_ptr(m_event_sp.get()) {
  LLDB_INSTRUMENT_VA(this, event_type, cstr, cstr_len);
}

SBEvent::SBEvent(EventSP &event_sp)
    : m_event_sp(event_sp), m_opaque_ptr(event_sp.get()) {
  LLDB_INSTRUMENT_VA(this, event_sp);
}

SBEvent::SBEvent(Event *event_ptr) : m_opaque_ptr(event_ptr) {
  LLDB_INSTRUMENT_VA(this, event_ptr);
}

SBEvent::SBEvent(const SBEvent &rhs)
    : m_event_sp(rhs.m_event_sp), m_opaque_ptr(rhs.m_opaque_ptr) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const SBEvent &SBEvent::operator=(const SBEvent &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_event_sp = rhs.m_event_sp;
    m_opaque_ptr = rhs.m_opaque_ptr;
  }
  return *this;
}

SBEvent::~SBEvent() = default;

const char *SBEvent::GetDataFlavor() {
  LLDB_INSTRUMENT_VA(this);

  Event *lldb_event = get();
  if (lldb_event) {
    EventData *event_data = lldb_event->GetData();
    if (event_data)
      return ConstString(lldb_event->GetData()->GetFlavor()).GetCString();
  }
  return nullptr;
}

uint32_t SBEvent::GetType() const {
  LLDB_INSTRUMENT_VA(this);

  const Event *lldb_event = get();
  uint32_t event_type = 0;
  if (lldb_event)
    event_type = lldb_event->GetType();


  return event_type;
}

SBBroadcaster SBEvent::GetBroadcaster() const {
  LLDB_INSTRUMENT_VA(this);

  SBBroadcaster broadcaster;
  const Event *lldb_event = get();
  if (lldb_event)
    broadcaster.reset(lldb_event->GetBroadcaster(), false);
  return broadcaster;
}

const char *SBEvent::GetBroadcasterClass() const {
  LLDB_INSTRUMENT_VA(this);

  const Event *lldb_event = get();
  if (lldb_event)
    return ConstString(lldb_event->GetBroadcaster()->GetBroadcasterClass())
        .AsCString();
  else
    return "unknown class";
}

bool SBEvent::BroadcasterMatchesPtr(const SBBroadcaster *broadcaster) {
  LLDB_INSTRUMENT_VA(this, broadcaster);

  if (broadcaster)
    return BroadcasterMatchesRef(*broadcaster);
  return false;
}

bool SBEvent::BroadcasterMatchesRef(const SBBroadcaster &broadcaster) {
  LLDB_INSTRUMENT_VA(this, broadcaster);

  Event *lldb_event = get();
  bool success = false;
  if (lldb_event)
    success = lldb_event->BroadcasterIs(broadcaster.get());


  return success;
}

void SBEvent::Clear() {
  LLDB_INSTRUMENT_VA(this);

  Event *lldb_event = get();
  if (lldb_event)
    lldb_event->Clear();
}

EventSP &SBEvent::GetSP() const { return m_event_sp; }

Event *SBEvent::get() const {
  // There is a dangerous accessor call GetSharedPtr which can be used, so if
  // we have anything valid in m_event_sp, we must use that since if it gets
  // used by a function that puts something in there, then it won't update
  // m_opaque_ptr...
  if (m_event_sp)
    m_opaque_ptr = m_event_sp.get();

  return m_opaque_ptr;
}

void SBEvent::reset(EventSP &event_sp) {
  m_event_sp = event_sp;
  m_opaque_ptr = m_event_sp.get();
}

void SBEvent::reset(Event *event_ptr) {
  m_opaque_ptr = event_ptr;
  m_event_sp.reset();
}

bool SBEvent::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBEvent::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  // Do NOT use m_opaque_ptr directly!!! Must use the SBEvent::get() accessor.
  // See comments in SBEvent::get()....
  return SBEvent::get() != nullptr;
}

const char *SBEvent::GetCStringFromEvent(const SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  return ConstString(static_cast<const char *>(
                         EventDataBytes::GetBytesFromEvent(event.get())))
      .GetCString();
}

bool SBEvent::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  if (get()) {
    m_opaque_ptr->Dump(&strm);
  } else
    strm.PutCString("No value");

  return true;
}

bool SBEvent::GetDescription(SBStream &description) const {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  if (get()) {
    m_opaque_ptr->Dump(&strm);
  } else
    strm.PutCString("No value");

  return true;
}
