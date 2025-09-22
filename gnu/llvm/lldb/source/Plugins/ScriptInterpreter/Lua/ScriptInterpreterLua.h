//===-- ScriptInterpreterLua.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ScriptInterpreterLua_h_
#define liblldb_ScriptInterpreterLua_h_

#include <vector>

#include "lldb/Breakpoint/WatchpointOptions.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"

namespace lldb_private {
class Lua;
class ScriptInterpreterLua : public ScriptInterpreter {
public:
  class CommandDataLua : public BreakpointOptions::CommandData {
  public:
    CommandDataLua() : BreakpointOptions::CommandData() {
      interpreter = lldb::eScriptLanguageLua;
    }
    CommandDataLua(StructuredData::ObjectSP extra_args_sp)
        : BreakpointOptions::CommandData(), m_extra_args_sp(extra_args_sp) {
      interpreter = lldb::eScriptLanguageLua;
    }
    StructuredData::ObjectSP m_extra_args_sp;
  };

  ScriptInterpreterLua(Debugger &debugger);

  ~ScriptInterpreterLua() override;

  bool ExecuteOneLine(
      llvm::StringRef command, CommandReturnObject *result,
      const ExecuteScriptOptions &options = ExecuteScriptOptions()) override;

  void ExecuteInterpreterLoop() override;

  bool LoadScriptingModule(const char *filename,
                           const LoadScriptOptions &options,
                           lldb_private::Status &error,
                           StructuredData::ObjectSP *module_sp = nullptr,
                           FileSpec extra_search_dir = {}) override;

  StructuredData::DictionarySP GetInterpreterInfo() override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static lldb::ScriptInterpreterSP CreateInstance(Debugger &debugger);

  static llvm::StringRef GetPluginNameStatic() { return "script-lua"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static bool BreakpointCallbackFunction(void *baton,
                                         StoppointCallbackContext *context,
                                         lldb::user_id_t break_id,
                                         lldb::user_id_t break_loc_id);

  static bool WatchpointCallbackFunction(void *baton,
                                         StoppointCallbackContext *context,
                                         lldb::user_id_t watch_id);

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  Lua &GetLua();

  llvm::Error EnterSession(lldb::user_id_t debugger_id);
  llvm::Error LeaveSession();

  void CollectDataForBreakpointCommandCallback(
      std::vector<std::reference_wrapper<BreakpointOptions>> &bp_options_vec,
      CommandReturnObject &result) override;

  void
  CollectDataForWatchpointCommandCallback(WatchpointOptions *wp_options,
                                          CommandReturnObject &result) override;

  Status SetBreakpointCommandCallback(BreakpointOptions &bp_options,
                                      const char *command_body_text,
                                      bool is_callback) override;

  void SetWatchpointCommandCallback(WatchpointOptions *wp_options,
                                    const char *command_body_text,
                                    bool is_callback) override;

  Status SetBreakpointCommandCallbackFunction(
      BreakpointOptions &bp_options, const char *function_name,
      StructuredData::ObjectSP extra_args_sp) override;

private:
  std::unique_ptr<Lua> m_lua;
  bool m_session_is_active = false;

  Status RegisterBreakpointCallback(BreakpointOptions &bp_options,
                                    const char *command_body_text,
                                    StructuredData::ObjectSP extra_args_sp);

  Status RegisterWatchpointCallback(WatchpointOptions *wp_options,
                                    const char *command_body_text,
                                    StructuredData::ObjectSP extra_args_sp);
};

} // namespace lldb_private

#endif // liblldb_ScriptInterpreterLua_h_
