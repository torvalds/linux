//===-- ScriptedThreadPlanPythonInterface.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"
#include "lldb/Utility/Log.h"
#include "lldb/lldb-enumerations.h"

#if LLDB_ENABLE_PYTHON

// LLDB Python header must be included first
#include "../lldb-python.h"

#include "../SWIGPythonBridge.h"
#include "../ScriptInterpreterPythonImpl.h"
#include "ScriptedThreadPlanPythonInterface.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::python;

ScriptedThreadPlanPythonInterface::ScriptedThreadPlanPythonInterface(
    ScriptInterpreterPythonImpl &interpreter)
    : ScriptedThreadPlanInterface(), ScriptedPythonInterface(interpreter) {}

llvm::Expected<StructuredData::GenericSP>
ScriptedThreadPlanPythonInterface::CreatePluginObject(
    const llvm::StringRef class_name, lldb::ThreadPlanSP thread_plan_sp,
    const StructuredDataImpl &args_sp) {
  return ScriptedPythonInterface::CreatePluginObject(class_name, nullptr,
                                                     thread_plan_sp, args_sp);
}

llvm::Expected<bool>
ScriptedThreadPlanPythonInterface::ExplainsStop(Event *event) {
  Status error;
  StructuredData::ObjectSP obj = Dispatch("explains_stop", error, event);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, obj,
                                                    error)) {
    if (!obj)
      return false;
    return error.ToError();
  }

  return obj->GetBooleanValue();
}

llvm::Expected<bool>
ScriptedThreadPlanPythonInterface::ShouldStop(Event *event) {
  Status error;
  StructuredData::ObjectSP obj = Dispatch("should_stop", error, event);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, obj,
                                                    error)) {
    if (!obj)
      return false;
    return error.ToError();
  }

  return obj->GetBooleanValue();
}

llvm::Expected<bool> ScriptedThreadPlanPythonInterface::IsStale() {
  Status error;
  StructuredData::ObjectSP obj = Dispatch("is_stale", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, obj,
                                                    error)) {
    if (!obj)
      return false;
    return error.ToError();
  }

  return obj->GetBooleanValue();
}

lldb::StateType ScriptedThreadPlanPythonInterface::GetRunState() {
  Status error;
  StructuredData::ObjectSP obj = Dispatch("should_step", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, obj,
                                                    error))
    return lldb::eStateStepping;

  return static_cast<lldb::StateType>(obj->GetUnsignedIntegerValue(
      static_cast<uint32_t>(lldb::eStateStepping)));
}

llvm::Error
ScriptedThreadPlanPythonInterface::GetStopDescription(lldb::StreamSP &stream) {
  Status error;
  Dispatch("stop_description", error, stream);

  if (error.Fail())
    return error.ToError();

  return llvm::Error::success();
}

#endif
