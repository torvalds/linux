//===-- CommandObjectHelp.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectHelp.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"

using namespace lldb;
using namespace lldb_private;

// CommandObjectHelp

void CommandObjectHelp::GenerateAdditionalHelpAvenuesMessage(
    Stream *s, llvm::StringRef command, llvm::StringRef prefix,
    llvm::StringRef subcommand, bool include_upropos,
    bool include_type_lookup) {
  if (!s || command.empty())
    return;

  std::string command_str = command.str();
  std::string prefix_str = prefix.str();
  std::string subcommand_str = subcommand.str();
  const std::string &lookup_str =
      !subcommand_str.empty() ? subcommand_str : command_str;
  s->Printf("'%s' is not a known command.\n", command_str.c_str());
  s->Printf("Try '%shelp' to see a current list of commands.\n",
            prefix.str().c_str());
  if (include_upropos) {
    s->Printf("Try '%sapropos %s' for a list of related commands.\n",
              prefix_str.c_str(), lookup_str.c_str());
  }
  if (include_type_lookup) {
    s->Printf("Try '%stype lookup %s' for information on types, methods, "
              "functions, modules, etc.",
              prefix_str.c_str(), lookup_str.c_str());
  }
}

CommandObjectHelp::CommandObjectHelp(CommandInterpreter &interpreter)
    : CommandObjectParsed(interpreter, "help",
                          "Show a list of all debugger "
                          "commands, or give details "
                          "about a specific command.",
                          "help [<cmd-name>]") {
  // A list of command names forming a path to the command we want help on.
  // No names is allowed - in which case we dump the top-level help.
  AddSimpleArgumentList(eArgTypeCommand, eArgRepeatStar);
}

CommandObjectHelp::~CommandObjectHelp() = default;

#define LLDB_OPTIONS_help
#include "CommandOptions.inc"

llvm::ArrayRef<OptionDefinition>
CommandObjectHelp::CommandOptions::GetDefinitions() {
  return llvm::ArrayRef(g_help_options);
}

