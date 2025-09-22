//===-- SBBreakpoint.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBBREAKPOINT_H
#define LLDB_API_SBBREAKPOINT_H

#include "lldb/API/SBDefines.h"

class SBBreakpointListImpl;

namespace lldb_private {
class ScriptInterpreter;
namespace python {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class LLDB_API SBBreakpoint {
public:

  SBBreakpoint();

  SBBreakpoint(const lldb::SBBreakpoint &rhs);

  ~SBBreakpoint();

  const lldb::SBBreakpoint &operator=(const lldb::SBBreakpoint &rhs);

  // Tests to see if the opaque breakpoint object in this object matches the
  // opaque breakpoint object in "rhs".
  bool operator==(const lldb::SBBreakpoint &rhs);

  bool operator!=(const lldb::SBBreakpoint &rhs);

  break_id_t GetID() const;

  explicit operator bool() const;

  bool IsValid() const;

  void ClearAllBreakpointSites();

  lldb::SBTarget GetTarget() const;

  lldb::SBBreakpointLocation FindLocationByAddress(lldb::addr_t vm_addr);

  lldb::break_id_t FindLocationIDByAddress(lldb::addr_t vm_addr);

  lldb::SBBreakpointLocation FindLocationByID(lldb::break_id_t bp_loc_id);

  lldb::SBBreakpointLocation GetLocationAtIndex(uint32_t index);

  void SetEnabled(bool enable);

  bool IsEnabled();

  void SetOneShot(bool one_shot);

  bool IsOneShot() const;

  bool IsInternal();

  uint32_t GetHitCount() const;

  void SetIgnoreCount(uint32_t count);

  uint32_t GetIgnoreCount() const;

  void SetCondition(const char *condition);

  const char *GetCondition();

  void SetAutoContinue(bool auto_continue);

  bool GetAutoContinue();

  void SetThreadID(lldb::tid_t sb_thread_id);

  lldb::tid_t GetThreadID();

  void SetThreadIndex(uint32_t index);

  uint32_t GetThreadIndex() const;

  void SetThreadName(const char *thread_name);

  const char *GetThreadName() const;

  void SetQueueName(const char *queue_name);

  const char *GetQueueName() const;

#ifndef SWIG
  void SetCallback(SBBreakpointHitCallback callback, void *baton);
#endif

  void SetScriptCallbackFunction(const char *callback_function_name);

  SBError SetScriptCallbackFunction(const char *callback_function_name,
                                 SBStructuredData &extra_args);

  void SetCommandLineCommands(SBStringList &commands);

  bool GetCommandLineCommands(SBStringList &commands);

  SBError SetScriptCallbackBody(const char *script_body_text);

  LLDB_DEPRECATED_FIXME("Doesn't provide error handling",
                        "AddNameWithErrorHandling")
  bool AddName(const char *new_name);

  SBError AddNameWithErrorHandling(const char *new_name);

  void RemoveName(const char *name_to_remove);

  bool MatchesName(const char *name);

  void GetNames(SBStringList &names);

  size_t GetNumResolvedLocations() const;

  size_t GetNumLocations() const;

  bool GetDescription(lldb::SBStream &description);

  bool GetDescription(lldb::SBStream &description, bool include_locations);

  static bool EventIsBreakpointEvent(const lldb::SBEvent &event);

  static lldb::BreakpointEventType
  GetBreakpointEventTypeFromEvent(const lldb::SBEvent &event);

  static lldb::SBBreakpoint GetBreakpointFromEvent(const lldb::SBEvent &event);

  static lldb::SBBreakpointLocation
  GetBreakpointLocationAtIndexFromEvent(const lldb::SBEvent &event,
                                        uint32_t loc_idx);

  static uint32_t
  GetNumBreakpointLocationsFromEvent(const lldb::SBEvent &event_sp);

  bool IsHardware() const;

  // Can only be called from a ScriptedBreakpointResolver...
  SBError
  AddLocation(SBAddress &address);

  SBStructuredData SerializeToStructuredData();

private:
  friend class SBBreakpointList;
  friend class SBBreakpointLocation;
  friend class SBBreakpointName;
  friend class SBTarget;

  friend class lldb_private::ScriptInterpreter;
  friend class lldb_private::python::SWIGBridge;

  SBBreakpoint(const lldb::BreakpointSP &bp_sp);

  lldb::BreakpointSP GetSP() const;

  lldb::BreakpointWP m_opaque_wp;
};

class LLDB_API SBBreakpointList {
public:
  SBBreakpointList(SBTarget &target);

  ~SBBreakpointList();

  size_t GetSize() const;

  SBBreakpoint GetBreakpointAtIndex(size_t idx);

  SBBreakpoint FindBreakpointByID(lldb::break_id_t);

  void Append(const SBBreakpoint &sb_bkpt);

  bool AppendIfUnique(const SBBreakpoint &sb_bkpt);

  void AppendByID(lldb::break_id_t id);

  void Clear();

protected:
  friend class SBTarget;

  void CopyToBreakpointIDList(lldb_private::BreakpointIDList &bp_id_list);

private:
  std::shared_ptr<SBBreakpointListImpl> m_opaque_sp;
};

} // namespace lldb

#endif // LLDB_API_SBBREAKPOINT_H
