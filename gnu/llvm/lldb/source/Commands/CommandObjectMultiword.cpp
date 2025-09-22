//===-- CommandObjectMultiword.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/Options.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

// CommandObjectMultiword

CommandObjectMultiword::CommandObjectMultiword(CommandInterpreter &interpreter,
                                               const char *name,
                                               const char *help,
                                               const char *syntax,
                                               uint32_t flags)
    : CommandObject(interpreter, name, help, syntax, flags),
      m_can_be_removed(false) {}

CommandObjectMultiword::~CommandObjectMultiword() = default;

CommandObjectSP
CommandObjectMultiword::GetSubcommandSPExact(llvm::StringRef sub_cmd) {
  if (m_subcommand_dict.empty())
    return {};

  auto pos = m_subcommand_dict.find(std::string(sub_cmd));
  if (pos == m_subcommand_dict.end())
    return {};

  return pos->second;
}

CommandObjectSP CommandObjectMultiword::GetSubcommandSP(llvm::StringRef sub_cmd,
                                                        StringList *matches) {
  if (m_subcommand_dict.empty())
    return {};

  CommandObjectSP return_cmd_sp = GetSubcommandSPExact(sub_cmd);
  if (return_cmd_sp) {
    if (matches)
      matches->AppendString(sub_cmd);
    return return_cmd_sp;
  }

  CommandObject::CommandMap::iterator pos;

  StringList local_matches;
  if (matches == nullptr)
    matches = &local_matches;
  int num_matches =
      AddNamesMatchingPartialString(m_subcommand_dict, sub_cmd, *matches);

  if (num_matches == 1) {
    // Cleaner, but slightly less efficient would be to call back into this
    // function, since I now know I have an exact match...

    sub_cmd = matches->GetStringAtIndex(0);
    pos = m_subcommand_dict.find(std::string(sub_cmd));
    if (pos != m_subcommand_dict.end())
      return_cmd_sp = pos->second;
  }

  return return_cmd_sp;
}

CommandObject *
CommandObjectMultiword::GetSubcommandObject(llvm::StringRef sub_cmd,
                                            StringList *matches) {
  return GetSubcommandSP(sub_cmd, matches).get();
}

bool CommandObjectMultiword::LoadSubCommand(llvm::StringRef name,
                                            const CommandObjectSP &cmd_obj_sp) {
  if (cmd_obj_sp)
    lldbassert((&GetCommandInterpreter() == &cmd_obj_sp->GetCommandInterpreter()) &&
           "tried to add a CommandObject from a different interpreter");

  CommandMap::iterator pos;
  bool success = true;

  pos = m_subcommand_dict.find(std::string(name));
  if (pos == m_subcommand_dict.end()) {
    m_subcommand_dict[std::string(name)] = cmd_obj_sp;
  } else
    success = false;

  return success;
}

llvm::Error CommandObjectMultiword::LoadUserSubcommand(
    llvm::StringRef name, const CommandObjectSP &cmd_obj_sp, bool can_replace) {
  Status result;
  if (cmd_obj_sp)
    lldbassert((&GetCommandInterpreter() == &cmd_obj_sp->GetCommandInterpreter()) &&
           "tried to add a CommandObject from a different interpreter");
  if (!IsUserCommand()) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                              "can't add a user subcommand to a builtin container command.");
  }
  // Make sure this a user command if it isn't already:
  cmd_obj_sp->SetIsUserCommand(true);

  std::string str_name(name);

  auto pos = m_subcommand_dict.find(str_name);
  if (pos == m_subcommand_dict.end()) {
    m_subcommand_dict[str_name] = cmd_obj_sp;
    return llvm::Error::success();
  }

  const char *error_str = nullptr;
  if (!can_replace)
    error_str = "sub-command already exists";
  if (!(*pos).second->IsUserCommand())
    error_str = "can't replace a builtin subcommand";

  if (error_str) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(), error_str);
  }
  m_subcommand_dict[str_name] = cmd_obj_sp;
  return llvm::Error::success();
}

