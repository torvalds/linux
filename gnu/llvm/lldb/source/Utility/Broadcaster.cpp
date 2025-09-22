//===-- Broadcaster.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Broadcaster.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <cassert>
#include <cstddef>

using namespace lldb;
using namespace lldb_private;

Broadcaster::Broadcaster(BroadcasterManagerSP manager_sp, std::string name)
    : m_broadcaster_sp(std::make_shared<BroadcasterImpl>(*this)),
      m_manager_sp(std::move(manager_sp)), m_broadcaster_name(std::move(name)) {
  Log *log = GetLog(LLDBLog::Object);
  LLDB_LOG(log, "{0} Broadcaster::Broadcaster(\"{1}\")",
           static_cast<void *>(this), GetBroadcasterName());
}

Broadcaster::BroadcasterImpl::BroadcasterImpl(Broadcaster &broadcaster)
    : m_broadcaster(broadcaster), m_listeners(), m_listeners_mutex(),
      m_hijacking_listeners(), m_hijacking_masks() {}

Broadcaster::~Broadcaster() {
  Log *log = GetLog(LLDBLog::Object);
  LLDB_LOG(log, "{0} Broadcaster::~Broadcaster(\"{1}\")",
           static_cast<void *>(this), GetBroadcasterName());

  Clear();
}

void Broadcaster::CheckInWithManager() {
  if (m_manager_sp) {
    m_manager_sp->SignUpListenersForBroadcaster(*this);
  }
}

llvm::SmallVector<std::pair<ListenerSP, uint32_t &>, 4>
Broadcaster::BroadcasterImpl::GetListeners(uint32_t event_mask,
                                           bool include_primary) {
  llvm::SmallVector<std::pair<ListenerSP, uint32_t &>, 4> listeners;
  size_t max_count = m_listeners.size();
  if (include_primary)
    max_count++;
  listeners.reserve(max_count);

  for (auto it = m_listeners.begin(); it != m_listeners.end();) {
    lldb::ListenerSP curr_listener_sp(it->first.lock());
    if (curr_listener_sp) {
      if (it->second & event_mask)
        listeners.emplace_back(std::move(curr_listener_sp), it->second);
      ++it;
    } else
      // If our listener_wp didn't resolve, then we should remove this entry.
      it = m_listeners.erase(it);
  }
  if (include_primary && m_primary_listener_sp)
    listeners.emplace_back(m_primary_listener_sp, m_primary_listener_mask);

  return listeners;
}

bool Broadcaster::BroadcasterImpl::HasListeners(uint32_t event_mask) {
  if (m_primary_listener_sp)
    return true;
  for (auto it = m_listeners.begin(); it != m_listeners.end(); it++) {
    // Don't return a listener if the other end of the WP is gone:
    lldb::ListenerSP curr_listener_sp(it->first.lock());
    if (curr_listener_sp && (it->second & event_mask))
      return true;
  }
  return false;
}

void Broadcaster::BroadcasterImpl::Clear() {
  std::lock_guard<std::mutex> guard(m_listeners_mutex);

  // Make sure the listener forgets about this broadcaster. We do this in the
  // broadcaster in case the broadcaster object initiates the removal.
  for (auto &pair : GetListeners())
    pair.first->BroadcasterWillDestruct(&m_broadcaster);

  m_listeners.clear();
  m_primary_listener_sp.reset();
}

Broadcaster *Broadcaster::BroadcasterImpl::GetBroadcaster() {
  return &m_broadcaster;
}

bool Broadcaster::BroadcasterImpl::GetEventNames(
    Stream &s, uint32_t event_mask, bool prefix_with_broadcaster_name) const {
  uint32_t num_names_added = 0;
  if (event_mask && !m_event_names.empty()) {
    event_names_map::const_iterator end = m_event_names.end();
    for (uint32_t bit = 1u, mask = event_mask; mask != 0 && bit != 0;
         bit <<= 1, mask >>= 1) {
      if (mask & 1) {
        event_names_map::const_iterator pos = m_event_names.find(bit);
        if (pos != end) {
          if (num_names_added > 0)
            s.PutCString(", ");

          if (prefix_with_broadcaster_name) {
            s.PutCString(GetBroadcasterName());
            s.PutChar('.');
          }
          s.PutCString(pos->second);
          ++num_names_added;
        }
      }
    }
  }
  return num_names_added > 0;
}

