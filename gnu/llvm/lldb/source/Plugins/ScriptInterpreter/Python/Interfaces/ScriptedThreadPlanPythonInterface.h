//===-- ScriptedThreadPlanPythonInterface.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_SCRIPTEDTHREADPLANPYTHONINTERFACE_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_SCRIPTEDTHREADPLANPYTHONINTERFACE_H

#include "lldb/Host/Config.h"

#if LLDB_ENABLE_PYTHON

#include "ScriptedPythonInterface.h"
#include "lldb/Interpreter/Interfaces/ScriptedThreadPlanInterface.h"
#include <optional>

namespace lldb_private {
class ScriptedThreadPlanPythonInterface : public ScriptedThreadPlanInterface,
                                          public ScriptedPythonInterface {
public:
  ScriptedThreadPlanPythonInterface(ScriptInterpreterPythonImpl &interpreter);

  llvm::Expected<StructuredData::GenericSP>
  CreatePluginObject(const llvm::StringRef class_name,
                     lldb::ThreadPlanSP thread_plan_sp,
                     const StructuredDataImpl &args_sp) override;

  llvm::SmallVector<llvm::StringLiteral> GetAbstractMethods() const override {
    return {};
  }

  llvm::Expected<bool> ExplainsStop(Event *event) override;

  llvm::Expected<bool> ShouldStop(Event *event) override;

  llvm::Expected<bool> IsStale() override;

  lldb::StateType GetRunState() override;

  llvm::Error GetStopDescription(lldb::StreamSP &stream) override;
};
} // namespace lldb_private

#endif // LLDB_ENABLE_PYTHON
#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_SCRIPTEDTHREADPLANPYTHONINTERFACE_H