llvm::Error CommandObjectMultiword::RemoveUserSubcommand(llvm::StringRef cmd_name,
                                                    bool must_be_multiword) {
  CommandMap::iterator pos;
  std::string str_name(cmd_name);

  pos = m_subcommand_dict.find(str_name);
  if (pos == m_subcommand_dict.end()) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),"subcommand '%s' not found.",
                                   str_name.c_str());
  }
  if (!(*pos).second->IsUserCommand()) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),"subcommand '%s' not a user command.",
                                   str_name.c_str());
  }

  if (must_be_multiword && !(*pos).second->IsMultiwordObject()) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),"subcommand '%s' is not a container command",
                                   str_name.c_str());
  }
  if (!must_be_multiword && (*pos).second->IsMultiwordObject()) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),"subcommand '%s' is not a user command",
                                   str_name.c_str());
  }

  m_subcommand_dict.erase(pos);

  return llvm::Error::success();
}

void CommandObjectMultiword::Execute(const char *args_string,
                                     CommandReturnObject &result) {
  Args args(args_string);
  const size_t argc = args.GetArgumentCount();
  if (argc == 0) {
    this->CommandObject::GenerateHelpText(result);
    return;
  }

  auto sub_command = args[0].ref();
  if (sub_command.empty()) {
    result.AppendError("Need to specify a non-empty subcommand.");
    return;
  }

  if (m_subcommand_dict.empty()) {
    result.AppendErrorWithFormat("'%s' does not have any subcommands.\n",
                                 GetCommandName().str().c_str());
    return;
  }

  StringList matches;
  CommandObject *sub_cmd_obj = GetSubcommandObject(sub_command, &matches);
  if (sub_cmd_obj != nullptr) {
    // Now call CommandObject::Execute to process options in `rest_of_line`.
    // From there the command-specific version of Execute will be called, with
    // the processed arguments.

    args.Shift();
    sub_cmd_obj->Execute(args_string, result);
    return;
  }

  std::string error_msg;
  const size_t num_subcmd_matches = matches.GetSize();
  if (num_subcmd_matches > 0)
    error_msg.assign("ambiguous command ");
  else
    error_msg.assign("invalid command ");

  error_msg.append("'");
  error_msg.append(std::string(GetCommandName()));
  error_msg.append(" ");
  error_msg.append(std::string(sub_command));
  error_msg.append("'.");

  if (num_subcmd_matches > 0) {
    error_msg.append(" Possible completions:");
    for (const std::string &match : matches) {
      error_msg.append("\n\t");
      error_msg.append(match);
    }
  }
  error_msg.append("\n");
  result.AppendRawError(error_msg.c_str());
}

void CommandObjectMultiword::GenerateHelpText(Stream &output_stream) {
  // First time through here, generate the help text for the object and push it
  // to the return result object as well

  CommandObject::GenerateHelpText(output_stream);
  output_stream.PutCString("\nThe following subcommands are supported:\n\n");

  CommandMap::iterator pos;
  uint32_t max_len = FindLongestCommandWord(m_subcommand_dict);

  if (max_len)
    max_len += 4; // Indent the output by 4 spaces.

  for (pos = m_subcommand_dict.begin(); pos != m_subcommand_dict.end(); ++pos) {
    std::string indented_command("    ");
    indented_command.append(pos->first);
    if (pos->second->WantsRawCommandString()) {
      std::string help_text(std::string(pos->second->GetHelp()));
      help_text.append("  Expects 'raw' input (see 'help raw-input'.)");
      m_interpreter.OutputFormattedHelpText(output_stream, indented_command,
                                            "--", help_text, max_len);
    } else
      m_interpreter.OutputFormattedHelpText(output_stream, indented_command,
                                            "--", pos->second->GetHelp(),
                                            max_len);
  }

  output_stream.PutCString("\nFor more help on any particular subcommand, type "
                           "'help <command> <subcommand>'.\n");
}

void CommandObjectMultiword::HandleCompletion(CompletionRequest &request) {
  auto arg0 = request.GetParsedLine()[0].ref();
  if (request.GetCursorIndex() == 0) {
    StringList new_matches, descriptions;
    AddNamesMatchingPartialString(m_subcommand_dict, arg0, new_matches,
                                  &descriptions);
    request.AddCompletions(new_matches, descriptions);

    if (new_matches.GetSize() == 1 &&
        new_matches.GetStringAtIndex(0) != nullptr &&
        (arg0 == new_matches.GetStringAtIndex(0))) {
      StringList temp_matches;
      CommandObject *cmd_obj = GetSubcommandObject(arg0, &temp_matches);
      if (cmd_obj != nullptr) {
        if (request.GetParsedLine().GetArgumentCount() != 1) {
          request.GetParsedLine().Shift();
          request.AppendEmptyArgument();
          cmd_obj->HandleCompletion(request);
        }
      }
    }
    return;
  }

  StringList new_matches;
  CommandObject *sub_command_object = GetSubcommandObject(arg0, &new_matches);

  // The subcommand is ambiguous. The completion isn't meaningful.
  if (!sub_command_object)
    return;

  // Remove the one match that we got from calling GetSubcommandObject.
  new_matches.DeleteStringAtIndex(0);
  request.AddCompletions(new_matches);
  request.ShiftArguments();
  sub_command_object->HandleCompletion(request);
}

