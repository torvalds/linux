//===-- SBEvent.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBEVENT_H
#define LLDB_API_SBEVENT_H

#include "lldb/API/SBDefines.h"

#include <cstdio>
#include <vector>

namespace lldb_private {
class ScriptInterpreter;
namespace python {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class SBBroadcaster;

class LLDB_API SBEvent {
public:
  SBEvent();

  SBEvent(const lldb::SBEvent &rhs);

  // Make an event that contains a C string.
  SBEvent(uint32_t event, const char *cstr, uint32_t cstr_len);

  ~SBEvent();

  const SBEvent &operator=(const lldb::SBEvent &rhs);

  explicit operator bool() const;

  bool IsValid() const;

  const char *GetDataFlavor();

  uint32_t GetType() const;

  lldb::SBBroadcaster GetBroadcaster() const;

  const char *GetBroadcasterClass() const;

#ifndef SWIG
  bool BroadcasterMatchesPtr(const lldb::SBBroadcaster *broadcaster);
#endif

  bool BroadcasterMatchesRef(const lldb::SBBroadcaster &broadcaster);

  void Clear();

  static const char *GetCStringFromEvent(const lldb::SBEvent &event);

  bool GetDescription(lldb::SBStream &description);

  bool GetDescription(lldb::SBStream &description) const;

protected:
  friend class SBListener;
  friend class SBBroadcaster;
  friend class SBBreakpoint;
  friend class SBDebugger;
  friend class SBProcess;
  friend class SBTarget;
  friend class SBThread;
  friend class SBWatchpoint;

  friend class lldb_private::ScriptInterpreter;
  friend class lldb_private::python::SWIGBridge;

  SBEvent(lldb::EventSP &event_sp);

  SBEvent(lldb_private::Event *event);

  lldb::EventSP &GetSP() const;

  void reset(lldb::EventSP &event_sp);

  void reset(lldb_private::Event *event);

  lldb_private::Event *get() const;

private:
  mutable lldb::EventSP m_event_sp;
  mutable lldb_private::Event *m_opaque_ptr = nullptr;
};

} // namespace lldb

#endif // LLDB_API_SBEVENT_H
