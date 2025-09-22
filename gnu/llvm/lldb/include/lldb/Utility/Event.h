//===-- Event.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_EVENT_H
#define LLDB_UTILITY_EVENT_H

#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Predicate.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-forward.h"

#include "llvm/ADT/StringRef.h"

#include <chrono>
#include <memory>
#include <string>

#include <cstddef>
#include <cstdint>

namespace lldb_private {
class Event;
class Stream;
}

namespace lldb_private {

// lldb::EventData
class EventData {
  friend class Event;

public:
  EventData();

  virtual ~EventData();

  virtual llvm::StringRef GetFlavor() const = 0;

  virtual Log *GetLogChannel() { return nullptr; }
  
  virtual void Dump(Stream *s) const;

private:
  /// This will be queried for a Broadcaster with a primary and some secondary
  /// listeners after the primary listener pulled the event from the event queue
  /// and ran its DoOnRemoval, right before the event is delivered.
  /// If it returns true, the event will also be forwarded to the secondary
  /// listeners, and if false, event propagation stops at the primary listener.
  /// Some broadcasters (particularly the Process broadcaster) fetch events on
  /// a private Listener, and then forward the event to the Public Listeners
  /// after some processing.  The Process broadcaster does not want to forward
  /// to the secondary listeners at the private processing stage.
  virtual bool ForwardEventToPendingListeners(Event *event_ptr) { return true; }

  virtual void DoOnRemoval(Event *event_ptr) {}

  EventData(const EventData &) = delete;
  const EventData &operator=(const EventData &) = delete;
};

// lldb::EventDataBytes
class EventDataBytes : public EventData {
public:
  // Constructors
  EventDataBytes();

  EventDataBytes(llvm::StringRef str);

  ~EventDataBytes() override;

  // Member functions
  llvm::StringRef GetFlavor() const override;

  void Dump(Stream *s) const override;

  const void *GetBytes() const;

  size_t GetByteSize() const;

  // Static functions
  static const EventDataBytes *GetEventDataFromEvent(const Event *event_ptr);

  static const void *GetBytesFromEvent(const Event *event_ptr);

  static size_t GetByteSizeFromEvent(const Event *event_ptr);

  static llvm::StringRef GetFlavorString();

private:
  std::string m_bytes;

  EventDataBytes(const EventDataBytes &) = delete;
  const EventDataBytes &operator=(const EventDataBytes &) = delete;
};

class EventDataReceipt : public EventData {
public:
  EventDataReceipt() : m_predicate(false) {}

  ~EventDataReceipt() override = default;

  static llvm::StringRef GetFlavorString();

  llvm::StringRef GetFlavor() const override { return GetFlavorString(); }

  bool WaitForEventReceived(const Timeout<std::micro> &timeout = std::nullopt) {
    return m_predicate.WaitForValueEqualTo(true, timeout);
  }

private:
  Predicate<bool> m_predicate;

  void DoOnRemoval(Event *event_ptr) override {
    m_predicate.SetValue(true, eBroadcastAlways);
  }
};

/// This class handles one or more StructuredData::Dictionary entries
/// that are raised for structured data events.

class EventDataStructuredData : public EventData {
public:
  // Constructors
  EventDataStructuredData();

  EventDataStructuredData(const lldb::ProcessSP &process_sp,
                          const StructuredData::ObjectSP &object_sp,
                          const lldb::StructuredDataPluginSP &plugin_sp);

  ~EventDataStructuredData() override;

  // Member functions
  llvm::StringRef GetFlavor() const override;

  void Dump(Stream *s) const override;

  const lldb::ProcessSP &GetProcess() const;

  const StructuredData::ObjectSP &GetObject() const;

  const lldb::StructuredDataPluginSP &GetStructuredDataPlugin() const;

  void SetProcess(const lldb::ProcessSP &process_sp);

  void SetObject(const StructuredData::ObjectSP &object_sp);

  void SetStructuredDataPlugin(const lldb::StructuredDataPluginSP &plugin_sp);

  // Static functions
  static const EventDataStructuredData *
  GetEventDataFromEvent(const Event *event_ptr);

  static lldb::ProcessSP GetProcessFromEvent(const Event *event_ptr);

  static StructuredData::ObjectSP GetObjectFromEvent(const Event *event_ptr);

  static lldb::StructuredDataPluginSP
  GetPluginFromEvent(const Event *event_ptr);

  static llvm::StringRef GetFlavorString();

private:
  lldb::ProcessSP m_process_sp;
  StructuredData::ObjectSP m_object_sp;
  lldb::StructuredDataPluginSP m_plugin_sp;

  EventDataStructuredData(const EventDataStructuredData &) = delete;
  const EventDataStructuredData &
  operator=(const EventDataStructuredData &) = delete;
};

// lldb::Event
class Event : public std::enable_shared_from_this<Event> {
  friend class Listener;
  friend class EventData;
  friend class Broadcaster::BroadcasterImpl;

public:
  Event(Broadcaster *broadcaster, uint32_t event_type,
        EventData *data = nullptr);

  Event(Broadcaster *broadcaster, uint32_t event_type,
        const lldb::EventDataSP &event_data_sp);

  Event(uint32_t event_type, EventData *data = nullptr);

  Event(uint32_t event_type, const lldb::EventDataSP &event_data_sp);

  ~Event();

  void Dump(Stream *s) const;

  EventData *GetData() { return m_data_sp.get(); }

  const EventData *GetData() const { return m_data_sp.get(); }

  void SetData(EventData *new_data) { m_data_sp.reset(new_data); }

  uint32_t GetType() const { return m_type; }

  void SetType(uint32_t new_type) { m_type = new_type; }

  Broadcaster *GetBroadcaster() const {
    Broadcaster::BroadcasterImplSP broadcaster_impl_sp =
        m_broadcaster_wp.lock();
    if (broadcaster_impl_sp)
      return broadcaster_impl_sp->GetBroadcaster();
    else
      return nullptr;
  }

  bool BroadcasterIs(Broadcaster *broadcaster) {
    Broadcaster::BroadcasterImplSP broadcaster_impl_sp =
        m_broadcaster_wp.lock();
    if (broadcaster_impl_sp)
      return broadcaster_impl_sp->GetBroadcaster() == broadcaster;
    else
      return false;
  }

  void Clear() { m_data_sp.reset(); }

  /// This is used by Broadcasters with Primary Listeners to store the other
  /// Listeners till after the Event's DoOnRemoval has completed.
  void AddPendingListener(lldb::ListenerSP pending_listener_sp) {
    m_pending_listeners.push_back(pending_listener_sp);
  };

private:
  // This is only called by Listener when it pops an event off the queue for
  // the listener.  It calls the Event Data's DoOnRemoval() method, which is
  // virtual and can be overridden by the specific data classes.

  void DoOnRemoval();

  // Called by Broadcaster::BroadcastEvent prior to letting all the listeners
  // know about it update the contained broadcaster so that events can be
  // popped off one queue and re-broadcast to others.
  void SetBroadcaster(Broadcaster *broadcaster) {
    m_broadcaster_wp = broadcaster->GetBroadcasterImpl();
  }

  Broadcaster::BroadcasterImplWP
      m_broadcaster_wp;        // The broadcaster that sent this event
  uint32_t m_type;             // The bit describing this event
  lldb::EventDataSP m_data_sp; // User specific data for this event
  std::vector<lldb::ListenerSP> m_pending_listeners;
  std::mutex m_listeners_mutex;

  Event(const Event &) = delete;
  const Event &operator=(const Event &) = delete;
  Event() = delete;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_EVENT_H
