//===-- ScriptInterpreterLua.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ScriptInterpreterLua.h"
#include "Lua.h"
#include "lldb/Breakpoint/StoppointCallbackContext.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/Timer.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FormatAdapters.h"
#include <memory>
#include <vector>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ScriptInterpreterLua)

enum ActiveIOHandler {
  eIOHandlerNone,
  eIOHandlerBreakpoint,
  eIOHandlerWatchpoint
};

class IOHandlerLuaInterpreter : public IOHandlerDelegate,
                                public IOHandlerEditline {
public:
  IOHandlerLuaInterpreter(Debugger &debugger,
                          ScriptInterpreterLua &script_interpreter,
                          ActiveIOHandler active_io_handler = eIOHandlerNone)
      : IOHandlerEditline(debugger, IOHandler::Type::LuaInterpreter, "lua",
                          llvm::StringRef(">>> "), llvm::StringRef("..> "),
                          true, debugger.GetUseColor(), 0, *this),
        m_script_interpreter(script_interpreter),
        m_active_io_handler(active_io_handler) {
    llvm::cantFail(m_script_interpreter.GetLua().ChangeIO(
        debugger.GetOutputFile().GetStream(),
        debugger.GetErrorFile().GetStream()));
    llvm::cantFail(m_script_interpreter.EnterSession(debugger.GetID()));
  }

  ~IOHandlerLuaInterpreter() override {
    llvm::cantFail(m_script_interpreter.LeaveSession());
  }

  void IOHandlerActivated(IOHandler &io_handler, bool interactive) override {
    const char *instructions = nullptr;
    switch (m_active_io_handler) {
    case eIOHandlerNone:
      break;
    case eIOHandlerWatchpoint:
      instructions = "Enter your Lua command(s). Type 'quit' to end.\n"
                     "The commands are compiled as the body of the following "
                     "Lua function\n"
                     "function (frame, wp) end\n";
      SetPrompt(llvm::StringRef("..> "));
      break;
    case eIOHandlerBreakpoint:
      instructions = "Enter your Lua command(s). Type 'quit' to end.\n"
                     "The commands are compiled as the body of the following "
                     "Lua function\n"
                     "function (frame, bp_loc, ...) end\n";
      SetPrompt(llvm::StringRef("..> "));
      break;
    }
    if (instructions == nullptr)
      return;
    if (interactive)
      *io_handler.GetOutputStreamFileSP() << instructions;
  }

  bool IOHandlerIsInputComplete(IOHandler &io_handler,
                                StringList &lines) override {
    size_t last = lines.GetSize() - 1;
    if (IsQuitCommand(lines.GetStringAtIndex(last))) {
      if (m_active_io_handler == eIOHandlerBreakpoint ||
          m_active_io_handler == eIOHandlerWatchpoint)
        lines.DeleteStringAtIndex(last);
      return true;
    }
    StreamString str;
    lines.Join("\n", str);
    if (llvm::Error E =
            m_script_interpreter.GetLua().CheckSyntax(str.GetString())) {
      std::string error_str = toString(std::move(E));
      // Lua always errors out to incomplete code with '<eof>'
      return error_str.find("<eof>") == std::string::npos;
    }
    // The breakpoint and watchpoint handler only exits with a explicit 'quit'
    return m_active_io_handler != eIOHandlerBreakpoint &&
           m_active_io_handler != eIOHandlerWatchpoint;
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &data) override {
    switch (m_active_io_handler) {
    case eIOHandlerBreakpoint: {
      auto *bp_options_vec =
          static_cast<std::vector<std::reference_wrapper<BreakpointOptions>> *>(
              io_handler.GetUserData());
      for (BreakpointOptions &bp_options : *bp_options_vec) {
        Status error = m_script_interpreter.SetBreakpointCommandCallback(
            bp_options, data.c_str(), /*is_callback=*/false);
        if (error.Fail())
          *io_handler.GetErrorStreamFileSP() << error.AsCString() << '\n';
      }
      io_handler.SetIsDone(true);
    } break;
    case eIOHandlerWatchpoint: {
      auto *wp_options =
          static_cast<WatchpointOptions *>(io_handler.GetUserData());
      m_script_interpreter.SetWatchpointCommandCallback(wp_options,
                                                        data.c_str(),
                                                        /*is_callback=*/false);
      io_handler.SetIsDone(true);
    } break;
    case eIOHandlerNone:
      if (IsQuitCommand(data)) {
        io_handler.SetIsDone(true);
        return;
      }
      if (llvm::Error error = m_script_interpreter.GetLua().Run(data))
        *io_handler.GetErrorStreamFileSP() << toString(std::move(error));
      break;
    }
  }

private:
  ScriptInterpreterLua &m_script_interpreter;
  ActiveIOHandler m_active_io_handler;

  bool IsQuitCommand(llvm::StringRef cmd) { return cmd.rtrim() == "quit"; }
};

