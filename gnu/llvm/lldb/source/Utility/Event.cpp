//===-- Event.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Event.h"

#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/StringExtras.h"

#include <algorithm>

#include <cctype>

using namespace lldb;
using namespace lldb_private;

#pragma mark -
#pragma mark Event

// Event functions

Event::Event(Broadcaster *broadcaster, uint32_t event_type, EventData *data)
    : m_broadcaster_wp(broadcaster->GetBroadcasterImpl()), m_type(event_type),
      m_data_sp(data) {}

Event::Event(Broadcaster *broadcaster, uint32_t event_type,
             const EventDataSP &event_data_sp)
    : m_broadcaster_wp(broadcaster->GetBroadcasterImpl()), m_type(event_type),
      m_data_sp(event_data_sp) {}

Event::Event(uint32_t event_type, EventData *data)
    : m_broadcaster_wp(), m_type(event_type), m_data_sp(data) {}

Event::Event(uint32_t event_type, const EventDataSP &event_data_sp)
    : m_broadcaster_wp(), m_type(event_type), m_data_sp(event_data_sp) {}

Event::~Event() = default;

void Event::Dump(Stream *s) const {
  Broadcaster *broadcaster;
  Broadcaster::BroadcasterImplSP broadcaster_impl_sp(m_broadcaster_wp.lock());
  if (broadcaster_impl_sp)
    broadcaster = broadcaster_impl_sp->GetBroadcaster();
  else
    broadcaster = nullptr;

  if (broadcaster) {
    StreamString event_name;
    if (broadcaster->GetEventNames(event_name, m_type, false))
      s->Printf("%p Event: broadcaster = %p (%s), type = 0x%8.8x (%s), data = ",
                static_cast<const void *>(this),
                static_cast<void *>(broadcaster),
                broadcaster->GetBroadcasterName().c_str(), m_type,
                event_name.GetData());
    else
      s->Printf("%p Event: broadcaster = %p (%s), type = 0x%8.8x, data = ",
                static_cast<const void *>(this),
                static_cast<void *>(broadcaster),
                broadcaster->GetBroadcasterName().c_str(), m_type);
  } else
    s->Printf("%p Event: broadcaster = NULL, type = 0x%8.8x, data = ",
              static_cast<const void *>(this), m_type);

  if (m_data_sp) {
    s->PutChar('{');
    m_data_sp->Dump(s);
    s->PutChar('}');
  } else
    s->Printf("<NULL>");
}

void Event::DoOnRemoval() {
  std::lock_guard<std::mutex> guard(m_listeners_mutex);

  if (!m_data_sp)
    return;

  m_data_sp->DoOnRemoval(this);

  // Now that the event has been handled by the primary event Listener, forward
  // it to the other Listeners.

  EventSP me_sp = shared_from_this();
  if (m_data_sp->ForwardEventToPendingListeners(this)) {
    for (auto listener_sp : m_pending_listeners)
      listener_sp->AddEvent(me_sp);
    m_pending_listeners.clear();
  }
}

#pragma mark -
#pragma mark EventData

// EventData functions

EventData::EventData() = default;

EventData::~EventData() = default;

void EventData::Dump(Stream *s) const { s->PutCString("Generic Event Data"); }

#pragma mark -
#pragma mark EventDataBytes

// EventDataBytes functions

EventDataBytes::EventDataBytes() : m_bytes() {}

EventDataBytes::EventDataBytes(llvm::StringRef str) : m_bytes(str.str()) {}

EventDataBytes::~EventDataBytes() = default;

llvm::StringRef EventDataBytes::GetFlavorString() { return "EventDataBytes"; }

llvm::StringRef EventDataBytes::GetFlavor() const {
  return EventDataBytes::GetFlavorString();
}

void EventDataBytes::Dump(Stream *s) const {
  if (llvm::all_of(m_bytes, llvm::isPrint))
    s->Format("\"{0}\"", m_bytes);
  else
    s->Format("{0:$[ ]@[x-2]}", llvm::make_range(
                         reinterpret_cast<const uint8_t *>(m_bytes.data()),
                         reinterpret_cast<const uint8_t *>(m_bytes.data() +
                                                           m_bytes.size())));
}

const void *EventDataBytes::GetBytes() const {
  return (m_bytes.empty() ? nullptr : m_bytes.data());
}