void Broadcaster::AddInitialEventsToListener(
    const lldb::ListenerSP &listener_sp, uint32_t requested_events) {}

uint32_t
Broadcaster::BroadcasterImpl::AddListener(const lldb::ListenerSP &listener_sp,
                                          uint32_t event_mask) {
  if (!listener_sp)
    return 0;

  std::lock_guard<std::mutex> guard(m_listeners_mutex);

  // See if we already have this listener, and if so, update its mask

  bool handled = false;

  if (listener_sp == m_primary_listener_sp)
    // This already handles all bits so just return the mask:
    return event_mask;

  for (auto &pair : GetListeners(UINT32_MAX, false)) {
    if (pair.first == listener_sp) {
      handled = true;
      pair.second |= event_mask;
      m_broadcaster.AddInitialEventsToListener(listener_sp, event_mask);
      break;
    }
  }

  if (!handled) {
    // Grant a new listener the available event bits
    m_listeners.push_back(
        std::make_pair(lldb::ListenerWP(listener_sp), event_mask));

    // Individual broadcasters decide whether they have outstanding data when a
    // listener attaches, and insert it into the listener with this method.
    m_broadcaster.AddInitialEventsToListener(listener_sp, event_mask);
  }

  // Return the event bits that were granted to the listener
  return event_mask;
}

bool Broadcaster::BroadcasterImpl::EventTypeHasListeners(uint32_t event_type) {
  std::lock_guard<std::mutex> guard(m_listeners_mutex);

  if (!m_hijacking_listeners.empty() && event_type & m_hijacking_masks.back())
    return true;

  // The primary listener listens for all event bits:
  if (m_primary_listener_sp)
    return true;

  return HasListeners(event_type);
}

bool Broadcaster::BroadcasterImpl::RemoveListener(
    lldb_private::Listener *listener, uint32_t event_mask) {
  if (!listener)
    return false;

  if (listener == m_primary_listener_sp.get()) {
    // Primary listeners listen for all the event bits for their broadcaster,
    // so remove this altogether if asked:
    m_primary_listener_sp.reset();
    return true;
  }

  std::lock_guard<std::mutex> guard(m_listeners_mutex);
  for (auto it = m_listeners.begin(); it != m_listeners.end();) {
    lldb::ListenerSP curr_listener_sp(it->first.lock());

    if (!curr_listener_sp) {
      // The weak pointer for this listener didn't resolve, lets' prune it
      // as we go.
      it = m_listeners.erase(it);
      continue;
    }

    if (curr_listener_sp.get() == listener) {
      it->second &= ~event_mask;
      // If we removed all the event bits from a listener, remove it from
      // the list as well.
      if (!it->second)
        m_listeners.erase(it);
      return true;
    }
    it++;
  }
  return false;
}

bool Broadcaster::BroadcasterImpl::RemoveListener(
    const lldb::ListenerSP &listener_sp, uint32_t event_mask) {
  return RemoveListener(listener_sp.get(), event_mask);
}

void Broadcaster::BroadcasterImpl::BroadcastEvent(EventSP &event_sp) {
  return PrivateBroadcastEvent(event_sp, false);
}

void Broadcaster::BroadcasterImpl::BroadcastEventIfUnique(EventSP &event_sp) {
  return PrivateBroadcastEvent(event_sp, true);
}

