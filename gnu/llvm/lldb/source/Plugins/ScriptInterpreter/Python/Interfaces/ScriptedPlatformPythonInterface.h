//===-- ScriptedPlatformPythonInterface.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_SCRIPTEDPLATFORMPYTHONINTERFACE_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_SCRIPTEDPLATFORMPYTHONINTERFACE_H

#include "lldb/Host/Config.h"

#if LLDB_ENABLE_PYTHON

#include "ScriptedPythonInterface.h"
#include "lldb/Interpreter/Interfaces/ScriptedPlatformInterface.h"

namespace lldb_private {
class ScriptedPlatformPythonInterface : public ScriptedPlatformInterface,
                                        public ScriptedPythonInterface {
public:
  ScriptedPlatformPythonInterface(ScriptInterpreterPythonImpl &interpreter);

  llvm::Expected<StructuredData::GenericSP>
  CreatePluginObject(const llvm::StringRef class_name,
                     ExecutionContext &exe_ctx,
                     StructuredData::DictionarySP args_sp,
                     StructuredData::Generic *script_obj = nullptr) override;

  llvm::SmallVector<llvm::StringLiteral> GetAbstractMethods() const override {
    return llvm::SmallVector<llvm::StringLiteral>(
        {"list_processes", "attach_to_process", "launch_process",
         "kill_process"});
  }

  StructuredData::DictionarySP ListProcesses() override;

  StructuredData::DictionarySP GetProcessInfo(lldb::pid_t) override;

  Status AttachToProcess(lldb::ProcessAttachInfoSP attach_info) override;

  Status LaunchProcess(lldb::ProcessLaunchInfoSP launch_info) override;

  Status KillProcess(lldb::pid_t pid) override;
};
} // namespace lldb_private

#endif // LLDB_ENABLE_PYTHON
#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_SCRIPTEDPLATFORMPYTHONINTERFACE_H
