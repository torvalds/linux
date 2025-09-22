//===-- ScriptInterpreterNone.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SCRIPTINTERPRETER_NONE_SCRIPTINTERPRETERNONE_H
#define LLDB_SOURCE_PLUGINS_SCRIPTINTERPRETER_NONE_SCRIPTINTERPRETERNONE_H

#include "lldb/Interpreter/ScriptInterpreter.h"

namespace lldb_private {

class ScriptInterpreterNone : public ScriptInterpreter {
public:
  ScriptInterpreterNone(Debugger &debugger);

  ~ScriptInterpreterNone() override;

  bool ExecuteOneLine(
      llvm::StringRef command, CommandReturnObject *result,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) override;

  void ExecuteInterpreterLoop() override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static lldb::ScriptInterpreterSP CreateInstance(Debugger &debugger);

  static llvm::StringRef GetPluginNameStatic() { return "script-none"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SCRIPTINTERPRETER_NONE_SCRIPTINTERPRETERNONE_H
