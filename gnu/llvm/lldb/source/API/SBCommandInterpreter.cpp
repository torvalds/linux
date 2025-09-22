//===-- SBCommandInterpreter.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-types.h"

#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Listener.h"

#include "lldb/API/SBBroadcaster.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/API/SBCommandInterpreterRunOptions.h"
#include "lldb/API/SBCommandReturnObject.h"
#include "lldb/API/SBEvent.h"
#include "lldb/API/SBExecutionContext.h"
#include "lldb/API/SBListener.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/API/SBTarget.h"

#include <memory>
#include <optional>

using namespace lldb;
using namespace lldb_private;

namespace lldb_private {
class CommandPluginInterfaceImplementation : public CommandObjectParsed {
public:
  CommandPluginInterfaceImplementation(CommandInterpreter &interpreter,
                                       const char *name,
                                       lldb::SBCommandPluginInterface *backend,
                                       const char *help = nullptr,
                                       const char *syntax = nullptr,
                                       uint32_t flags = 0,
                                       const char *auto_repeat_command = "")
      : CommandObjectParsed(interpreter, name, help, syntax, flags),
        m_backend(backend) {
    m_auto_repeat_command =
        auto_repeat_command == nullptr
            ? std::nullopt
            : std::optional<std::string>(auto_repeat_command);
    // We don't know whether any given command coming from this interface takes
    // arguments or not so here we're just disabling the basic args check.
    CommandArgumentData none_arg{eArgTypeNone, eArgRepeatStar};
    m_arguments.push_back({none_arg});
  }

  bool IsRemovable() const override { return true; }

  /// More documentation is available in lldb::CommandObject::GetRepeatCommand,
  /// but in short, if std::nullopt is returned, the previous command will be
  /// repeated, and if an empty string is returned, no commands will be
  /// executed.
  std::optional<std::string> GetRepeatCommand(Args &current_command_args,
                                              uint32_t index) override {
    if (!m_auto_repeat_command)
      return std::nullopt;
    else
      return m_auto_repeat_command;
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    SBCommandReturnObject sb_return(result);
    SBCommandInterpreter sb_interpreter(&m_interpreter);
    SBDebugger debugger_sb(m_interpreter.GetDebugger().shared_from_this());
    m_backend->DoExecute(debugger_sb, command.GetArgumentVector(), sb_return);
  }
  std::shared_ptr<lldb::SBCommandPluginInterface> m_backend;
  std::optional<std::string> m_auto_repeat_command;
};
} // namespace lldb_private

SBCommandInterpreter::SBCommandInterpreter() : m_opaque_ptr() {
  LLDB_INSTRUMENT_VA(this);
}

SBCommandInterpreter::SBCommandInterpreter(CommandInterpreter *interpreter)
    : m_opaque_ptr(interpreter) {
  LLDB_INSTRUMENT_VA(this, interpreter);
}

SBCommandInterpreter::SBCommandInterpreter(const SBCommandInterpreter &rhs)
    : m_opaque_ptr(rhs.m_opaque_ptr) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBCommandInterpreter::~SBCommandInterpreter() = default;

const SBCommandInterpreter &SBCommandInterpreter::
operator=(const SBCommandInterpreter &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_ptr = rhs.m_opaque_ptr;
  return *this;
}

bool SBCommandInterpreter::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBCommandInterpreter::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_ptr != nullptr;
}

bool SBCommandInterpreter::CommandExists(const char *cmd) {
  LLDB_INSTRUMENT_VA(this, cmd);

  return (((cmd != nullptr) && IsValid()) ? m_opaque_ptr->CommandExists(cmd)
                                          : false);
}

bool SBCommandInterpreter::UserCommandExists(const char *cmd) {
  LLDB_INSTRUMENT_VA(this, cmd);

  return (((cmd != nullptr) && IsValid()) ? m_opaque_ptr->UserCommandExists(cmd)
                                          : false);
}

bool SBCommandInterpreter::AliasExists(const char *cmd) {
  LLDB_INSTRUMENT_VA(this, cmd);

  return (((cmd != nullptr) && IsValid()) ? m_opaque_ptr->AliasExists(cmd)
                                          : false);
}