void Broadcaster::BroadcasterImpl::PrivateBroadcastEvent(EventSP &event_sp,
                                                         bool unique) {
  // Can't add a nullptr event...
  if (!event_sp)
    return;

  // Update the broadcaster on this event
  event_sp->SetBroadcaster(&m_broadcaster);

  const uint32_t event_type = event_sp->GetType();

  std::lock_guard<std::mutex> guard(m_listeners_mutex);

  ListenerSP hijacking_listener_sp;

  if (!m_hijacking_listeners.empty()) {
    assert(!m_hijacking_masks.empty());
    hijacking_listener_sp = m_hijacking_listeners.back();
    if ((event_type & m_hijacking_masks.back()) == 0)
      hijacking_listener_sp.reset();
  }

  Log *log = GetLog(LLDBLog::Events);
  if (!log && event_sp->GetData())
    log = event_sp->GetData()->GetLogChannel();

  if (log) {
    StreamString event_description;
    event_sp->Dump(&event_description);
    LLDB_LOG(log,
             "{0:x} Broadcaster(\"{1}\")::BroadcastEvent (event_sp = {2}, "
             "unique={3}) hijack = {4:x}",
             static_cast<void *>(this), GetBroadcasterName(),
             event_description.GetData(), unique,
             static_cast<void *>(hijacking_listener_sp.get()));
  }
  ListenerSP primary_listener_sp
      = hijacking_listener_sp ? hijacking_listener_sp : m_primary_listener_sp;

  if (primary_listener_sp) {
    if (unique && primary_listener_sp->PeekAtNextEventForBroadcasterWithType(
                      &m_broadcaster, event_type))
      return;
    // Add the pending listeners but not if the event is hijacked, since that
    // is given sole access to the event stream it is hijacking.
    // Make sure to do this before adding the event to the primary or it might
    // start handling the event before we're done adding all the pending
    // listeners.
    // Also, don't redo the check for unique here, since otherwise that could
    // be racy, and if we send the event to the primary listener then we SHOULD 
    // send it to the secondary listeners or they will get out of sync with the
    // primary listener.
    if (!hijacking_listener_sp) {
      for (auto &pair : GetListeners(event_type, false))
        event_sp->AddPendingListener(pair.first);
    }
    primary_listener_sp->AddEvent(event_sp);
  } else {
    for (auto &pair : GetListeners(event_type)) {
      if (unique && pair.first->PeekAtNextEventForBroadcasterWithType(
                        &m_broadcaster, event_type))
        continue;

      pair.first->AddEvent(event_sp);
    }
  }
}

void Broadcaster::BroadcasterImpl::BroadcastEvent(uint32_t event_type) {
  auto event_sp = std::make_shared<Event>(event_type, /*data = */ nullptr);
  PrivateBroadcastEvent(event_sp, false);
}

void Broadcaster::BroadcasterImpl::BroadcastEvent(
    uint32_t event_type, const lldb::EventDataSP &event_data_sp) {
  auto event_sp = std::make_shared<Event>(event_type, event_data_sp);
  PrivateBroadcastEvent(event_sp, false);
}

void Broadcaster::BroadcasterImpl::BroadcastEventIfUnique(uint32_t event_type) {
  auto event_sp = std::make_shared<Event>(event_type, /*data = */ nullptr);
  PrivateBroadcastEvent(event_sp, true);
}

void Broadcaster::BroadcasterImpl::SetPrimaryListener(lldb::ListenerSP
                                                      listener_sp) {
  // This might have already been added as a normal listener, make sure we
  // don't hold two copies.
  RemoveListener(listener_sp.get(), UINT32_MAX);
  m_primary_listener_sp = listener_sp;
                                                      
}

bool Broadcaster::BroadcasterImpl::HijackBroadcaster(
    const lldb::ListenerSP &listener_sp, uint32_t event_mask) {
  std::lock_guard<std::mutex> guard(m_listeners_mutex);

  Log *log = GetLog(LLDBLog::Events);
  LLDB_LOG(
      log,
      "{0} Broadcaster(\"{1}\")::HijackBroadcaster (listener(\"{2}\")={3})",
      static_cast<void *>(this), GetBroadcasterName(),
      listener_sp->m_name.c_str(), static_cast<void *>(listener_sp.get()));
  m_hijacking_listeners.push_back(listener_sp);
  m_hijacking_masks.push_back(event_mask);
  return true;
}