std::optional<std::string>
CommandObjectMultiword::GetRepeatCommand(Args &current_command_args,
                                         uint32_t index) {
  index++;
  if (current_command_args.GetArgumentCount() <= index)
    return std::nullopt;
  CommandObject *sub_command_object =
      GetSubcommandObject(current_command_args[index].ref());
  if (sub_command_object == nullptr)
    return std::nullopt;
  return sub_command_object->GetRepeatCommand(current_command_args, index);
}

CommandObjectProxy::CommandObjectProxy(CommandInterpreter &interpreter,
                                       const char *name, const char *help,
                                       const char *syntax, uint32_t flags)
    : CommandObject(interpreter, name, help, syntax, flags) {}

CommandObjectProxy::~CommandObjectProxy() = default;

Options *CommandObjectProxy::GetOptions() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetOptions();
  return CommandObject::GetOptions();
}

llvm::StringRef CommandObjectProxy::GetHelp() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetHelp();
  return CommandObject::GetHelp();
}

llvm::StringRef CommandObjectProxy::GetSyntax() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetSyntax();
  return CommandObject::GetSyntax();
}

llvm::StringRef CommandObjectProxy::GetHelpLong() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetHelpLong();
  return CommandObject::GetHelpLong();
}

bool CommandObjectProxy::IsRemovable() const {
  const CommandObject *proxy_command =
      const_cast<CommandObjectProxy *>(this)->GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->IsRemovable();
  return false;
}

bool CommandObjectProxy::IsMultiwordObject() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->IsMultiwordObject();
  return false;
}

CommandObjectMultiword *CommandObjectProxy::GetAsMultiwordCommand() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetAsMultiwordCommand();
  return nullptr;
}

void CommandObjectProxy::GenerateHelpText(Stream &result) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    proxy_command->GenerateHelpText(result);
  else
    CommandObject::GenerateHelpText(result);
}

lldb::CommandObjectSP
CommandObjectProxy::GetSubcommandSP(llvm::StringRef sub_cmd,
                                    StringList *matches) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetSubcommandSP(sub_cmd, matches);
  return lldb::CommandObjectSP();
}

CommandObject *CommandObjectProxy::GetSubcommandObject(llvm::StringRef sub_cmd,
                                                       StringList *matches) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetSubcommandObject(sub_cmd, matches);
  return nullptr;
}

bool CommandObjectProxy::LoadSubCommand(
    llvm::StringRef cmd_name, const lldb::CommandObjectSP &command_sp) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->LoadSubCommand(cmd_name, command_sp);
  return false;
}

bool CommandObjectProxy::WantsRawCommandString() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->WantsRawCommandString();
  return false;
}

bool CommandObjectProxy::WantsCompletion() {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->WantsCompletion();
  return false;
}

void CommandObjectProxy::HandleCompletion(CompletionRequest &request) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    proxy_command->HandleCompletion(request);
}

void CommandObjectProxy::HandleArgumentCompletion(
    CompletionRequest &request, OptionElementVector &opt_element_vector) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    proxy_command->HandleArgumentCompletion(request, opt_element_vector);
}

std::optional<std::string>
CommandObjectProxy::GetRepeatCommand(Args &current_command_args,
                                     uint32_t index) {
  CommandObject *proxy_command = GetProxyCommandObject();
  if (proxy_command)
    return proxy_command->GetRepeatCommand(current_command_args, index);
  return std::nullopt;
}

llvm::StringRef CommandObjectProxy::GetUnsupportedError() {
  return "command is not implemented";
}

void CommandObjectProxy::Execute(const char *args_string,
                                 CommandReturnObject &result) {
  if (CommandObject *proxy_command = GetProxyCommandObject())
    proxy_command->Execute(args_string, result);
  else
    result.AppendError(GetUnsupportedError());
}