bool SBCommandInterpreter::IsActive() {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? m_opaque_ptr->IsActive() : false);
}

bool SBCommandInterpreter::WasInterrupted() const {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? m_opaque_ptr->GetDebugger().InterruptRequested() : false);
}

bool SBCommandInterpreter::InterruptCommand() {
  LLDB_INSTRUMENT_VA(this);
  
  return (IsValid() ? m_opaque_ptr->InterruptCommand() : false);
}

const char *SBCommandInterpreter::GetIOHandlerControlSequence(char ch) {
  LLDB_INSTRUMENT_VA(this, ch);

  if (!IsValid())
    return nullptr;

  return ConstString(
             m_opaque_ptr->GetDebugger().GetTopIOHandlerControlSequence(ch))
      .GetCString();
}

lldb::ReturnStatus
SBCommandInterpreter::HandleCommand(const char *command_line,
                                    SBCommandReturnObject &result,
                                    bool add_to_history) {
  LLDB_INSTRUMENT_VA(this, command_line, result, add_to_history);

  SBExecutionContext sb_exe_ctx;
  return HandleCommand(command_line, sb_exe_ctx, result, add_to_history);
}

lldb::ReturnStatus SBCommandInterpreter::HandleCommand(
    const char *command_line, SBExecutionContext &override_context,
    SBCommandReturnObject &result, bool add_to_history) {
  LLDB_INSTRUMENT_VA(this, command_line, override_context, result,
                     add_to_history);

  result.Clear();
  if (command_line && IsValid()) {
    result.ref().SetInteractive(false);
    auto do_add_to_history = add_to_history ? eLazyBoolYes : eLazyBoolNo;
    if (override_context.get())
      m_opaque_ptr->HandleCommand(command_line, do_add_to_history,
                                  override_context.get()->Lock(true),
                                  result.ref());
    else
      m_opaque_ptr->HandleCommand(command_line, do_add_to_history,
                                  result.ref());
  } else {
    result->AppendError(
        "SBCommandInterpreter or the command line is not valid");
  }

  return result.GetStatus();
}

void SBCommandInterpreter::HandleCommandsFromFile(
    lldb::SBFileSpec &file, lldb::SBExecutionContext &override_context,
    lldb::SBCommandInterpreterRunOptions &options,
    lldb::SBCommandReturnObject result) {
  LLDB_INSTRUMENT_VA(this, file, override_context, options, result);

  if (!IsValid()) {
    result->AppendError("SBCommandInterpreter is not valid.");
    return;
  }

  if (!file.IsValid()) {
    SBStream s;
    file.GetDescription(s);
    result->AppendErrorWithFormat("File is not valid: %s.", s.GetData());
  }

  FileSpec tmp_spec = file.ref();
  if (override_context.get())
    m_opaque_ptr->HandleCommandsFromFile(tmp_spec,
                                         override_context.get()->Lock(true),
                                         options.ref(),
                                         result.ref());

  else
    m_opaque_ptr->HandleCommandsFromFile(tmp_spec, options.ref(), result.ref());
}

int SBCommandInterpreter::HandleCompletion(
    const char *current_line, const char *cursor, const char *last_char,
    int match_start_point, int max_return_elements, SBStringList &matches) {
  LLDB_INSTRUMENT_VA(this, current_line, cursor, last_char, match_start_point,
                     max_return_elements, matches);

  SBStringList dummy_descriptions;
  return HandleCompletionWithDescriptions(
      current_line, cursor, last_char, match_start_point, max_return_elements,
      matches, dummy_descriptions);
}