void CommandObjectHelp::DoExecute(Args &command, CommandReturnObject &result) {
  CommandObject::CommandMap::iterator pos;
  CommandObject *cmd_obj;
  const size_t argc = command.GetArgumentCount();

  // 'help' doesn't take any arguments, other than command names.  If argc is
  // 0, we show the user all commands (aliases and user commands if asked for).
  // Otherwise every argument must be the name of a command or a sub-command.
  if (argc == 0) {
    uint32_t cmd_types = CommandInterpreter::eCommandTypesBuiltin;
    if (m_options.m_show_aliases)
      cmd_types |= CommandInterpreter::eCommandTypesAliases;
    if (m_options.m_show_user_defined) {
      cmd_types |= CommandInterpreter::eCommandTypesUserDef;
      cmd_types |= CommandInterpreter::eCommandTypesUserMW;
    }
    if (m_options.m_show_hidden)
      cmd_types |= CommandInterpreter::eCommandTypesHidden;

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    m_interpreter.GetHelp(result, cmd_types); // General help
  } else {
    // Get command object for the first command argument. Only search built-in
    // command dictionary.
    StringList matches;
    auto command_name = command[0].ref();
    cmd_obj = m_interpreter.GetCommandObject(command_name, &matches);

    if (cmd_obj != nullptr) {
      StringList matches;
      bool all_okay = true;
      CommandObject *sub_cmd_obj = cmd_obj;
      // Loop down through sub_command dictionaries until we find the command
      // object that corresponds to the help command entered.
      std::string sub_command;
      for (auto &entry : command.entries().drop_front()) {
        sub_command = std::string(entry.ref());
        matches.Clear();
        if (sub_cmd_obj->IsAlias())
          sub_cmd_obj =
              ((CommandAlias *)sub_cmd_obj)->GetUnderlyingCommand().get();
        if (!sub_cmd_obj->IsMultiwordObject()) {
          all_okay = false;
          break;
        } else {
          CommandObject *found_cmd;
          found_cmd =
              sub_cmd_obj->GetSubcommandObject(sub_command.c_str(), &matches);
          if (found_cmd == nullptr || matches.GetSize() > 1) {
            all_okay = false;
            break;
          } else
            sub_cmd_obj = found_cmd;
        }
      }

      if (!all_okay || (sub_cmd_obj == nullptr)) {
        std::string cmd_string;
        command.GetCommandString(cmd_string);
        if (matches.GetSize() >= 2) {
          StreamString s;
          s.Printf("ambiguous command %s", cmd_string.c_str());
          size_t num_matches = matches.GetSize();
          for (size_t match_idx = 0; match_idx < num_matches; match_idx++) {
            s.Printf("\n\t%s", matches.GetStringAtIndex(match_idx));
          }
          s.Printf("\n");
          result.AppendError(s.GetString());
          return;
        } else if (!sub_cmd_obj) {
          StreamString error_msg_stream;
          GenerateAdditionalHelpAvenuesMessage(
              &error_msg_stream, cmd_string.c_str(),
              m_interpreter.GetCommandPrefix(), sub_command.c_str());
          result.AppendError(error_msg_stream.GetString());
          return;
        } else {
          GenerateAdditionalHelpAvenuesMessage(
              &result.GetOutputStream(), cmd_string.c_str(),
              m_interpreter.GetCommandPrefix(), sub_command.c_str());
          result.GetOutputStream().Printf(
              "\nThe closest match is '%s'. Help on it follows.\n\n",
              sub_cmd_obj->GetCommandName().str().c_str());
        }
      }

      sub_cmd_obj->GenerateHelpText(result);
      std::string alias_full_name;
      // Don't use AliasExists here, that only checks exact name matches.  If
      // the user typed a shorter unique alias name, we should still tell them
      // it was an alias.
      if (m_interpreter.GetAliasFullName(command_name, alias_full_name)) {
        StreamString sstr;
        m_interpreter.GetAlias(alias_full_name)->GetAliasExpansion(sstr);
        result.GetOutputStream().Printf("\n'%s' is an abbreviation for %s\n",
                                        command[0].c_str(), sstr.GetData());
      }
    } else if (matches.GetSize() > 0) {
      Stream &output_strm = result.GetOutputStream();
      output_strm.Printf("Help requested with ambiguous command name, possible "
                         "completions:\n");
      const size_t match_count = matches.GetSize();
      for (size_t i = 0; i < match_count; i++) {
        output_strm.Printf("\t%s\n", matches.GetStringAtIndex(i));
      }
    } else {
      // Maybe the user is asking for help about a command argument rather than
      // a command.
      const CommandArgumentType arg_type =
          CommandObject::LookupArgumentName(command_name);
      if (arg_type != eArgTypeLastArg) {
        Stream &output_strm = result.GetOutputStream();
        CommandObject::GetArgumentHelp(output_strm, arg_type, m_interpreter);
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
      } else {
        StreamString error_msg_stream;
        GenerateAdditionalHelpAvenuesMessage(&error_msg_stream, command_name,
                                             m_interpreter.GetCommandPrefix(),
                                             "");
        result.AppendError(error_msg_stream.GetString());
      }
    }
  }
}

void CommandObjectHelp::HandleCompletion(CompletionRequest &request) {
  // Return the completions of the commands in the help system:
  if (request.GetCursorIndex() == 0) {
    m_interpreter.HandleCompletionMatches(request);
    return;
  }
  CommandObject *cmd_obj =
      m_interpreter.GetCommandObject(request.GetParsedLine()[0].ref());

  // The command that they are getting help on might be ambiguous, in which
  // case we should complete that, otherwise complete with the command the
  // user is getting help on...

  if (cmd_obj) {
    request.ShiftArguments();
    cmd_obj->HandleCompletion(request);
    return;
  }
  m_interpreter.HandleCompletionMatches(request);
}