size_t EventDataBytes::GetByteSize() const { return m_bytes.size(); }

const void *EventDataBytes::GetBytesFromEvent(const Event *event_ptr) {
  const EventDataBytes *e = GetEventDataFromEvent(event_ptr);
  if (e != nullptr)
    return e->GetBytes();
  return nullptr;
}

size_t EventDataBytes::GetByteSizeFromEvent(const Event *event_ptr) {
  const EventDataBytes *e = GetEventDataFromEvent(event_ptr);
  if (e != nullptr)
    return e->GetByteSize();
  return 0;
}

const EventDataBytes *
EventDataBytes::GetEventDataFromEvent(const Event *event_ptr) {
  if (event_ptr != nullptr) {
    const EventData *event_data = event_ptr->GetData();
    if (event_data &&
        event_data->GetFlavor() == EventDataBytes::GetFlavorString())
      return static_cast<const EventDataBytes *>(event_data);
  }
  return nullptr;
}

llvm::StringRef EventDataReceipt::GetFlavorString() {
  return "Process::ProcessEventData";
}

#pragma mark -
#pragma mark EventStructuredData

// EventDataStructuredData definitions

EventDataStructuredData::EventDataStructuredData()
    : EventData(), m_process_sp(), m_object_sp(), m_plugin_sp() {}

EventDataStructuredData::EventDataStructuredData(
    const ProcessSP &process_sp, const StructuredData::ObjectSP &object_sp,
    const lldb::StructuredDataPluginSP &plugin_sp)
    : EventData(), m_process_sp(process_sp), m_object_sp(object_sp),
      m_plugin_sp(plugin_sp) {}

EventDataStructuredData::~EventDataStructuredData() = default;

// EventDataStructuredData member functions

llvm::StringRef EventDataStructuredData::GetFlavor() const {
  return EventDataStructuredData::GetFlavorString();
}

void EventDataStructuredData::Dump(Stream *s) const {
  if (!s)
    return;

  if (m_object_sp)
    m_object_sp->Dump(*s);
}

const ProcessSP &EventDataStructuredData::GetProcess() const {
  return m_process_sp;
}

const StructuredData::ObjectSP &EventDataStructuredData::GetObject() const {
  return m_object_sp;
}

const lldb::StructuredDataPluginSP &
EventDataStructuredData::GetStructuredDataPlugin() const {
  return m_plugin_sp;
}

void EventDataStructuredData::SetProcess(const ProcessSP &process_sp) {
  m_process_sp = process_sp;
}

void EventDataStructuredData::SetObject(
    const StructuredData::ObjectSP &object_sp) {
  m_object_sp = object_sp;
}

void EventDataStructuredData::SetStructuredDataPlugin(
    const lldb::StructuredDataPluginSP &plugin_sp) {
  m_plugin_sp = plugin_sp;
}

// EventDataStructuredData static functions

const EventDataStructuredData *
EventDataStructuredData::GetEventDataFromEvent(const Event *event_ptr) {
  if (event_ptr == nullptr)
    return nullptr;

  const EventData *event_data = event_ptr->GetData();
  if (!event_data ||
      event_data->GetFlavor() != EventDataStructuredData::GetFlavorString())
    return nullptr;

  return static_cast<const EventDataStructuredData *>(event_data);
}

ProcessSP EventDataStructuredData::GetProcessFromEvent(const Event *event_ptr) {
  auto event_data = EventDataStructuredData::GetEventDataFromEvent(event_ptr);
  if (event_data)
    return event_data->GetProcess();
  else
    return ProcessSP();
}

StructuredData::ObjectSP
EventDataStructuredData::GetObjectFromEvent(const Event *event_ptr) {
  auto event_data = EventDataStructuredData::GetEventDataFromEvent(event_ptr);
  if (event_data)
    return event_data->GetObject();
  else
    return StructuredData::ObjectSP();
}

lldb::StructuredDataPluginSP
EventDataStructuredData::GetPluginFromEvent(const Event *event_ptr) {
  auto event_data = EventDataStructuredData::GetEventDataFromEvent(event_ptr);
  if (event_data)
    return event_data->GetStructuredDataPlugin();
  else
    return StructuredDataPluginSP();
}

llvm::StringRef EventDataStructuredData::GetFlavorString() {
  return "EventDataStructuredData";
}