int SBCommandInterpreter::HandleCompletionWithDescriptions(
    const char *current_line, const char *cursor, const char *last_char,
    int match_start_point, int max_return_elements, SBStringList &matches,
    SBStringList &descriptions) {
  LLDB_INSTRUMENT_VA(this, current_line, cursor, last_char, match_start_point,
                     max_return_elements, matches, descriptions);

  // Sanity check the arguments that are passed in: cursor & last_char have to
  // be within the current_line.
  if (current_line == nullptr || cursor == nullptr || last_char == nullptr)
    return 0;

  if (cursor < current_line || last_char < current_line)
    return 0;

  size_t current_line_size = strlen(current_line);
  if (cursor - current_line > static_cast<ptrdiff_t>(current_line_size) ||
      last_char - current_line > static_cast<ptrdiff_t>(current_line_size))
    return 0;

  if (!IsValid())
    return 0;

  lldb_private::StringList lldb_matches, lldb_descriptions;
  CompletionResult result;
  CompletionRequest request(current_line, cursor - current_line, result);
  m_opaque_ptr->HandleCompletion(request);
  result.GetMatches(lldb_matches);
  result.GetDescriptions(lldb_descriptions);

  // Make the result array indexed from 1 again by adding the 'common prefix'
  // of all completions as element 0. This is done to emulate the old API.
  if (request.GetParsedLine().GetArgumentCount() == 0) {
    // If we got an empty string, insert nothing.
    lldb_matches.InsertStringAtIndex(0, "");
    lldb_descriptions.InsertStringAtIndex(0, "");
  } else {
    // Now figure out if there is a common substring, and if so put that in
    // element 0, otherwise put an empty string in element 0.
    std::string command_partial_str = request.GetCursorArgumentPrefix().str();

    std::string common_prefix = lldb_matches.LongestCommonPrefix();
    const size_t partial_name_len = command_partial_str.size();
    common_prefix.erase(0, partial_name_len);

    // If we matched a unique single command, add a space... Only do this if
    // the completer told us this was a complete word, however...
    if (lldb_matches.GetSize() == 1) {
      char quote_char = request.GetParsedArg().GetQuoteChar();
      common_prefix =
          Args::EscapeLLDBCommandArgument(common_prefix, quote_char);
      if (request.GetParsedArg().IsQuoted())
        common_prefix.push_back(quote_char);
      common_prefix.push_back(' ');
    }
    lldb_matches.InsertStringAtIndex(0, common_prefix.c_str());
    lldb_descriptions.InsertStringAtIndex(0, "");
  }

  SBStringList temp_matches_list(&lldb_matches);
  matches.AppendList(temp_matches_list);
  SBStringList temp_descriptions_list(&lldb_descriptions);
  descriptions.AppendList(temp_descriptions_list);
  return result.GetNumberOfResults();
}

int SBCommandInterpreter::HandleCompletionWithDescriptions(
    const char *current_line, uint32_t cursor_pos, int match_start_point,
    int max_return_elements, SBStringList &matches,
    SBStringList &descriptions) {
  LLDB_INSTRUMENT_VA(this, current_line, cursor_pos, match_start_point,
                     max_return_elements, matches, descriptions);

  const char *cursor = current_line + cursor_pos;
  const char *last_char = current_line + strlen(current_line);
  return HandleCompletionWithDescriptions(
      current_line, cursor, last_char, match_start_point, max_return_elements,
      matches, descriptions);
}

int SBCommandInterpreter::HandleCompletion(const char *current_line,
                                           uint32_t cursor_pos,
                                           int match_start_point,
                                           int max_return_elements,
                                           lldb::SBStringList &matches) {
  LLDB_INSTRUMENT_VA(this, current_line, cursor_pos, match_start_point,
                     max_return_elements, matches);

  const char *cursor = current_line + cursor_pos;
  const char *last_char = current_line + strlen(current_line);
  return HandleCompletion(current_line, cursor, last_char, match_start_point,
                          max_return_elements, matches);
}

bool SBCommandInterpreter::HasCommands() {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? m_opaque_ptr->HasCommands() : false);
}

bool SBCommandInterpreter::HasAliases() {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? m_opaque_ptr->HasAliases() : false);
}

bool SBCommandInterpreter::HasAliasOptions() {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? m_opaque_ptr->HasAliasOptions() : false);
}

bool SBCommandInterpreter::IsInteractive() {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? m_opaque_ptr->IsInteractive() : false);
}

SBProcess SBCommandInterpreter::GetProcess() {
  LLDB_INSTRUMENT_VA(this);

  SBProcess sb_process;
  ProcessSP process_sp;
  if (IsValid()) {
    TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
    if (target_sp) {
      std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
      process_sp = target_sp->GetProcessSP();
      sb_process.SetSP(process_sp);
    }
  }

  return sb_process;
}