bool Broadcaster::BroadcasterImpl::IsHijackedForEvent(uint32_t event_mask) {
  std::lock_guard<std::mutex> guard(m_listeners_mutex);

  if (!m_hijacking_listeners.empty())
    return (event_mask & m_hijacking_masks.back()) != 0;
  return false;
}

const char *Broadcaster::BroadcasterImpl::GetHijackingListenerName() {
  if (m_hijacking_listeners.size()) {
    return m_hijacking_listeners.back()->GetName();
  }
  return nullptr;
}

void Broadcaster::BroadcasterImpl::RestoreBroadcaster() {
  std::lock_guard<std::mutex> guard(m_listeners_mutex);

  if (!m_hijacking_listeners.empty()) {
    ListenerSP listener_sp = m_hijacking_listeners.back();
    Log *log = GetLog(LLDBLog::Events);
    LLDB_LOG(log,
             "{0} Broadcaster(\"{1}\")::RestoreBroadcaster (about to pop "
             "listener(\"{2}\")={3})",
             static_cast<void *>(this), GetBroadcasterName(),
             listener_sp->m_name.c_str(),
             static_cast<void *>(listener_sp.get()));
    m_hijacking_listeners.pop_back();
  }
  if (!m_hijacking_masks.empty())
    m_hijacking_masks.pop_back();
}

llvm::StringRef Broadcaster::GetBroadcasterClass() const {
  static constexpr llvm::StringLiteral class_name("lldb.anonymous");
  return class_name;
}

bool BroadcastEventSpec::operator<(const BroadcastEventSpec &rhs) const {
  if (GetBroadcasterClass() == rhs.GetBroadcasterClass()) {
    return GetEventBits() < rhs.GetEventBits();
  }
  return GetBroadcasterClass() < rhs.GetBroadcasterClass();
}

BroadcasterManager::BroadcasterManager() : m_manager_mutex() {}

lldb::BroadcasterManagerSP BroadcasterManager::MakeBroadcasterManager() {
  return lldb::BroadcasterManagerSP(new BroadcasterManager());
}

uint32_t BroadcasterManager::RegisterListenerForEventsNoLock(
    const lldb::ListenerSP &listener_sp, const BroadcastEventSpec &event_spec) {
  collection::iterator iter = m_event_map.begin(), end_iter = m_event_map.end();
  uint32_t available_bits = event_spec.GetEventBits();

  auto class_matches = [&event_spec](const event_listener_key &input) -> bool {
    return input.first.GetBroadcasterClass() ==
           event_spec.GetBroadcasterClass();
  };

  while (iter != end_iter &&
         (iter = find_if(iter, end_iter, class_matches)) != end_iter) {
    available_bits &= ~((*iter).first.GetEventBits());
    iter++;
  }

  if (available_bits != 0) {
    m_event_map.insert(event_listener_key(
        BroadcastEventSpec(event_spec.GetBroadcasterClass(), available_bits),
        listener_sp));
    m_listeners.insert(listener_sp);
  }

  return available_bits;
}