ScriptInterpreterLua::ScriptInterpreterLua(Debugger &debugger)
    : ScriptInterpreter(debugger, eScriptLanguageLua),
      m_lua(std::make_unique<Lua>()) {}

ScriptInterpreterLua::~ScriptInterpreterLua() = default;

StructuredData::DictionarySP ScriptInterpreterLua::GetInterpreterInfo() {
  auto info = std::make_shared<StructuredData::Dictionary>();
  info->AddStringItem("language", "lua");
  return info;
}

bool ScriptInterpreterLua::ExecuteOneLine(llvm::StringRef command,
                                          CommandReturnObject *result,
                                          const ExecuteScriptOptions &options) {
  if (command.empty()) {
    if (result)
      result->AppendError("empty command passed to lua\n");
    return false;
  }

  llvm::Expected<std::unique_ptr<ScriptInterpreterIORedirect>>
      io_redirect_or_error = ScriptInterpreterIORedirect::Create(
          options.GetEnableIO(), m_debugger, result);
  if (!io_redirect_or_error) {
    if (result)
      result->AppendErrorWithFormatv(
          "failed to redirect I/O: {0}\n",
          llvm::fmt_consume(io_redirect_or_error.takeError()));
    else
      llvm::consumeError(io_redirect_or_error.takeError());
    return false;
  }

  ScriptInterpreterIORedirect &io_redirect = **io_redirect_or_error;

  if (llvm::Error e =
          m_lua->ChangeIO(io_redirect.GetOutputFile()->GetStream(),
                          io_redirect.GetErrorFile()->GetStream())) {
    result->AppendErrorWithFormatv("lua failed to redirect I/O: {0}\n",
                                   llvm::toString(std::move(e)));
    return false;
  }

  if (llvm::Error e = m_lua->Run(command)) {
    result->AppendErrorWithFormatv(
        "lua failed attempting to evaluate '{0}': {1}\n", command,
        llvm::toString(std::move(e)));
    return false;
  }

  io_redirect.Flush();
  return true;
}

void ScriptInterpreterLua::ExecuteInterpreterLoop() {
  LLDB_SCOPED_TIMER();

  // At the moment, the only time the debugger does not have an input file
  // handle is when this is called directly from lua, in which case it is
  // both dangerous and unnecessary (not to mention confusing) to try to embed
  // a running interpreter loop inside the already running lua interpreter
  // loop, so we won't do it.
  if (!m_debugger.GetInputFile().IsValid())
    return;

  IOHandlerSP io_handler_sp(new IOHandlerLuaInterpreter(m_debugger, *this));
  m_debugger.RunIOHandlerAsync(io_handler_sp);
}

