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
#include "OperatingSystemPythonInterface.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::python;
using Locker = ScriptInterpreterPythonImpl::Locker;

OperatingSystemPythonInterface::OperatingSystemPythonInterface(
    ScriptInterpreterPythonImpl &interpreter)
    : OperatingSystemInterface(), ScriptedThreadPythonInterface(interpreter) {}

llvm::Expected<StructuredData::GenericSP>
OperatingSystemPythonInterface::CreatePluginObject(
    llvm::StringRef class_name, ExecutionContext &exe_ctx,
    StructuredData::DictionarySP args_sp, StructuredData::Generic *script_obj) {
  return ScriptedPythonInterface::CreatePluginObject(class_name, nullptr,
                                                     exe_ctx.GetProcessSP());
}

StructuredData::DictionarySP
OperatingSystemPythonInterface::CreateThread(lldb::tid_t tid,
                                             lldb::addr_t context) {
  Status error;
  StructuredData::DictionarySP dict = Dispatch<StructuredData::DictionarySP>(
      "create_thread", error, tid, context);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, dict,
                                                    error))
    return {};

  return dict;
}

StructuredData::ArraySP OperatingSystemPythonInterface::GetThreadInfo() {
  Status error;
  StructuredData::ArraySP arr =
      Dispatch<StructuredData::ArraySP>("get_thread_info", error);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, arr,
                                                    error))
    return {};

  return arr;
}

StructuredData::DictionarySP OperatingSystemPythonInterface::GetRegisterInfo() {
  return ScriptedThreadPythonInterface::GetRegisterInfo();
}

std::optional<std::string>
OperatingSystemPythonInterface::GetRegisterContextForTID(lldb::tid_t tid) {
  Status error;
  StructuredData::ObjectSP obj = Dispatch("get_register_data", error, tid);

  if (!ScriptedInterface::CheckStructuredDataObject(LLVM_PRETTY_FUNCTION, obj,
                                                    error))
    return {};

  return obj->GetAsString()->GetValue().str();
}

#endif
