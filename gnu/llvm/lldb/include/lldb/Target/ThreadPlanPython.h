//===-- ThreadPlanPython.h --------------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPLANPYTHON_H
#define LLDB_TARGET_THREADPLANPYTHON_H

#include <string>

#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Interpreter/Interfaces/ScriptedThreadPlanInterface.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanTracer.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

//  ThreadPlanPython:
//

class ThreadPlanPython : public ThreadPlan {
public:
  ThreadPlanPython(Thread &thread, const char *class_name,
                   const StructuredDataImpl &args_data);

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

  bool ValidatePlan(Stream *error) override;

  bool ShouldStop(Event *event_ptr) override;

  bool MischiefManaged() override;

  bool WillStop() override;

  bool StopOthers() override { return m_stop_others; }

  void SetStopOthers(bool new_value) override { m_stop_others = new_value; }

  void DidPush() override;

  bool IsPlanStale() override;
  
  bool DoWillResume(lldb::StateType resume_state, bool current_plan) override;


protected:
  bool DoPlanExplainsStop(Event *event_ptr) override;

  lldb::StateType GetPlanRunState() override;
  
  ScriptInterpreter *GetScriptInterpreter();

private:
  std::string m_class_name;
  StructuredDataImpl m_args_data;
  std::string m_error_str;
  StructuredData::ObjectSP m_implementation_sp;
  StreamString m_stop_description; // Cache the stop description here
  bool m_did_push;
  bool m_stop_others;
  lldb::ScriptedThreadPlanInterfaceSP m_interface;

  ThreadPlanPython(const ThreadPlanPython &) = delete;
  const ThreadPlanPython &operator=(const ThreadPlanPython &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADPLANPYTHON_H
