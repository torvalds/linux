//===-- ThreadPlanPython.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ThreadPlan.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/Interfaces/ScriptedThreadPlanInterface.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlan.h"
#include "lldb/Target/ThreadPlanPython.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

// ThreadPlanPython

ThreadPlanPython::ThreadPlanPython(Thread &thread, const char *class_name,
                                   const StructuredDataImpl &args_data)
    : ThreadPlan(ThreadPlan::eKindPython, "Python based Thread Plan", thread,
                 eVoteNoOpinion, eVoteNoOpinion),
      m_class_name(class_name), m_args_data(args_data), m_did_push(false),
      m_stop_others(false) {
  ScriptInterpreter *interpreter = GetScriptInterpreter();
  if (!interpreter) {
    SetPlanComplete(false);
    // FIXME: error handling
    // error.SetErrorStringWithFormat(
    //     "ThreadPlanPython::%s () - ERROR: %s", __FUNCTION__,
    //     "Couldn't get script interpreter");
    return;
  }

  m_interface = interpreter->CreateScriptedThreadPlanInterface();
  if (!m_interface) {
    SetPlanComplete(false);
    // FIXME: error handling
    // error.SetErrorStringWithFormat(
    //     "ThreadPlanPython::%s () - ERROR: %s", __FUNCTION__,
    //     "Script interpreter couldn't create Scripted Thread Plan Interface");
    return;
  }

  SetIsControllingPlan(true);
  SetOkayToDiscard(true);
  SetPrivate(false);
}

bool ThreadPlanPython::ValidatePlan(Stream *error) {
  if (!m_did_push)
    return true;

  if (!m_implementation_sp) {
    if (error)
      error->Printf("Error constructing Python ThreadPlan: %s",
          m_error_str.empty() ? "<unknown error>"
                                : m_error_str.c_str());
    return false;
  }

  return true;
}

ScriptInterpreter *ThreadPlanPython::GetScriptInterpreter() {
  return m_process.GetTarget().GetDebugger().GetScriptInterpreter();
}

void ThreadPlanPython::DidPush() {
  // We set up the script side in DidPush, so that it can push other plans in
  // the constructor, and doesn't have to care about the details of DidPush.
  m_did_push = true;
  if (m_interface) {
    auto obj_or_err = m_interface->CreatePluginObject(
        m_class_name, this->shared_from_this(), m_args_data);
    if (!obj_or_err) {
      m_error_str = llvm::toString(obj_or_err.takeError());
      SetPlanComplete(false);
    } else
      m_implementation_sp = *obj_or_err;
  }
}

bool ThreadPlanPython::ShouldStop(Event *event_ptr) {
  Log *log = GetLog(LLDBLog::Thread);
  LLDB_LOGF(log, "%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
            m_class_name.c_str());

  bool should_stop = true;
  if (m_implementation_sp) {
    auto should_stop_or_err = m_interface->ShouldStop(event_ptr);
    if (!should_stop_or_err) {
      LLDB_LOG_ERROR(GetLog(LLDBLog::Thread), should_stop_or_err.takeError(),
                     "Can't call ScriptedThreadPlan::ShouldStop.");
      SetPlanComplete(false);
    } else
      should_stop = *should_stop_or_err;
  }
  return should_stop;
}

bool ThreadPlanPython::IsPlanStale() {
  Log *log = GetLog(LLDBLog::Thread);
  LLDB_LOGF(log, "%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
            m_class_name.c_str());

  bool is_stale = true;
  if (m_implementation_sp) {
    auto is_stale_or_err = m_interface->IsStale();
    if (!is_stale_or_err) {
      LLDB_LOG_ERROR(GetLog(LLDBLog::Thread), is_stale_or_err.takeError(),
                     "Can't call ScriptedThreadPlan::IsStale.");
      SetPlanComplete(false);
    } else
      is_stale = *is_stale_or_err;
  }
  return is_stale;
}

bool ThreadPlanPython::DoPlanExplainsStop(Event *event_ptr) {
  Log *log = GetLog(LLDBLog::Thread);
  LLDB_LOGF(log, "%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
            m_class_name.c_str());

  bool explains_stop = true;
  if (m_implementation_sp) {
    auto explains_stop_or_error = m_interface->ExplainsStop(event_ptr);
    if (!explains_stop_or_error) {
      LLDB_LOG_ERROR(GetLog(LLDBLog::Thread),
                     explains_stop_or_error.takeError(),
                     "Can't call ScriptedThreadPlan::ExplainsStop.");
      SetPlanComplete(false);
    } else
      explains_stop = *explains_stop_or_error;
  }
  return explains_stop;
}

bool ThreadPlanPython::MischiefManaged() {
  Log *log = GetLog(LLDBLog::Thread);
  LLDB_LOGF(log, "%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
            m_class_name.c_str());
  bool mischief_managed = true;
  if (m_implementation_sp) {
    // I don't really need mischief_managed, since it's simpler to just call
    // SetPlanComplete in should_stop.
    mischief_managed = IsPlanComplete();
    if (mischief_managed) {
      // We need to cache the stop reason here we'll need it in GetDescription.
      GetDescription(&m_stop_description, eDescriptionLevelBrief);
      m_implementation_sp.reset();
    }
  }
  return mischief_managed;
}

lldb::StateType ThreadPlanPython::GetPlanRunState() {
  Log *log = GetLog(LLDBLog::Thread);
  LLDB_LOGF(log, "%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
            m_class_name.c_str());
  lldb::StateType run_state = eStateRunning;
  if (m_implementation_sp)
    run_state = m_interface->GetRunState();
  return run_state;
}

void ThreadPlanPython::GetDescription(Stream *s, lldb::DescriptionLevel level) {
  Log *log = GetLog(LLDBLog::Thread);
  LLDB_LOGF(log, "%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
            m_class_name.c_str());
  if (m_implementation_sp) {
    ScriptInterpreter *script_interp = GetScriptInterpreter();
    if (script_interp) {
      lldb::StreamSP stream = std::make_shared<lldb_private::StreamString>();
      llvm::Error err = m_interface->GetStopDescription(stream);
      if (err) {
        LLDB_LOG_ERROR(GetLog(LLDBLog::Thread), std::move(err),
                       "Can't call ScriptedThreadPlan::GetStopDescription.");
        s->Printf("Python thread plan implemented by class %s.",
            m_class_name.c_str());
      } else
        s->PutCString(
            reinterpret_cast<StreamString *>(stream.get())->GetData());
    }
    return;
  }
  // It's an error not to have a description, so if we get here, we should
  // add something.
  if (m_stop_description.Empty())
    s->Printf("Python thread plan implemented by class %s.",
              m_class_name.c_str());
  s->PutCString(m_stop_description.GetData());
}

// The ones below are not currently exported to Python.
bool ThreadPlanPython::WillStop() {
  Log *log = GetLog(LLDBLog::Thread);
  LLDB_LOGF(log, "%s called on Python Thread Plan: %s )", LLVM_PRETTY_FUNCTION,
            m_class_name.c_str());
  return true;
}

bool ThreadPlanPython::DoWillResume(lldb::StateType resume_state, 
                                  bool current_plan) {
  m_stop_description.Clear();
  return true;                                  
}