bool ScriptInterpreterLua::LoadScriptingModule(
    const char *filename, const LoadScriptOptions &options,
    lldb_private::Status &error, StructuredData::ObjectSP *module_sp,
    FileSpec extra_search_dir) {

  if (llvm::Error e = m_lua->LoadModule(filename)) {
    error.SetErrorStringWithFormatv("lua failed to import '{0}': {1}\n",
                                    filename, llvm::toString(std::move(e)));
    return false;
  }
  return true;
}

void ScriptInterpreterLua::Initialize() {
  static llvm::once_flag g_once_flag;

  llvm::call_once(g_once_flag, []() {
    PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                  GetPluginDescriptionStatic(),
                                  lldb::eScriptLanguageLua, CreateInstance);
  });
}

void ScriptInterpreterLua::Terminate() {}

llvm::Error ScriptInterpreterLua::EnterSession(user_id_t debugger_id) {
  if (m_session_is_active)
    return llvm::Error::success();

  const char *fmt_str =
      "lldb.debugger = lldb.SBDebugger.FindDebuggerWithID({0}); "
      "lldb.target = lldb.debugger:GetSelectedTarget(); "
      "lldb.process = lldb.target:GetProcess(); "
      "lldb.thread = lldb.process:GetSelectedThread(); "
      "lldb.frame = lldb.thread:GetSelectedFrame()";
  return m_lua->Run(llvm::formatv(fmt_str, debugger_id).str());
}

llvm::Error ScriptInterpreterLua::LeaveSession() {
  if (!m_session_is_active)
    return llvm::Error::success();

  m_session_is_active = false;

  llvm::StringRef str = "lldb.debugger = nil; "
                        "lldb.target = nil; "
                        "lldb.process = nil; "
                        "lldb.thread = nil; "
                        "lldb.frame = nil";
  return m_lua->Run(str);
}

bool ScriptInterpreterLua::BreakpointCallbackFunction(
    void *baton, StoppointCallbackContext *context, user_id_t break_id,
    user_id_t break_loc_id) {
  assert(context);

  ExecutionContext exe_ctx(context->exe_ctx_ref);
  Target *target = exe_ctx.GetTargetPtr();
  if (target == nullptr)
    return true;

  StackFrameSP stop_frame_sp(exe_ctx.GetFrameSP());
  BreakpointSP breakpoint_sp = target->GetBreakpointByID(break_id);
  BreakpointLocationSP bp_loc_sp(breakpoint_sp->FindLocationByID(break_loc_id));

  Debugger &debugger = target->GetDebugger();
  ScriptInterpreterLua *lua_interpreter = static_cast<ScriptInterpreterLua *>(
      debugger.GetScriptInterpreter(true, eScriptLanguageLua));
  Lua &lua = lua_interpreter->GetLua();

  CommandDataLua *bp_option_data = static_cast<CommandDataLua *>(baton);
  llvm::Expected<bool> BoolOrErr = lua.CallBreakpointCallback(
      baton, stop_frame_sp, bp_loc_sp, bp_option_data->m_extra_args_sp);
  if (llvm::Error E = BoolOrErr.takeError()) {
    debugger.GetErrorStream() << toString(std::move(E));
    return true;
  }

  return *BoolOrErr;
}

bool ScriptInterpreterLua::WatchpointCallbackFunction(
    void *baton, StoppointCallbackContext *context, user_id_t watch_id) {
  assert(context);

  ExecutionContext exe_ctx(context->exe_ctx_ref);
  Target *target = exe_ctx.GetTargetPtr();
  if (target == nullptr)
    return true;

  StackFrameSP stop_frame_sp(exe_ctx.GetFrameSP());
  WatchpointSP wp_sp = target->GetWatchpointList().FindByID(watch_id);

  Debugger &debugger = target->GetDebugger();
  ScriptInterpreterLua *lua_interpreter = static_cast<ScriptInterpreterLua *>(
      debugger.GetScriptInterpreter(true, eScriptLanguageLua));
  Lua &lua = lua_interpreter->GetLua();

  llvm::Expected<bool> BoolOrErr =
      lua.CallWatchpointCallback(baton, stop_frame_sp, wp_sp);
  if (llvm::Error E = BoolOrErr.takeError()) {
    debugger.GetErrorStream() << toString(std::move(E));
    return true;
  }

  return *BoolOrErr;
}