SBDebugger SBCommandInterpreter::GetDebugger() {
  LLDB_INSTRUMENT_VA(this);

  SBDebugger sb_debugger;
  if (IsValid())
    sb_debugger.reset(m_opaque_ptr->GetDebugger().shared_from_this());

  return sb_debugger;
}

bool SBCommandInterpreter::GetPromptOnQuit() {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? m_opaque_ptr->GetPromptOnQuit() : false);
}

void SBCommandInterpreter::SetPromptOnQuit(bool b) {
  LLDB_INSTRUMENT_VA(this, b);

  if (IsValid())
    m_opaque_ptr->SetPromptOnQuit(b);
}

void SBCommandInterpreter::AllowExitCodeOnQuit(bool allow) {
  LLDB_INSTRUMENT_VA(this, allow);

  if (m_opaque_ptr)
    m_opaque_ptr->AllowExitCodeOnQuit(allow);
}

bool SBCommandInterpreter::HasCustomQuitExitCode() {
  LLDB_INSTRUMENT_VA(this);

  bool exited = false;
  if (m_opaque_ptr)
    m_opaque_ptr->GetQuitExitCode(exited);
  return exited;
}

int SBCommandInterpreter::GetQuitStatus() {
  LLDB_INSTRUMENT_VA(this);

  bool exited = false;
  return (m_opaque_ptr ? m_opaque_ptr->GetQuitExitCode(exited) : 0);
}

void SBCommandInterpreter::ResolveCommand(const char *command_line,
                                          SBCommandReturnObject &result) {
  LLDB_INSTRUMENT_VA(this, command_line, result);

  result.Clear();
  if (command_line && IsValid()) {
    m_opaque_ptr->ResolveCommand(command_line, result.ref());
  } else {
    result->AppendError(
        "SBCommandInterpreter or the command line is not valid");
  }
}

CommandInterpreter *SBCommandInterpreter::get() { return m_opaque_ptr; }

CommandInterpreter &SBCommandInterpreter::ref() {
  assert(m_opaque_ptr);
  return *m_opaque_ptr;
}

void SBCommandInterpreter::reset(
    lldb_private::CommandInterpreter *interpreter) {
  m_opaque_ptr = interpreter;
}

void SBCommandInterpreter::SourceInitFileInGlobalDirectory(
    SBCommandReturnObject &result) {
  LLDB_INSTRUMENT_VA(this, result);

  result.Clear();
  if (IsValid()) {
    TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp)
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());
    m_opaque_ptr->SourceInitFileGlobal(result.ref());
  } else {
    result->AppendError("SBCommandInterpreter is not valid");
  }
}

void SBCommandInterpreter::SourceInitFileInHomeDirectory(
    SBCommandReturnObject &result) {
  LLDB_INSTRUMENT_VA(this, result);

  SourceInitFileInHomeDirectory(result, /*is_repl=*/false);
}

void SBCommandInterpreter::SourceInitFileInHomeDirectory(
    SBCommandReturnObject &result, bool is_repl) {
  LLDB_INSTRUMENT_VA(this, result, is_repl);

  result.Clear();
  if (IsValid()) {
    TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp)
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());
    m_opaque_ptr->SourceInitFileHome(result.ref(), is_repl);
  } else {
    result->AppendError("SBCommandInterpreter is not valid");
  }
}

void SBCommandInterpreter::SourceInitFileInCurrentWorkingDirectory(
    SBCommandReturnObject &result) {
  LLDB_INSTRUMENT_VA(this, result);

  result.Clear();
  if (IsValid()) {
    TargetSP target_sp(m_opaque_ptr->GetDebugger().GetSelectedTarget());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp)
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());
    m_opaque_ptr->SourceInitFileCwd(result.ref());
  } else {
    result->AppendError("SBCommandInterpreter is not valid");
  }
}

SBBroadcaster SBCommandInterpreter::GetBroadcaster() {
  LLDB_INSTRUMENT_VA(this);

  SBBroadcaster broadcaster(m_opaque_ptr, false);

  return broadcaster;
}

const char *SBCommandInterpreter::GetBroadcasterClass() {
  LLDB_INSTRUMENT();

  return ConstString(CommandInterpreter::GetStaticBroadcasterClass())
      .AsCString();
}

