//===-- ScriptedPlatformInterface.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_INTERFACES_SCRIPTEDPLATFORMINTERFACE_H
#define LLDB_INTERPRETER_INTERFACES_SCRIPTEDPLATFORMINTERFACE_H

#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Interpreter/Interfaces/ScriptedInterface.h"

#include "lldb/lldb-private.h"

#include <string>

namespace lldb_private {
class ScriptedPlatformInterface : virtual public ScriptedInterface {
public:
  virtual llvm::Expected<StructuredData::GenericSP>
  CreatePluginObject(llvm::StringRef class_name, ExecutionContext &exe_ctx,
                     StructuredData::DictionarySP args_sp,
                     StructuredData::Generic *script_obj = nullptr) = 0;

  virtual StructuredData::DictionarySP ListProcesses() { return {}; }

  virtual StructuredData::DictionarySP GetProcessInfo(lldb::pid_t) {
    return {};
  }

  virtual Status AttachToProcess(lldb::ProcessAttachInfoSP attach_info) {
    return Status("ScriptedPlatformInterface cannot attach to a process");
  }

  virtual Status LaunchProcess(lldb::ProcessLaunchInfoSP launch_info) {
    return Status("ScriptedPlatformInterface cannot launch process");
  }

  virtual Status KillProcess(lldb::pid_t pid) {
    return Status("ScriptedPlatformInterface cannot kill process");
  }
};
} // namespace lldb_private

#endif // LLDB_INTERPRETER_INTERFACES_SCRIPTEDPLATFORMINTERFACE_H
