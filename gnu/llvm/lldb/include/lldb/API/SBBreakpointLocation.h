//===-- SBBreakpointLocation.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBBREAKPOINTLOCATION_H
#define LLDB_API_SBBREAKPOINTLOCATION_H

#include "lldb/API/SBBreakpoint.h"
#include "lldb/API/SBDefines.h"

namespace lldb_private {
namespace python {
class SWIGBridge;
}
namespace lua {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class LLDB_API SBBreakpointLocation {
public:
  SBBreakpointLocation();

  SBBreakpointLocation(const lldb::SBBreakpointLocation &rhs);

  ~SBBreakpointLocation();

  const lldb::SBBreakpointLocation &
  operator=(const lldb::SBBreakpointLocation &rhs);

  break_id_t GetID();

  explicit operator bool() const;

  bool IsValid() const;

  lldb::SBAddress GetAddress();

  lldb::addr_t GetLoadAddress();

  void SetEnabled(bool enabled);

  bool IsEnabled();

  uint32_t GetHitCount();

  uint32_t GetIgnoreCount();

  void SetIgnoreCount(uint32_t n);

  void SetCondition(const char *condition);

  const char *GetCondition();

  void SetAutoContinue(bool auto_continue);

  bool GetAutoContinue();

#ifndef SWIG
  void SetCallback(SBBreakpointHitCallback callback, void *baton);
#endif

  void SetScriptCallbackFunction(const char *callback_function_name);

  SBError SetScriptCallbackFunction(const char *callback_function_name,
                                    lldb::SBStructuredData &extra_args);

  SBError SetScriptCallbackBody(const char *script_body_text);
  
  void SetCommandLineCommands(lldb::SBStringList &commands);

  bool GetCommandLineCommands(lldb::SBStringList &commands);
 
  void SetThreadID(lldb::tid_t sb_thread_id);

  lldb::tid_t GetThreadID();

  void SetThreadIndex(uint32_t index);

  uint32_t GetThreadIndex() const;

  void SetThreadName(const char *thread_name);

  const char *GetThreadName() const;

  void SetQueueName(const char *queue_name);

  const char *GetQueueName() const;

  bool IsResolved();

  bool GetDescription(lldb::SBStream &description, DescriptionLevel level);

  SBBreakpoint GetBreakpoint();

protected:
  friend class lldb_private::python::SWIGBridge;
  friend class lldb_private::lua::SWIGBridge;
  SBBreakpointLocation(const lldb::BreakpointLocationSP &break_loc_sp);

private:
  friend class SBBreakpoint;
  friend class SBBreakpointCallbackBaton;

  void SetLocation(const lldb::BreakpointLocationSP &break_loc_sp);
  BreakpointLocationSP GetSP() const;

  lldb::BreakpointLocationWP m_opaque_wp;
};

} // namespace lldb

#endif // LLDB_API_SBBREAKPOINTLOCATION_H
