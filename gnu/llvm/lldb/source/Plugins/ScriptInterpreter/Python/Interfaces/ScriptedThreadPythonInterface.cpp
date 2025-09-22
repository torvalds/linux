//===-- ScriptedThreadPythonInterface.cpp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Utility/Log.h"
#include "lldb/lldb-enumerations.h"

#if LLDB_ENABLE_PYTHON

// LLDB Python header must be included first
#include "../lldb-python.h"

#include "../SWIGPythonBridge.h"
#include "../ScriptInterpreterPythonImpl.h"
#include "ScriptedThreadPythonInterface.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::python;
using Locker = ScriptInterpreterPythonImpl::Locker;

ScriptedThreadPythonInterface::ScriptedThreadPythonInterface(
    ScriptInterpreterPythonImpl &interpreter)
    : ScriptedThreadInterface(), ScriptedPythonInterface(interpreter) {}

llvm::Expected<StructuredData::GenericSP>
ScriptedThreadPythonInterface::CreatePluginObject(
    const llvm::StringRef class_name, ExecutionContext &exe_ctx,
    StructuredData::DictionarySP args_sp, StructuredData::Generic *script_obj) {
  ExecutionContextRefSP exe_ctx_ref_sp =
      std::make_shared<ExecutionContextRef>(exe_ctx);
  StructuredDataImpl sd_impl(args_sp);
  return ScriptedPythonInterface::CreatePluginObject(class_name, script_obj,
                                                     exe_ctx_ref_sp, sd_impl);
}

lldb::tid_t ScriptedThreadPythonInterface::GetThreadID() {
  Status error;
  StructuredData::ObjectSP obj = Dispatch("get_thread_id", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, obj,
                                                    error))
    return LLDB_INVALID_THREAD_ID;

  return obj->GetUnsignedIntegerValue(LLDB_INVALID_THREAD_ID);
}

std::optional<std::string> ScriptedThreadPythonInterface::GetName() {
  Status error;
  StructuredData::ObjectSP obj = Dispatch("get_name", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, obj,
                                                    error))
    return {};

  return obj->GetStringValue().str();
}

lldb::StateType ScriptedThreadPythonInterface::GetState() {
  Status error;
  StructuredData::ObjectSP obj = Dispatch("get_state", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, obj,
                                                    error))
    return eStateInvalid;

  return static_cast<StateType>(obj->GetUnsignedIntegerValue(eStateInvalid));
}

std::optional<std::string> ScriptedThreadPythonInterface::GetQueue() {
  Status error;
  StructuredData::ObjectSP obj = Dispatch("get_queue", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, obj,
                                                    error))
    return {};

  return obj->GetStringValue().str();
}

StructuredData::DictionarySP ScriptedThreadPythonInterface::GetStopReason() {
  Status error;
  StructuredData::DictionarySP dict =
      Dispatch<StructuredData::DictionarySP>("get_stop_reason", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, dict,
                                                    error))
    return {};

  return dict;
}

StructuredData::ArraySP ScriptedThreadPythonInterface::GetStackFrames() {
  Status error;
  StructuredData::ArraySP arr =
      Dispatch<StructuredData::ArraySP>("get_stackframes", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, arr,
                                                    error))
    return {};

  return arr;
}

StructuredData::DictionarySP ScriptedThreadPythonInterface::GetRegisterInfo() {
  Status error;
  StructuredData::DictionarySP dict =
      Dispatch<StructuredData::DictionarySP>("get_register_info", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, dict,
                                                    error))
    return {};

  return dict;
}

std::optional<std::string> ScriptedThreadPythonInterface::GetRegisterContext() {
  Status error;
  StructuredData::ObjectSP obj = Dispatch("get_register_context", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, obj,
                                                    error))
    return {};

  return obj->GetAsString()->GetValue().str();
}

StructuredData::ArraySP ScriptedThreadPythonInterface::GetExtendedInfo() {
  Status error;
  StructuredData::ArraySP arr =
      Dispatch<StructuredData::ArraySP>("get_extended_info", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, arr,
                                                    error))
    return {};

  return arr;
}

#endif
