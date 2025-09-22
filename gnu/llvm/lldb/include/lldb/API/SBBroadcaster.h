//===-- SBBroadcaster.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBBROADCASTER_H
#define LLDB_API_SBBROADCASTER_H

#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBBroadcaster {
public:
  SBBroadcaster();

  SBBroadcaster(const char *name);

  SBBroadcaster(const SBBroadcaster &rhs);

  const SBBroadcaster &operator=(const SBBroadcaster &rhs);

  ~SBBroadcaster();

  explicit operator bool() const;

  bool IsValid() const;

  void Clear();

  void BroadcastEventByType(uint32_t event_type, bool unique = false);

  void BroadcastEvent(const lldb::SBEvent &event, bool unique = false);

  void AddInitialEventsToListener(const lldb::SBListener &listener,
                                  uint32_t requested_events);

  uint32_t AddListener(const lldb::SBListener &listener, uint32_t event_mask);

  const char *GetName() const;

  bool EventTypeHasListeners(uint32_t event_type);

  bool RemoveListener(const lldb::SBListener &listener,
                      uint32_t event_mask = UINT32_MAX);

  // This comparison is checking if the internal opaque pointer value is equal
  // to that in "rhs".
  bool operator==(const lldb::SBBroadcaster &rhs) const;

  // This comparison is checking if the internal opaque pointer value is not
  // equal to that in "rhs".
  bool operator!=(const lldb::SBBroadcaster &rhs) const;

  // This comparison is checking if the internal opaque pointer value is less
  // than that in "rhs" so SBBroadcaster objects can be contained in ordered
  // containers.
  bool operator<(const lldb::SBBroadcaster &rhs) const;

protected:
  friend class SBCommandInterpreter;
  friend class SBCommunication;
  friend class SBDebugger;
  friend class SBEvent;
  friend class SBListener;
  friend class SBProcess;
  friend class SBTarget;

  SBBroadcaster(lldb_private::Broadcaster *broadcaster, bool owns);

  lldb_private::Broadcaster *get() const;

  void reset(lldb_private::Broadcaster *broadcaster, bool owns);

private:
  lldb::BroadcasterSP m_opaque_sp;
  lldb_private::Broadcaster *m_opaque_ptr = nullptr;
};

} // namespace lldb

#endif // LLDB_API_SBBROADCASTER_H
