//===-- SBBroadcaster.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBListener.h"

using namespace lldb;
using namespace lldb_private;

SBBroadcaster::SBBroadcaster() { LLDB_INSTRUMENT_VA(this); }

SBBroadcaster::SBBroadcaster(const char *name)
    : m_opaque_sp(new Broadcaster(nullptr, name)) {
  LLDB_INSTRUMENT_VA(this, name);

  m_opaque_ptr = m_opaque_sp.get();
}

SBBroadcaster::SBBroadcaster(lldb_private::Broadcaster *broadcaster, bool owns)
    : m_opaque_sp(owns ? broadcaster : nullptr), m_opaque_ptr(broadcaster) {}

SBBroadcaster::SBBroadcaster(const SBBroadcaster &rhs)
    : m_opaque_sp(rhs.m_opaque_sp), m_opaque_ptr(rhs.m_opaque_ptr) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const SBBroadcaster &SBBroadcaster::operator=(const SBBroadcaster &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
    m_opaque_ptr = rhs.m_opaque_ptr;
  }
  return *this;
}

SBBroadcaster::~SBBroadcaster() { reset(nullptr, false); }

void SBBroadcaster::BroadcastEventByType(uint32_t event_type, bool unique) {
  LLDB_INSTRUMENT_VA(this, event_type, unique);

  if (m_opaque_ptr == nullptr)
    return;

  if (unique)
    m_opaque_ptr->BroadcastEventIfUnique(event_type);
  else
    m_opaque_ptr->BroadcastEvent(event_type);
}

void SBBroadcaster::BroadcastEvent(const SBEvent &event, bool unique) {
  LLDB_INSTRUMENT_VA(this, event, unique);

  if (m_opaque_ptr == nullptr)
    return;

  EventSP event_sp = event.GetSP();
  if (unique)
    m_opaque_ptr->BroadcastEventIfUnique(event_sp);
  else
    m_opaque_ptr->BroadcastEvent(event_sp);
}

void SBBroadcaster::AddInitialEventsToListener(const SBListener &listener,
                                               uint32_t requested_events) {
  LLDB_INSTRUMENT_VA(this, listener, requested_events);

  if (m_opaque_ptr)
    m_opaque_ptr->AddInitialEventsToListener(listener.m_opaque_sp,
                                             requested_events);
}

uint32_t SBBroadcaster::AddListener(const SBListener &listener,
                                    uint32_t event_mask) {
  LLDB_INSTRUMENT_VA(this, listener, event_mask);

  if (m_opaque_ptr)
    return m_opaque_ptr->AddListener(listener.m_opaque_sp, event_mask);
  return 0;
}

const char *SBBroadcaster::GetName() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return ConstString(m_opaque_ptr->GetBroadcasterName()).GetCString();
  return nullptr;
}

bool SBBroadcaster::EventTypeHasListeners(uint32_t event_type) {
  LLDB_INSTRUMENT_VA(this, event_type);

  if (m_opaque_ptr)
    return m_opaque_ptr->EventTypeHasListeners(event_type);
  return false;
}

bool SBBroadcaster::RemoveListener(const SBListener &listener,
                                   uint32_t event_mask) {
  LLDB_INSTRUMENT_VA(this, listener, event_mask);

  if (m_opaque_ptr)
    return m_opaque_ptr->RemoveListener(listener.m_opaque_sp, event_mask);
  return false;
}

Broadcaster *SBBroadcaster::get() const { return m_opaque_ptr; }

void SBBroadcaster::reset(Broadcaster *broadcaster, bool owns) {
  if (owns)
    m_opaque_sp.reset(broadcaster);
  else
    m_opaque_sp.reset();
  m_opaque_ptr = broadcaster;
}

bool SBBroadcaster::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBBroadcaster::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_ptr != nullptr;
}

void SBBroadcaster::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_sp.reset();
  m_opaque_ptr = nullptr;
}

bool SBBroadcaster::operator==(const SBBroadcaster &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_ptr == rhs.m_opaque_ptr;
}

bool SBBroadcaster::operator!=(const SBBroadcaster &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_ptr != rhs.m_opaque_ptr;
}

bool SBBroadcaster::operator<(const SBBroadcaster &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_ptr < rhs.m_opaque_ptr;
}