void ScriptInterpreterLua::CollectDataForBreakpointCommandCallback(
    std::vector<std::reference_wrapper<BreakpointOptions>> &bp_options_vec,
    CommandReturnObject &result) {
  IOHandlerSP io_handler_sp(
      new IOHandlerLuaInterpreter(m_debugger, *this, eIOHandlerBreakpoint));
  io_handler_sp->SetUserData(&bp_options_vec);
  m_debugger.RunIOHandlerAsync(io_handler_sp);
}

void ScriptInterpreterLua::CollectDataForWatchpointCommandCallback(
    WatchpointOptions *wp_options, CommandReturnObject &result) {
  IOHandlerSP io_handler_sp(
      new IOHandlerLuaInterpreter(m_debugger, *this, eIOHandlerWatchpoint));
  io_handler_sp->SetUserData(wp_options);
  m_debugger.RunIOHandlerAsync(io_handler_sp);
}

Status ScriptInterpreterLua::SetBreakpointCommandCallbackFunction(
    BreakpointOptions &bp_options, const char *function_name,
    StructuredData::ObjectSP extra_args_sp) {
  const char *fmt_str = "return {0}(frame, bp_loc, ...)";
  std::string oneliner = llvm::formatv(fmt_str, function_name).str();
  return RegisterBreakpointCallback(bp_options, oneliner.c_str(),
                                    extra_args_sp);
}

Status ScriptInterpreterLua::SetBreakpointCommandCallback(
    BreakpointOptions &bp_options, const char *command_body_text,
    bool is_callback) {
  return RegisterBreakpointCallback(bp_options, command_body_text, {});
}

Status ScriptInterpreterLua::RegisterBreakpointCallback(
    BreakpointOptions &bp_options, const char *command_body_text,
    StructuredData::ObjectSP extra_args_sp) {
  Status error;
  auto data_up = std::make_unique<CommandDataLua>(extra_args_sp);
  error = m_lua->RegisterBreakpointCallback(data_up.get(), command_body_text);
  if (error.Fail())
    return error;
  auto baton_sp =
      std::make_shared<BreakpointOptions::CommandBaton>(std::move(data_up));
  bp_options.SetCallback(ScriptInterpreterLua::BreakpointCallbackFunction,
                         baton_sp);
  return error;
}

void ScriptInterpreterLua::SetWatchpointCommandCallback(
    WatchpointOptions *wp_options, const char *command_body_text,
    bool is_callback) {
  RegisterWatchpointCallback(wp_options, command_body_text, {});
}

Status ScriptInterpreterLua::RegisterWatchpointCallback(
    WatchpointOptions *wp_options, const char *command_body_text,
    StructuredData::ObjectSP extra_args_sp) {
  Status error;
  auto data_up = std::make_unique<WatchpointOptions::CommandData>();
  error = m_lua->RegisterWatchpointCallback(data_up.get(), command_body_text);
  if (error.Fail())
    return error;
  auto baton_sp =
      std::make_shared<WatchpointOptions::CommandBaton>(std::move(data_up));
  wp_options->SetCallback(ScriptInterpreterLua::WatchpointCallbackFunction,
                          baton_sp);
  return error;
}

lldb::ScriptInterpreterSP
ScriptInterpreterLua::CreateInstance(Debugger &debugger) {
  return std::make_shared<ScriptInterpreterLua>(debugger);
}

llvm::StringRef ScriptInterpreterLua::GetPluginDescriptionStatic() {
  return "Lua script interpreter";
}

Lua &ScriptInterpreterLua::GetLua() { return *m_lua; }