const char *SBCommandInterpreter::GetArgumentTypeAsCString(
    const lldb::CommandArgumentType arg_type) {
  LLDB_INSTRUMENT_VA(arg_type);

  return ConstString(CommandObject::GetArgumentTypeAsCString(arg_type))
      .GetCString();
}

const char *SBCommandInterpreter::GetArgumentDescriptionAsCString(
    const lldb::CommandArgumentType arg_type) {
  LLDB_INSTRUMENT_VA(arg_type);

  return ConstString(CommandObject::GetArgumentDescriptionAsCString(arg_type))
      .GetCString();
}

bool SBCommandInterpreter::EventIsCommandInterpreterEvent(
    const lldb::SBEvent &event) {
  LLDB_INSTRUMENT_VA(event);

  return event.GetBroadcasterClass() ==
         SBCommandInterpreter::GetBroadcasterClass();
}

bool SBCommandInterpreter::SetCommandOverrideCallback(
    const char *command_name, lldb::CommandOverrideCallback callback,
    void *baton) {
  LLDB_INSTRUMENT_VA(this, command_name, callback, baton);

  if (command_name && command_name[0] && IsValid()) {
    llvm::StringRef command_name_str = command_name;
    CommandObject *cmd_obj =
        m_opaque_ptr->GetCommandObjectForCommand(command_name_str);
    if (cmd_obj) {
      assert(command_name_str.empty());
      cmd_obj->SetOverrideCallback(callback, baton);
      return true;
    }
  }
  return false;
}

SBStructuredData SBCommandInterpreter::GetStatistics() {
  LLDB_INSTRUMENT_VA(this);

  SBStructuredData data;
  if (!IsValid())
    return data;

  std::string json_str =
      llvm::formatv("{0:2}", m_opaque_ptr->GetStatistics()).str();
  data.m_impl_up->SetObjectSP(StructuredData::ParseJSON(json_str));
  return data;
}

SBStructuredData SBCommandInterpreter::GetTranscript() {
  LLDB_INSTRUMENT_VA(this);

  SBStructuredData data;
  if (IsValid())
    // A deep copy is performed by `std::make_shared` on the
    // `StructuredData::Array`, via its implicitly-declared copy constructor.
    // This ensures thread-safety between the user changing the returned
    // `SBStructuredData` and the `CommandInterpreter` changing its internal
    // `m_transcript`.
    data.m_impl_up->SetObjectSP(
        std::make_shared<StructuredData::Array>(m_opaque_ptr->GetTranscript()));
  return data;
}

lldb::SBCommand SBCommandInterpreter::AddMultiwordCommand(const char *name,
                                                          const char *help) {
  LLDB_INSTRUMENT_VA(this, name, help);

  lldb::CommandObjectSP new_command_sp(
      new CommandObjectMultiword(*m_opaque_ptr, name, help));
  new_command_sp->GetAsMultiwordCommand()->SetRemovable(true);
  Status add_error = m_opaque_ptr->AddUserCommand(name, new_command_sp, true);
  if (add_error.Success())
    return lldb::SBCommand(new_command_sp);
  return lldb::SBCommand();
}

lldb::SBCommand SBCommandInterpreter::AddCommand(
    const char *name, lldb::SBCommandPluginInterface *impl, const char *help) {
  LLDB_INSTRUMENT_VA(this, name, impl, help);

  return AddCommand(name, impl, help, /*syntax=*/nullptr,
                    /*auto_repeat_command=*/"");
}

lldb::SBCommand
SBCommandInterpreter::AddCommand(const char *name,
                                 lldb::SBCommandPluginInterface *impl,
                                 const char *help, const char *syntax) {
  LLDB_INSTRUMENT_VA(this, name, impl, help, syntax);
  return AddCommand(name, impl, help, syntax, /*auto_repeat_command=*/"");
}

lldb::SBCommand SBCommandInterpreter::AddCommand(
    const char *name, lldb::SBCommandPluginInterface *impl, const char *help,
    const char *syntax, const char *auto_repeat_command) {
  LLDB_INSTRUMENT_VA(this, name, impl, help, syntax, auto_repeat_command);

  lldb::CommandObjectSP new_command_sp;
  new_command_sp = std::make_shared<CommandPluginInterfaceImplementation>(
      *m_opaque_ptr, name, impl, help, syntax, /*flags=*/0,
      auto_repeat_command);

  Status add_error = m_opaque_ptr->AddUserCommand(name, new_command_sp, true);
  if (add_error.Success())
    return lldb::SBCommand(new_command_sp);
  return lldb::SBCommand();
}

