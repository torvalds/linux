//===-- ScriptedPlatformPythonInterface.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"

#if LLDB_ENABLE_PYTHON

// LLDB Python header must be included first
#include "../lldb-python.h"

#include "../SWIGPythonBridge.h"
#include "../ScriptInterpreterPythonImpl.h"
#include "ScriptedPlatformPythonInterface.h"

#include "lldb/Target/ExecutionContext.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::python;
using Locker = ScriptInterpreterPythonImpl::Locker;

ScriptedPlatformPythonInterface::ScriptedPlatformPythonInterface(
    ScriptInterpreterPythonImpl &interpreter)
    : ScriptedPlatformInterface(), ScriptedPythonInterface(interpreter) {}

llvm::Expected<StructuredData::GenericSP>
ScriptedPlatformPythonInterface::CreatePluginObject(
    llvm::StringRef class_name, ExecutionContext &exe_ctx,
    StructuredData::DictionarySP args_sp, StructuredData::Generic *script_obj) {
  ExecutionContextRefSP exe_ctx_ref_sp =
      std::make_shared<ExecutionContextRef>(exe_ctx);
  StructuredDataImpl sd_impl(args_sp);
  return ScriptedPythonInterface::CreatePluginObject(class_name, script_obj,
                                                     exe_ctx_ref_sp, sd_impl);
}

StructuredData::DictionarySP ScriptedPlatformPythonInterface::ListProcesses() {
  Status error;
  StructuredData::DictionarySP dict_sp =
      Dispatch<StructuredData::DictionarySP>("list_processes", error);

  if (!dict_sp || !dict_sp->IsValid() || error.Fail()) {
    return ScriptedInterface::ErrorWithMessage<StructuredData::DictionarySP>(
        LLVM_PRETTY_FUNCTION,
        llvm::Twine("Null or invalid object (" +
                    llvm::Twine(error.AsCString()) + llvm::Twine(")."))
            .str(),
        error);
  }

  return dict_sp;
}

StructuredData::DictionarySP
ScriptedPlatformPythonInterface::GetProcessInfo(lldb::pid_t pid) {
  Status error;
  StructuredData::DictionarySP dict_sp =
      Dispatch<StructuredData::DictionarySP>("get_process_info", error, pid);

  if (!dict_sp || !dict_sp->IsValid() || error.Fail()) {
    return ScriptedInterface::ErrorWithMessage<StructuredData::DictionarySP>(
        LLVM_PRETTY_FUNCTION,
        llvm::Twine("Null or invalid object (" +
                    llvm::Twine(error.AsCString()) + llvm::Twine(")."))
            .str(),
        error);
  }

  return dict_sp;
}

Status ScriptedPlatformPythonInterface::AttachToProcess(
    ProcessAttachInfoSP attach_info) {
  // FIXME: Pass `attach_info` to method call
  return GetStatusFromMethod("attach_to_process");
}

Status ScriptedPlatformPythonInterface::LaunchProcess(
    ProcessLaunchInfoSP launch_info) {
  // FIXME: Pass `launch_info` to method call
  return GetStatusFromMethod("launch_process");
}

Status ScriptedPlatformPythonInterface::KillProcess(lldb::pid_t pid) {
  return GetStatusFromMethod("kill_process", pid);
}

#endif // LLDB_ENABLE_PYTHON
