//===-- ScriptInterpreterNone.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ScriptInterpreterNone.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"

#include "llvm/Support/Threading.h"

#include <mutex>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ScriptInterpreterNone)

ScriptInterpreterNone::ScriptInterpreterNone(Debugger &debugger)
    : ScriptInterpreter(debugger, eScriptLanguageNone) {}

ScriptInterpreterNone::~ScriptInterpreterNone() = default;

static const char *no_interpreter_err_msg =
    "error: Embedded script interpreter unavailable. LLDB was built without "
    "scripting language support.\n";

bool ScriptInterpreterNone::ExecuteOneLine(llvm::StringRef command,
                                           CommandReturnObject *,
                                           const ExecuteScriptOptions &) {
  m_debugger.GetErrorStream().PutCString(no_interpreter_err_msg);
  return false;
}

void ScriptInterpreterNone::ExecuteInterpreterLoop() {
  m_debugger.GetErrorStream().PutCString(no_interpreter_err_msg);
}

void ScriptInterpreterNone::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(),
                                  lldb::eScriptLanguageNone, CreateInstance);
  });
}

void ScriptInterpreterNone::Terminate() {}

lldb::ScriptInterpreterSP
ScriptInterpreterNone::CreateInstance(Debugger &debugger) {
  return std::make_shared<ScriptInterpreterNone>(debugger);
}

llvm::StringRef ScriptInterpreterNone::GetPluginDescriptionStatic() {
  return "Null script interpreter";
}