SBCommand::SBCommand() { LLDB_INSTRUMENT_VA(this); }

SBCommand::SBCommand(lldb::CommandObjectSP cmd_sp) : m_opaque_sp(cmd_sp) {}

bool SBCommand::IsValid() {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBCommand::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get() != nullptr;
}

const char *SBCommand::GetName() {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? ConstString(m_opaque_sp->GetCommandName()).AsCString() : nullptr);
}

const char *SBCommand::GetHelp() {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? ConstString(m_opaque_sp->GetHelp()).AsCString()
                    : nullptr);
}

const char *SBCommand::GetHelpLong() {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? ConstString(m_opaque_sp->GetHelpLong()).AsCString()
                    : nullptr);
}

void SBCommand::SetHelp(const char *help) {
  LLDB_INSTRUMENT_VA(this, help);

  if (IsValid())
    m_opaque_sp->SetHelp(help);
}

void SBCommand::SetHelpLong(const char *help) {
  LLDB_INSTRUMENT_VA(this, help);

  if (IsValid())
    m_opaque_sp->SetHelpLong(help);
}

lldb::SBCommand SBCommand::AddMultiwordCommand(const char *name,
                                               const char *help) {
  LLDB_INSTRUMENT_VA(this, name, help);

  if (!IsValid())
    return lldb::SBCommand();
  if (!m_opaque_sp->IsMultiwordObject())
    return lldb::SBCommand();
  CommandObjectMultiword *new_command = new CommandObjectMultiword(
      m_opaque_sp->GetCommandInterpreter(), name, help);
  new_command->SetRemovable(true);
  lldb::CommandObjectSP new_command_sp(new_command);
  if (new_command_sp && m_opaque_sp->LoadSubCommand(name, new_command_sp))
    return lldb::SBCommand(new_command_sp);
  return lldb::SBCommand();
}

lldb::SBCommand SBCommand::AddCommand(const char *name,
                                      lldb::SBCommandPluginInterface *impl,
                                      const char *help) {
  LLDB_INSTRUMENT_VA(this, name, impl, help);
  return AddCommand(name, impl, help, /*syntax=*/nullptr,
                    /*auto_repeat_command=*/"");
}

lldb::SBCommand SBCommand::AddCommand(const char *name,
                                      lldb::SBCommandPluginInterface *impl,
                                      const char *help, const char *syntax) {
  LLDB_INSTRUMENT_VA(this, name, impl, help, syntax);
  return AddCommand(name, impl, help, syntax, /*auto_repeat_command=*/"");
}

lldb::SBCommand SBCommand::AddCommand(const char *name,
                                      lldb::SBCommandPluginInterface *impl,
                                      const char *help, const char *syntax,
                                      const char *auto_repeat_command) {
  LLDB_INSTRUMENT_VA(this, name, impl, help, syntax, auto_repeat_command);

  if (!IsValid())
    return lldb::SBCommand();
  if (!m_opaque_sp->IsMultiwordObject())
    return lldb::SBCommand();
  lldb::CommandObjectSP new_command_sp;
  new_command_sp = std::make_shared<CommandPluginInterfaceImplementation>(
      m_opaque_sp->GetCommandInterpreter(), name, impl, help, syntax,
      /*flags=*/0, auto_repeat_command);
  if (new_command_sp && m_opaque_sp->LoadSubCommand(name, new_command_sp))
    return lldb::SBCommand(new_command_sp);
  return lldb::SBCommand();
}

uint32_t SBCommand::GetFlags() {
  LLDB_INSTRUMENT_VA(this);

  return (IsValid() ? m_opaque_sp->GetFlags().Get() : 0);
}

void SBCommand::SetFlags(uint32_t flags) {
  LLDB_INSTRUMENT_VA(this, flags);

  if (IsValid())
    m_opaque_sp->GetFlags().Set(flags);
}
