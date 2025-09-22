//===-- ScriptedThreadPythonInterface.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_SCRIPTEDTHREADPYTHONINTERFACE_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_SCRIPTEDTHREADPYTHONINTERFACE_H

#include "lldb/Host/Config.h"

#if LLDB_ENABLE_PYTHON

#include "ScriptedPythonInterface.h"
#include "lldb/Interpreter/Interfaces/ScriptedThreadInterface.h"
#include <optional>

namespace lldb_private {
class ScriptedThreadPythonInterface : public ScriptedThreadInterface,
                                      public ScriptedPythonInterface {
public:
  ScriptedThreadPythonInterface(ScriptInterpreterPythonImpl &interpreter);

  llvm::Expected<StructuredData::GenericSP>
  CreatePluginObject(llvm::StringRef class_name, ExecutionContext &exe_ctx,
                     StructuredData::DictionarySP args_sp,
                     StructuredData::Generic *script_obj = nullptr) override;

  llvm::SmallVector<llvm::StringLiteral> GetAbstractMethods() const override {
    return llvm::SmallVector<llvm::StringLiteral>(
        {"get_stop_reason", "get_register_context"});
  }

  lldb::tid_t GetThreadID() override;

  std::optional<std::string> GetName() override;

  lldb::StateType GetState() override;

  std::optional<std::string> GetQueue() override;

  StructuredData::DictionarySP GetStopReason() override;

  StructuredData::ArraySP GetStackFrames() override;

  StructuredData::DictionarySP GetRegisterInfo() override;

  std::optional<std::string> GetRegisterContext() override;

  StructuredData::ArraySP GetExtendedInfo() override;
};
} // namespace lldb_private

#endif // LLDB_ENABLE_PYTHON
#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_SCRIPTEDTHREADPYTHONINTERFACE_H
