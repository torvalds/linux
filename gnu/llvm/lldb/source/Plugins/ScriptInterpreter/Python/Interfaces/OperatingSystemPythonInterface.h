//===-- OperatingSystemPythonInterface.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_OPERATINGSYSTEMPYTHONINTERFACE_H
#define LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_OPERATINGSYSTEMPYTHONINTERFACE_H

#include "lldb/Host/Config.h"

#if LLDB_ENABLE_PYTHON

#include "ScriptedThreadPythonInterface.h"
#include "lldb/Interpreter/Interfaces/OperatingSystemInterface.h"
#include <optional>

namespace lldb_private {
class OperatingSystemPythonInterface
    : virtual public OperatingSystemInterface,
      virtual public ScriptedThreadPythonInterface {
public:
  OperatingSystemPythonInterface(ScriptInterpreterPythonImpl &interpreter);

  llvm::Expected<StructuredData::GenericSP>
  CreatePluginObject(llvm::StringRef class_name, ExecutionContext &exe_ctx,
                     StructuredData::DictionarySP args_sp,
                     StructuredData::Generic *script_obj = nullptr) override;

  llvm::SmallVector<llvm::StringLiteral> GetAbstractMethods() const override {
    return llvm::SmallVector<llvm::StringLiteral>({"get_thread_info"});
  }

  StructuredData::DictionarySP CreateThread(lldb::tid_t tid,
                                            lldb::addr_t context) override;

  StructuredData::ArraySP GetThreadInfo() override;

  StructuredData::DictionarySP GetRegisterInfo() override;

  std::optional<std::string> GetRegisterContextForTID(lldb::tid_t tid) override;
};
} // namespace lldb_private

#endif // LLDB_ENABLE_PYTHON
#endif // LLDB_PLUGINS_SCRIPTINTERPRETER_PYTHON_INTERFACES_OPERATINGSYSTEMPYTHONINTERFACE_H