bool BroadcasterManager::UnregisterListenerForEventsNoLock(
    const lldb::ListenerSP &listener_sp, const BroadcastEventSpec &event_spec) {
  bool removed_some = false;

  if (m_listeners.erase(listener_sp) == 0)
    return false;

  auto listener_matches_and_shared_bits =
      [&listener_sp, &event_spec](const event_listener_key &input) -> bool {
    return input.first.GetBroadcasterClass() ==
               event_spec.GetBroadcasterClass() &&
           (input.first.GetEventBits() & event_spec.GetEventBits()) != 0 &&
           input.second == listener_sp;
  };
  std::vector<BroadcastEventSpec> to_be_readded;
  uint32_t event_bits_to_remove = event_spec.GetEventBits();

  // Go through the map and delete the exact matches, and build a list of
  // matches that weren't exact to re-add:
  for (auto iter = m_event_map.begin(), end = m_event_map.end();;) {
    iter = find_if(iter, end, listener_matches_and_shared_bits);
    if (iter == end)
      break;
    uint32_t iter_event_bits = (*iter).first.GetEventBits();
    removed_some = true;

    if (event_bits_to_remove != iter_event_bits) {
      uint32_t new_event_bits = iter_event_bits & ~event_bits_to_remove;
      to_be_readded.emplace_back(event_spec.GetBroadcasterClass(),
                                 new_event_bits);
    }
    iter = m_event_map.erase(iter);
  }

  // Okay now add back the bits that weren't completely removed:
  for (const auto &event : to_be_readded) {
    m_event_map.insert(event_listener_key(event, listener_sp));
  }

  return removed_some;
}

ListenerSP BroadcasterManager::GetListenerForEventSpec(
    const BroadcastEventSpec &event_spec) const {
  std::lock_guard<std::mutex> guard(m_manager_mutex);

  auto event_spec_matches =
      [&event_spec](const event_listener_key &input) -> bool {
    return input.first.IsContainedIn(event_spec);
  };

  auto iter = llvm::find_if(m_event_map, event_spec_matches);
  if (iter != m_event_map.end())
    return (*iter).second;

  return nullptr;
}

void BroadcasterManager::RemoveListener(Listener *listener) {
  std::lock_guard<std::mutex> guard(m_manager_mutex);
  auto listeners_predicate =
      [&listener](const lldb::ListenerSP &input) -> bool {
    return input.get() == listener;
  };

  if (auto iter = llvm::find_if(m_listeners, listeners_predicate);
      iter != m_listeners.end())
    m_listeners.erase(iter);

  auto events_predicate = [listener](const event_listener_key &input) -> bool {
    return input.second.get() == listener;
  };

  // TODO: use 'std::map::erase_if' when moving to c++20.
  for (auto iter = m_event_map.begin(), end = m_event_map.end();;) {
    iter = find_if(iter, end, events_predicate);
    if (iter == end)
      break;

    iter = m_event_map.erase(iter);
  }
}

void BroadcasterManager::RemoveListener(const lldb::ListenerSP &listener_sp) {
  std::lock_guard<std::mutex> guard(m_manager_mutex);

  auto listener_matches =
      [&listener_sp](const event_listener_key &input) -> bool {
    return input.second == listener_sp;
  };

  if (m_listeners.erase(listener_sp) == 0)
    return;

  // TODO: use 'std::map::erase_if' when moving to c++20.
  for (auto iter = m_event_map.begin(), end_iter = m_event_map.end();;) {
    iter = find_if(iter, end_iter, listener_matches);
    if (iter == end_iter)
      break;

    iter = m_event_map.erase(iter);
  }
}

void BroadcasterManager::SignUpListenersForBroadcaster(
    Broadcaster &broadcaster) {
  std::lock_guard<std::mutex> guard(m_manager_mutex);

  collection::iterator iter = m_event_map.begin(), end_iter = m_event_map.end();

  auto class_matches = [&broadcaster](const event_listener_key &input) -> bool {
    return input.first.GetBroadcasterClass() ==
           broadcaster.GetBroadcasterClass();
  };

  while (iter != end_iter &&
         (iter = find_if(iter, end_iter, class_matches)) != end_iter) {
    (*iter).second->StartListeningForEvents(&broadcaster,
                                            (*iter).first.GetEventBits());
    iter++;
  }
}

void BroadcasterManager::Clear() {
  std::lock_guard<std::mutex> guard(m_manager_mutex);

  for (auto &listener : m_listeners)
    listener->BroadcasterManagerWillDestruct(this->shared_from_this());
  m_listeners.clear();
  m_event_map.clear();
}
