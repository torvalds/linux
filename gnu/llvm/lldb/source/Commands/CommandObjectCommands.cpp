//===-- CommandObjectCommands.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectCommands.h"
#include "CommandObjectHelp.h"
#include "CommandObjectRegexCommand.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/Interpreter/CommandHistory.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Interpreter/OptionValueBoolean.h"
#include "lldb/Interpreter/OptionValueString.h"
#include "lldb/Interpreter/OptionValueUInt64.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/StringList.h"
#include "llvm/ADT/StringRef.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

// CommandObjectCommandsSource

#define LLDB_OPTIONS_source
#include "CommandOptions.inc"

class CommandObjectCommandsSource : public CommandObjectParsed {
public:
  CommandObjectCommandsSource(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "command source",
            "Read and execute LLDB commands from the file <filename>.",
            nullptr) {
    AddSimpleArgumentList(eArgTypeFilename);
  }

  ~CommandObjectCommandsSource() override = default;

  std::optional<std::string> GetRepeatCommand(Args &current_command_args,
                                              uint32_t index) override {
    return std::string("");
  }

  Options *GetOptions() override { return &m_options; }

protected:
  class CommandOptions : public Options {
  public:
    CommandOptions()
        : m_stop_on_error(true), m_silent_run(false), m_stop_on_continue(true),
          m_cmd_relative_to_command_file(false) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'e':
        error = m_stop_on_error.SetValueFromString(option_arg);
        break;

      case 'c':
        error = m_stop_on_continue.SetValueFromString(option_arg);
        break;

      case 'C':
        m_cmd_relative_to_command_file = true;
        break;

      case 's':
        error = m_silent_run.SetValueFromString(option_arg);
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_stop_on_error.Clear();
      m_silent_run.Clear();
      m_stop_on_continue.Clear();
      m_cmd_relative_to_command_file.Clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_source_options);
    }

    // Instance variables to hold the values for command options.

    OptionValueBoolean m_stop_on_error;
    OptionValueBoolean m_silent_run;
    OptionValueBoolean m_stop_on_continue;
    OptionValueBoolean m_cmd_relative_to_command_file;
  };

  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (command.GetArgumentCount() != 1) {
      result.AppendErrorWithFormat(
          "'%s' takes exactly one executable filename argument.\n",
          GetCommandName().str().c_str());
      return;
    }

    FileSpec source_dir = {};
    if (m_options.m_cmd_relative_to_command_file) {
      source_dir = GetDebugger().GetCommandInterpreter().GetCurrentSourceDir();
      if (!source_dir) {
        result.AppendError("command source -C can only be specified "
                           "from a command file");
        result.SetStatus(eReturnStatusFailed);
        return;
      }
    }

    FileSpec cmd_file(command[0].ref());
    if (source_dir) {
      // Prepend the source_dir to the cmd_file path:
      if (!cmd_file.IsRelative()) {
        result.AppendError("command source -C can only be used "
                           "with a relative path.");
        result.SetStatus(eReturnStatusFailed);
        return;
      }
      cmd_file.MakeAbsolute(source_dir);
    }

    FileSystem::Instance().Resolve(cmd_file);

    CommandInterpreterRunOptions options;
    // If any options were set, then use them
    if (m_options.m_stop_on_error.OptionWasSet() ||
        m_options.m_silent_run.OptionWasSet() ||
        m_options.m_stop_on_continue.OptionWasSet()) {
      if (m_options.m_stop_on_continue.OptionWasSet())
        options.SetStopOnContinue(
            m_options.m_stop_on_continue.GetCurrentValue());

      if (m_options.m_stop_on_error.OptionWasSet())
        options.SetStopOnError(m_options.m_stop_on_error.GetCurrentValue());

      // Individual silent setting is override for global command echo settings.
      if (m_options.m_silent_run.GetCurrentValue()) {
        options.SetSilent(true);
      } else {
        options.SetPrintResults(true);
        options.SetPrintErrors(true);
        options.SetEchoCommands(m_interpreter.GetEchoCommands());
        options.SetEchoCommentCommands(m_interpreter.GetEchoCommentCommands());
      }
    }

    m_interpreter.HandleCommandsFromFile(cmd_file, options, result);
  }

  CommandOptions m_options;
};

#pragma mark CommandObjectCommandsAlias
// CommandObjectCommandsAlias

#define LLDB_OPTIONS_alias
#include "CommandOptions.inc"

static const char *g_python_command_instructions =
    "Enter your Python command(s). Type 'DONE' to end.\n"
    "You must define a Python function with this signature:\n"
    "def my_command_impl(debugger, args, exe_ctx, result, internal_dict):\n";

class CommandObjectCommandsAlias : public CommandObjectRaw {
protected:
  class CommandOptions : public OptionGroup {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_alias_options);
    }

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                          ExecutionContext *execution_context) override {
      Status error;

      const int short_option = GetDefinitions()[option_idx].short_option;
      std::string option_str(option_value);

      switch (short_option) {
      case 'h':
        m_help.SetCurrentValue(option_str);
        m_help.SetOptionWasSet();
        break;

      case 'H':
        m_long_help.SetCurrentValue(option_str);
        m_long_help.SetOptionWasSet();
        break;

      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_help.Clear();
      m_long_help.Clear();
    }

    OptionValueString m_help;
    OptionValueString m_long_help;
  };

  OptionGroupOptions m_option_group;
  CommandOptions m_command_options;

public:
  Options *GetOptions() override { return &m_option_group; }

  CommandObjectCommandsAlias(CommandInterpreter &interpreter)
      : CommandObjectRaw(
            interpreter, "command alias",
            "Define a custom command in terms of an existing command.") {
    m_option_group.Append(&m_command_options);
    m_option_group.Finalize();

    SetHelpLong(
        "'alias' allows the user to create a short-cut or abbreviation for long \
commands, multi-word commands, and commands that take particular options.  \
Below are some simple examples of how one might use the 'alias' command:"
        R"(

(lldb) command alias sc script

    Creates the abbreviation 'sc' for the 'script' command.

(lldb) command alias bp breakpoint

)"
        "    Creates the abbreviation 'bp' for the 'breakpoint' command.  Since \
breakpoint commands are two-word commands, the user would still need to \
enter the second word after 'bp', e.g. 'bp enable' or 'bp delete'."
        R"(

(lldb) command alias bpl breakpoint list

    Creates the abbreviation 'bpl' for the two-word command 'breakpoint list'.

)"
        "An alias can include some options for the command, with the values either \
filled in at the time the alias is created, or specified as positional \
arguments, to be filled in when the alias is invoked.  The following example \
shows how to create aliases with options:"
        R"(

(lldb) command alias bfl breakpoint set -f %1 -l %2

)"
        "    Creates the abbreviation 'bfl' (for break-file-line), with the -f and -l \
options already part of the alias.  So if the user wants to set a breakpoint \
by file and line without explicitly having to use the -f and -l options, the \
user can now use 'bfl' instead.  The '%1' and '%2' are positional placeholders \
for the actual arguments that will be passed when the alias command is used.  \
The number in the placeholder refers to the position/order the actual value \
occupies when the alias is used.  All the occurrences of '%1' in the alias \
will be replaced with the first argument, all the occurrences of '%2' in the \
alias will be replaced with the second argument, and so on.  This also allows \
actual arguments to be used multiple times within an alias (see 'process \
launch' example below)."
        R"(

)"
        "Note: the positional arguments must substitute as whole words in the resultant \
command, so you can't at present do something like this to append the file extension \
\".cpp\":"
        R"(

(lldb) command alias bcppfl breakpoint set -f %1.cpp -l %2

)"
        "For more complex aliasing, use the \"command regex\" command instead.  In the \
'bfl' case above, the actual file value will be filled in with the first argument \
following 'bfl' and the actual line number value will be filled in with the second \
argument.  The user would use this alias as follows:"
        R"(

(lldb) command alias bfl breakpoint set -f %1 -l %2
(lldb) bfl my-file.c 137

This would be the same as if the user had entered 'breakpoint set -f my-file.c -l 137'.

Another example:

(lldb) command alias pltty process launch -s -o %1 -e %1
(lldb) pltty /dev/tty0

    Interpreted as 'process launch -s -o /dev/tty0 -e /dev/tty0'

)"
        "If the user always wanted to pass the same value to a particular option, the \
alias could be defined with that value directly in the alias as a constant, \
rather than using a positional placeholder:"
        R"(

(lldb) command alias bl3 breakpoint set -f %1 -l 3

    Always sets a breakpoint on line 3 of whatever file is indicated.)");

    CommandArgumentEntry arg1;
    CommandArgumentEntry arg2;
    CommandArgumentEntry arg3;
    CommandArgumentData alias_arg;
    CommandArgumentData cmd_arg;
    CommandArgumentData options_arg;

    // Define the first (and only) variant of this arg.
    alias_arg.arg_type = eArgTypeAliasName;
    alias_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg1.push_back(alias_arg);

    // Define the first (and only) variant of this arg.
    cmd_arg.arg_type = eArgTypeCommandName;
    cmd_arg.arg_repetition = eArgRepeatPlain;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg2.push_back(cmd_arg);

    // Define the first (and only) variant of this arg.
    options_arg.arg_type = eArgTypeAliasOptions;
    options_arg.arg_repetition = eArgRepeatOptional;

    // There is only one variant this argument could be; put it into the
    // argument entry.
    arg3.push_back(options_arg);

    // Push the data for the first argument into the m_arguments vector.
    m_arguments.push_back(arg1);
    m_arguments.push_back(arg2);
    m_arguments.push_back(arg3);
  }

  ~CommandObjectCommandsAlias() override = default;

protected:
  void DoExecute(llvm::StringRef raw_command_line,
                 CommandReturnObject &result) override {
    if (raw_command_line.empty()) {
      result.AppendError("'command alias' requires at least two arguments");
      return;
    }

    ExecutionContext exe_ctx = GetCommandInterpreter().GetExecutionContext();
    m_option_group.NotifyOptionParsingStarting(&exe_ctx);

    OptionsWithRaw args_with_suffix(raw_command_line);

    if (args_with_suffix.HasArgs())
      if (!ParseOptionsAndNotify(args_with_suffix.GetArgs(), result,
                                 m_option_group, exe_ctx))
        return;

    llvm::StringRef raw_command_string = args_with_suffix.GetRawPart();
    Args args(raw_command_string);

    if (args.GetArgumentCount() < 2) {
      result.AppendError("'command alias' requires at least two arguments");
      return;
    }

    // Get the alias command.

    auto alias_command = args[0].ref();
    if (alias_command.starts_with("-")) {
      result.AppendError("aliases starting with a dash are not supported");
      if (alias_command == "--help" || alias_command == "--long-help") {
        result.AppendWarning("if trying to pass options to 'command alias' add "
                             "a -- at the end of the options");
      }
      return;
    }

    // Strip the new alias name off 'raw_command_string'  (leave it on args,
    // which gets passed to 'Execute', which does the stripping itself.
    size_t pos = raw_command_string.find(alias_command);
    if (pos == 0) {
      raw_command_string = raw_command_string.substr(alias_command.size());
      pos = raw_command_string.find_first_not_of(' ');
      if ((pos != std::string::npos) && (pos > 0))
        raw_command_string = raw_command_string.substr(pos);
    } else {
      result.AppendError("Error parsing command string.  No alias created.");
      return;
    }

    // Verify that the command is alias-able.
    if (m_interpreter.CommandExists(alias_command)) {
      result.AppendErrorWithFormat(
          "'%s' is a permanent debugger command and cannot be redefined.\n",
          args[0].c_str());
      return;
    }

    if (m_interpreter.UserMultiwordCommandExists(alias_command)) {
      result.AppendErrorWithFormat(
          "'%s' is a user container command and cannot be overwritten.\n"
          "Delete it first with 'command container delete'\n",
          args[0].c_str());
      return;
    }

    // Get CommandObject that is being aliased. The command name is read from
    // the front of raw_command_string. raw_command_string is returned with the
    // name of the command object stripped off the front.
    llvm::StringRef original_raw_command_string = raw_command_string;
    CommandObject *cmd_obj =
        m_interpreter.GetCommandObjectForCommand(raw_command_string);

    if (!cmd_obj) {
      result.AppendErrorWithFormat("invalid command given to 'command alias'. "
                                   "'%s' does not begin with a valid command."
                                   "  No alias created.",
                                   original_raw_command_string.str().c_str());
    } else if (!cmd_obj->WantsRawCommandString()) {
      // Note that args was initialized with the original command, and has not
      // been updated to this point. Therefore can we pass it to the version of
      // Execute that does not need/expect raw input in the alias.
      HandleAliasingNormalCommand(args, result);
    } else {
      HandleAliasingRawCommand(alias_command, raw_command_string, *cmd_obj,
                               result);
    }
  }

  bool HandleAliasingRawCommand(llvm::StringRef alias_command,
                                llvm::StringRef raw_command_string,
                                CommandObject &cmd_obj,
                                CommandReturnObject &result) {
    // Verify & handle any options/arguments passed to the alias command

    OptionArgVectorSP option_arg_vector_sp =
        OptionArgVectorSP(new OptionArgVector);

    const bool include_aliases = true;
    // Look up the command using command's name first.  This is to resolve
    // aliases when you are making nested aliases.  But if you don't find
    // it that way, then it wasn't an alias and we can just use the object
    // we were passed in.
    CommandObjectSP cmd_obj_sp = m_interpreter.GetCommandSPExact(
            cmd_obj.GetCommandName(), include_aliases);
    if (!cmd_obj_sp)
      cmd_obj_sp = cmd_obj.shared_from_this();

    if (m_interpreter.AliasExists(alias_command) ||
        m_interpreter.UserCommandExists(alias_command)) {
      result.AppendWarningWithFormat(
          "Overwriting existing definition for '%s'.\n",
          alias_command.str().c_str());
    }
    if (CommandAlias *alias = m_interpreter.AddAlias(
            alias_command, cmd_obj_sp, raw_command_string)) {
      if (m_command_options.m_help.OptionWasSet())
        alias->SetHelp(m_command_options.m_help.GetCurrentValue());
      if (m_command_options.m_long_help.OptionWasSet())
        alias->SetHelpLong(m_command_options.m_long_help.GetCurrentValue());
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendError("Unable to create requested alias.\n");
    }
    return result.Succeeded();
  }

  bool HandleAliasingNormalCommand(Args &args, CommandReturnObject &result) {
    size_t argc = args.GetArgumentCount();

    if (argc < 2) {
      result.AppendError("'command alias' requires at least two arguments");
      return false;
    }

    // Save these in std::strings since we're going to shift them off.
    const std::string alias_command(std::string(args[0].ref()));
    const std::string actual_command(std::string(args[1].ref()));

    args.Shift(); // Shift the alias command word off the argument vector.
    args.Shift(); // Shift the old command word off the argument vector.

    // Verify that the command is alias'able, and get the appropriate command
    // object.

    if (m_interpreter.CommandExists(alias_command)) {
      result.AppendErrorWithFormat(
          "'%s' is a permanent debugger command and cannot be redefined.\n",
          alias_command.c_str());
      return false;
    }

    if (m_interpreter.UserMultiwordCommandExists(alias_command)) {
      result.AppendErrorWithFormat(
          "'%s' is user container command and cannot be overwritten.\n"
          "Delete it first with 'command container delete'",
          alias_command.c_str());
      return false;
    }

    CommandObjectSP command_obj_sp(
        m_interpreter.GetCommandSPExact(actual_command, true));
    CommandObjectSP subcommand_obj_sp;
    bool use_subcommand = false;
    if (!command_obj_sp) {
      result.AppendErrorWithFormat("'%s' is not an existing command.\n",
                                   actual_command.c_str());
      return false;
    }
    CommandObject *cmd_obj = command_obj_sp.get();
    CommandObject *sub_cmd_obj = nullptr;
    OptionArgVectorSP option_arg_vector_sp =
        OptionArgVectorSP(new OptionArgVector);

    while (cmd_obj->IsMultiwordObject() && !args.empty()) {
      auto sub_command = args[0].ref();
      assert(!sub_command.empty());
      subcommand_obj_sp = cmd_obj->GetSubcommandSP(sub_command);
      if (!subcommand_obj_sp) {
        result.AppendErrorWithFormat(
            "'%s' is not a valid sub-command of '%s'.  "
            "Unable to create alias.\n",
            args[0].c_str(), actual_command.c_str());
        return false;
      }

      sub_cmd_obj = subcommand_obj_sp.get();
      use_subcommand = true;
      args.Shift(); // Shift the sub_command word off the argument vector.
      cmd_obj = sub_cmd_obj;
    }

    // Verify & handle any options/arguments passed to the alias command

    std::string args_string;

    if (!args.empty()) {
      CommandObjectSP tmp_sp =
          m_interpreter.GetCommandSPExact(cmd_obj->GetCommandName());
      if (use_subcommand)
        tmp_sp = m_interpreter.GetCommandSPExact(sub_cmd_obj->GetCommandName());

      args.GetCommandString(args_string);
    }

    if (m_interpreter.AliasExists(alias_command) ||
        m_interpreter.UserCommandExists(alias_command)) {
      result.AppendWarningWithFormat(
          "Overwriting existing definition for '%s'.\n", alias_command.c_str());
    }

    if (CommandAlias *alias = m_interpreter.AddAlias(
            alias_command, use_subcommand ? subcommand_obj_sp : command_obj_sp,
            args_string)) {
      if (m_command_options.m_help.OptionWasSet())
        alias->SetHelp(m_command_options.m_help.GetCurrentValue());
      if (m_command_options.m_long_help.OptionWasSet())
        alias->SetHelpLong(m_command_options.m_long_help.GetCurrentValue());
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendError("Unable to create requested alias.\n");
      return false;
    }

    return result.Succeeded();
  }
};

#pragma mark CommandObjectCommandsUnalias
// CommandObjectCommandsUnalias

class CommandObjectCommandsUnalias : public CommandObjectParsed {
public:
  CommandObjectCommandsUnalias(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "command unalias",
            "Delete one or more custom commands defined by 'command alias'.",
            nullptr) {
    AddSimpleArgumentList(eArgTypeAliasName);
  }

  ~CommandObjectCommandsUnalias() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (!m_interpreter.HasCommands() || request.GetCursorIndex() != 0)
      return;

    for (const auto &ent : m_interpreter.GetAliases()) {
      request.TryCompleteCurrentArg(ent.first, ent.second->GetHelp());
    }
  }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    CommandObject::CommandMap::iterator pos;
    CommandObject *cmd_obj;

    if (args.empty()) {
      result.AppendError("must call 'unalias' with a valid alias");
      return;
    }

    auto command_name = args[0].ref();
    cmd_obj = m_interpreter.GetCommandObject(command_name);
    if (!cmd_obj) {
      result.AppendErrorWithFormat(
          "'%s' is not a known command.\nTry 'help' to see a "
          "current list of commands.\n",
          args[0].c_str());
      return;
    }

    if (m_interpreter.CommandExists(command_name)) {
      if (cmd_obj->IsRemovable()) {
        result.AppendErrorWithFormat(
            "'%s' is not an alias, it is a debugger command which can be "
            "removed using the 'command delete' command.\n",
            args[0].c_str());
      } else {
        result.AppendErrorWithFormat(
            "'%s' is a permanent debugger command and cannot be removed.\n",
            args[0].c_str());
      }
      return;
    }

    if (!m_interpreter.RemoveAlias(command_name)) {
      if (m_interpreter.AliasExists(command_name))
        result.AppendErrorWithFormat(
            "Error occurred while attempting to unalias '%s'.\n",
            args[0].c_str());
      else
        result.AppendErrorWithFormat("'%s' is not an existing alias.\n",
                                     args[0].c_str());
      return;
    }

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }
};

#pragma mark CommandObjectCommandsDelete
// CommandObjectCommandsDelete

class CommandObjectCommandsDelete : public CommandObjectParsed {
public:
  CommandObjectCommandsDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "command delete",
            "Delete one or more custom commands defined by 'command regex'.",
            nullptr) {
    AddSimpleArgumentList(eArgTypeCommandName);
  }

  ~CommandObjectCommandsDelete() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    if (!m_interpreter.HasCommands() || request.GetCursorIndex() != 0)
      return;

    for (const auto &ent : m_interpreter.GetCommands()) {
      if (ent.second->IsRemovable())
        request.TryCompleteCurrentArg(ent.first, ent.second->GetHelp());
    }
  }

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override {
    CommandObject::CommandMap::iterator pos;

    if (args.empty()) {
      result.AppendErrorWithFormat("must call '%s' with one or more valid user "
                                   "defined regular expression command names",
                                   GetCommandName().str().c_str());
      return;
    }

    auto command_name = args[0].ref();
    if (!m_interpreter.CommandExists(command_name)) {
      StreamString error_msg_stream;
      const bool generate_upropos = true;
      const bool generate_type_lookup = false;
      CommandObjectHelp::GenerateAdditionalHelpAvenuesMessage(
          &error_msg_stream, command_name, llvm::StringRef(), llvm::StringRef(),
          generate_upropos, generate_type_lookup);
      result.AppendError(error_msg_stream.GetString());
      return;
    }

    if (!m_interpreter.RemoveCommand(command_name)) {
      result.AppendErrorWithFormat(
          "'%s' is a permanent debugger command and cannot be removed.\n",
          args[0].c_str());
      return;
    }

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }
};

// CommandObjectCommandsAddRegex

#define LLDB_OPTIONS_regex
#include "CommandOptions.inc"

#pragma mark CommandObjectCommandsAddRegex

class CommandObjectCommandsAddRegex : public CommandObjectParsed,
                                      public IOHandlerDelegateMultiline {
public:
  CommandObjectCommandsAddRegex(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "command regex",
            "Define a custom command in terms of "
            "existing commands by matching "
            "regular expressions.",
            "command regex <cmd-name> [s/<regex>/<subst>/ ...]"),
        IOHandlerDelegateMultiline("",
                                   IOHandlerDelegate::Completion::LLDBCommand) {
    SetHelpLong(
        R"(
)"
        "This command allows the user to create powerful regular expression commands \
with substitutions. The regular expressions and substitutions are specified \
using the regular expression substitution format of:"
        R"(

    s/<regex>/<subst>/

)"
        "<regex> is a regular expression that can use parenthesis to capture regular \
expression input and substitute the captured matches in the output using %1 \
for the first match, %2 for the second, and so on."
        R"(

)"
        "The regular expressions can all be specified on the command line if more than \
one argument is provided. If just the command name is provided on the command \
line, then the regular expressions and substitutions can be entered on separate \
lines, followed by an empty line to terminate the command definition."
        R"(

EXAMPLES

)"
        "The following example will define a regular expression command named 'f' that \
will call 'finish' if there are no arguments, or 'frame select <frame-idx>' if \
a number follows 'f':"
        R"(

    (lldb) command regex f s/^$/finish/ 's/([0-9]+)/frame select %1/')");
    AddSimpleArgumentList(eArgTypeSEDStylePair, eArgRepeatOptional);
  }

  ~CommandObjectCommandsAddRegex() override = default;

protected:
  void IOHandlerActivated(IOHandler &io_handler, bool interactive) override {
    StreamFileSP output_sp(io_handler.GetOutputStreamFileSP());
    if (output_sp && interactive) {
      output_sp->PutCString("Enter one or more sed substitution commands in "
                            "the form: 's/<regex>/<subst>/'.\nTerminate the "
                            "substitution list with an empty line.\n");
      output_sp->Flush();
    }
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &data) override {
    io_handler.SetIsDone(true);
    if (m_regex_cmd_up) {
      StringList lines;
      if (lines.SplitIntoLines(data)) {
        bool check_only = false;
        for (const std::string &line : lines) {
          Status error = AppendRegexSubstitution(line, check_only);
          if (error.Fail()) {
            if (!GetDebugger().GetCommandInterpreter().GetBatchCommandMode()) {
              StreamSP out_stream = GetDebugger().GetAsyncOutputStream();
              out_stream->Printf("error: %s\n", error.AsCString());
            }
          }
        }
      }
      if (m_regex_cmd_up->HasRegexEntries()) {
        CommandObjectSP cmd_sp(m_regex_cmd_up.release());
        m_interpreter.AddCommand(cmd_sp->GetCommandName(), cmd_sp, true);
      }
    }
  }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    const size_t argc = command.GetArgumentCount();
    if (argc == 0) {
      result.AppendError("usage: 'command regex <command-name> "
                         "[s/<regex1>/<subst1>/ s/<regex2>/<subst2>/ ...]'\n");
      return;
    }

    Status error;
    auto name = command[0].ref();
    m_regex_cmd_up = std::make_unique<CommandObjectRegexCommand>(
        m_interpreter, name, m_options.GetHelp(), m_options.GetSyntax(), 0,
        true);

    if (argc == 1) {
      Debugger &debugger = GetDebugger();
      bool color_prompt = debugger.GetUseColor();
      const bool multiple_lines = true; // Get multiple lines
      IOHandlerSP io_handler_sp(new IOHandlerEditline(
          debugger, IOHandler::Type::Other,
          "lldb-regex",          // Name of input reader for history
          llvm::StringRef("> "), // Prompt
          llvm::StringRef(),     // Continuation prompt
          multiple_lines, color_prompt,
          0, // Don't show line numbers
          *this));

      if (io_handler_sp) {
        debugger.RunIOHandlerAsync(io_handler_sp);
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
      }
    } else {
      for (auto &entry : command.entries().drop_front()) {
        bool check_only = false;
        error = AppendRegexSubstitution(entry.ref(), check_only);
        if (error.Fail())
          break;
      }

      if (error.Success()) {
        AddRegexCommandToInterpreter();
      }
    }
    if (error.Fail()) {
      result.AppendError(error.AsCString());
    }
  }

  Status AppendRegexSubstitution(const llvm::StringRef &regex_sed,
                                 bool check_only) {
    Status error;

    if (!m_regex_cmd_up) {
      error.SetErrorStringWithFormat(
          "invalid regular expression command object for: '%.*s'",
          (int)regex_sed.size(), regex_sed.data());
      return error;
    }

    size_t regex_sed_size = regex_sed.size();

    if (regex_sed_size <= 1) {
      error.SetErrorStringWithFormat(
          "regular expression substitution string is too short: '%.*s'",
          (int)regex_sed.size(), regex_sed.data());
      return error;
    }

    if (regex_sed[0] != 's') {
      error.SetErrorStringWithFormat("regular expression substitution string "
                                     "doesn't start with 's': '%.*s'",
                                     (int)regex_sed.size(), regex_sed.data());
      return error;
    }
    const size_t first_separator_char_pos = 1;
    // use the char that follows 's' as the regex separator character so we can
    // have "s/<regex>/<subst>/" or "s|<regex>|<subst>|"
    const char separator_char = regex_sed[first_separator_char_pos];
    const size_t second_separator_char_pos =
        regex_sed.find(separator_char, first_separator_char_pos + 1);

    if (second_separator_char_pos == std::string::npos) {
      error.SetErrorStringWithFormat(
          "missing second '%c' separator char after '%.*s' in '%.*s'",
          separator_char,
          (int)(regex_sed.size() - first_separator_char_pos - 1),
          regex_sed.data() + (first_separator_char_pos + 1),
          (int)regex_sed.size(), regex_sed.data());
      return error;
    }

    const size_t third_separator_char_pos =
        regex_sed.find(separator_char, second_separator_char_pos + 1);

    if (third_separator_char_pos == std::string::npos) {
      error.SetErrorStringWithFormat(
          "missing third '%c' separator char after '%.*s' in '%.*s'",
          separator_char,
          (int)(regex_sed.size() - second_separator_char_pos - 1),
          regex_sed.data() + (second_separator_char_pos + 1),
          (int)regex_sed.size(), regex_sed.data());
      return error;
    }

    if (third_separator_char_pos != regex_sed_size - 1) {
      // Make sure that everything that follows the last regex separator char
      if (regex_sed.find_first_not_of("\t\n\v\f\r ",
                                      third_separator_char_pos + 1) !=
          std::string::npos) {
        error.SetErrorStringWithFormat(
            "extra data found after the '%.*s' regular expression substitution "
            "string: '%.*s'",
            (int)third_separator_char_pos + 1, regex_sed.data(),
            (int)(regex_sed.size() - third_separator_char_pos - 1),
            regex_sed.data() + (third_separator_char_pos + 1));
        return error;
      }
    } else if (first_separator_char_pos + 1 == second_separator_char_pos) {
      error.SetErrorStringWithFormat(
          "<regex> can't be empty in 's%c<regex>%c<subst>%c' string: '%.*s'",
          separator_char, separator_char, separator_char, (int)regex_sed.size(),
          regex_sed.data());
      return error;
    } else if (second_separator_char_pos + 1 == third_separator_char_pos) {
      error.SetErrorStringWithFormat(
          "<subst> can't be empty in 's%c<regex>%c<subst>%c' string: '%.*s'",
          separator_char, separator_char, separator_char, (int)regex_sed.size(),
          regex_sed.data());
      return error;
    }

    if (!check_only) {
      std::string regex(std::string(regex_sed.substr(
          first_separator_char_pos + 1,
          second_separator_char_pos - first_separator_char_pos - 1)));
      std::string subst(std::string(regex_sed.substr(
          second_separator_char_pos + 1,
          third_separator_char_pos - second_separator_char_pos - 1)));
      m_regex_cmd_up->AddRegexCommand(regex, subst);
    }
    return error;
  }

  void AddRegexCommandToInterpreter() {
    if (m_regex_cmd_up) {
      if (m_regex_cmd_up->HasRegexEntries()) {
        CommandObjectSP cmd_sp(m_regex_cmd_up.release());
        m_interpreter.AddCommand(cmd_sp->GetCommandName(), cmd_sp, true);
      }
    }
  }

private:
  std::unique_ptr<CommandObjectRegexCommand> m_regex_cmd_up;

  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'h':
        m_help.assign(std::string(option_arg));
        break;
      case 's':
        m_syntax.assign(std::string(option_arg));
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_help.clear();
      m_syntax.clear();
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_regex_options);
    }

    llvm::StringRef GetHelp() { return m_help; }

    llvm::StringRef GetSyntax() { return m_syntax; }

  protected:
    // Instance variables to hold the values for command options.

    std::string m_help;
    std::string m_syntax;
  };

  Options *GetOptions() override { return &m_options; }

  CommandOptions m_options;
};

class CommandObjectPythonFunction : public CommandObjectRaw {
public:
  CommandObjectPythonFunction(CommandInterpreter &interpreter, std::string name,
                              std::string funct, std::string help,
                              ScriptedCommandSynchronicity synch,
                              CompletionType completion_type)
      : CommandObjectRaw(interpreter, name), m_function_name(funct),
        m_synchro(synch), m_completion_type(completion_type) {
    if (!help.empty())
      SetHelp(help);
    else {
      StreamString stream;
      stream.Printf("For more information run 'help %s'", name.c_str());
      SetHelp(stream.GetString());
    }
  }

  ~CommandObjectPythonFunction() override = default;

  bool IsRemovable() const override { return true; }

  const std::string &GetFunctionName() { return m_function_name; }

  ScriptedCommandSynchronicity GetSynchronicity() { return m_synchro; }

  llvm::StringRef GetHelpLong() override {
    if (m_fetched_help_long)
      return CommandObjectRaw::GetHelpLong();

    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();
    if (!scripter)
      return CommandObjectRaw::GetHelpLong();

    std::string docstring;
    m_fetched_help_long =
        scripter->GetDocumentationForItem(m_function_name.c_str(), docstring);
    if (!docstring.empty())
      SetHelpLong(docstring);
    return CommandObjectRaw::GetHelpLong();
  }

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), m_completion_type, request, nullptr);
  }

  bool WantsCompletion() override { return true; }

protected:
  void DoExecute(llvm::StringRef raw_command_line,
                 CommandReturnObject &result) override {
    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();

    m_interpreter.IncreaseCommandUsage(*this);

    Status error;

    result.SetStatus(eReturnStatusInvalid);

    if (!scripter || !scripter->RunScriptBasedCommand(
                         m_function_name.c_str(), raw_command_line, m_synchro,
                         result, error, m_exe_ctx)) {
      result.AppendError(error.AsCString());
    } else {
      // Don't change the status if the command already set it...
      if (result.GetStatus() == eReturnStatusInvalid) {
        if (result.GetOutputData().empty())
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
        else
          result.SetStatus(eReturnStatusSuccessFinishResult);
      }
    }
  }

private:
  std::string m_function_name;
  ScriptedCommandSynchronicity m_synchro;
  bool m_fetched_help_long = false;
  CompletionType m_completion_type = eNoCompletion;
};

/// This class implements a "raw" scripted command.  lldb does no parsing of the
/// command line, instead passing the line unaltered (except for backtick
/// substitution).
class CommandObjectScriptingObjectRaw : public CommandObjectRaw {
public:
  CommandObjectScriptingObjectRaw(CommandInterpreter &interpreter,
                                  std::string name,
                                  StructuredData::GenericSP cmd_obj_sp,
                                  ScriptedCommandSynchronicity synch,
                                  CompletionType completion_type)
      : CommandObjectRaw(interpreter, name), m_cmd_obj_sp(cmd_obj_sp),
        m_synchro(synch), m_fetched_help_short(false),
        m_fetched_help_long(false), m_completion_type(completion_type) {
    StreamString stream;
    stream.Printf("For more information run 'help %s'", name.c_str());
    SetHelp(stream.GetString());
    if (ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter())
      GetFlags().Set(scripter->GetFlagsForCommandObject(cmd_obj_sp));
  }

  ~CommandObjectScriptingObjectRaw() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), m_completion_type, request, nullptr);
  }

  bool WantsCompletion() override { return true; }

  bool IsRemovable() const override { return true; }

  ScriptedCommandSynchronicity GetSynchronicity() { return m_synchro; }

  std::optional<std::string> GetRepeatCommand(Args &args,
                                              uint32_t index) override {
    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();
    if (!scripter)
      return std::nullopt;

    return scripter->GetRepeatCommandForScriptedCommand(m_cmd_obj_sp, args);
  }

  llvm::StringRef GetHelp() override {
    if (m_fetched_help_short)
      return CommandObjectRaw::GetHelp();
    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();
    if (!scripter)
      return CommandObjectRaw::GetHelp();
    std::string docstring;
    m_fetched_help_short =
        scripter->GetShortHelpForCommandObject(m_cmd_obj_sp, docstring);
    if (!docstring.empty())
      SetHelp(docstring);

    return CommandObjectRaw::GetHelp();
  }

  llvm::StringRef GetHelpLong() override {
    if (m_fetched_help_long)
      return CommandObjectRaw::GetHelpLong();

    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();
    if (!scripter)
      return CommandObjectRaw::GetHelpLong();

    std::string docstring;
    m_fetched_help_long =
        scripter->GetLongHelpForCommandObject(m_cmd_obj_sp, docstring);
    if (!docstring.empty())
      SetHelpLong(docstring);
    return CommandObjectRaw::GetHelpLong();
  }

protected:
  void DoExecute(llvm::StringRef raw_command_line,
                 CommandReturnObject &result) override {
    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();

    Status error;

    result.SetStatus(eReturnStatusInvalid);

    if (!scripter ||
        !scripter->RunScriptBasedCommand(m_cmd_obj_sp, raw_command_line,
                                         m_synchro, result, error, m_exe_ctx)) {
      result.AppendError(error.AsCString());
    } else {
      // Don't change the status if the command already set it...
      if (result.GetStatus() == eReturnStatusInvalid) {
        if (result.GetOutputData().empty())
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
        else
          result.SetStatus(eReturnStatusSuccessFinishResult);
      }
    }
  }

private:
  StructuredData::GenericSP m_cmd_obj_sp;
  ScriptedCommandSynchronicity m_synchro;
  bool m_fetched_help_short : 1;
  bool m_fetched_help_long : 1;
  CompletionType m_completion_type = eNoCompletion;
};


/// This command implements a lldb parsed scripted command.  The command
/// provides a definition of the options and arguments, and a option value
/// setting callback, and then the command's execution function gets passed
/// just the parsed arguments.
/// Note, implementing a command in Python using these base interfaces is a bit
/// of a pain, but it is much easier to export this low level interface, and
/// then make it nicer on the Python side, than to try to do that in a
/// script language neutral way.
/// So I've also added a base class in Python that provides a table-driven
/// way of defining the options and arguments, which automatically fills the
/// option values, making them available as properties in Python.
/// 
class CommandObjectScriptingObjectParsed : public CommandObjectParsed {
private: 
  class CommandOptions : public Options {
  public:
    CommandOptions(CommandInterpreter &interpreter, 
        StructuredData::GenericSP cmd_obj_sp) : m_interpreter(interpreter), 
            m_cmd_obj_sp(cmd_obj_sp) {}

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      ScriptInterpreter *scripter = 
        m_interpreter.GetDebugger().GetScriptInterpreter();
      if (!scripter) {
        error.SetErrorString("No script interpreter for SetOptionValue.");
        return error;
      }
      if (!m_cmd_obj_sp) {
        error.SetErrorString("SetOptionValue called with empty cmd_obj.");
        return error;
      }
      if (!m_options_definition_up) {
        error.SetErrorString("SetOptionValue called before options definitions "
                             "were created.");
        return error;
      }
      // Pass the long option, since you aren't actually required to have a
      // short_option, and for those options the index or short option character
      // aren't meaningful on the python side.
      const char * long_option = 
        m_options_definition_up.get()[option_idx].long_option;
      bool success = scripter->SetOptionValueForCommandObject(m_cmd_obj_sp, 
        execution_context, long_option, option_arg);
      if (!success)
        error.SetErrorStringWithFormatv("Error setting option: {0} to {1}",
                                        long_option, option_arg);
      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      ScriptInterpreter *scripter = 
        m_interpreter.GetDebugger().GetScriptInterpreter();
      if (!scripter || !m_cmd_obj_sp)
        return;

      scripter->OptionParsingStartedForCommandObject(m_cmd_obj_sp);
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      if (!m_options_definition_up)
        return {};
      return llvm::ArrayRef(m_options_definition_up.get(), m_num_options);
    }
    
    static Status ParseUsageMaskFromArray(StructuredData::ObjectSP obj_sp, 
        size_t counter, uint32_t &usage_mask) {
      // If the usage entry is not provided, we use LLDB_OPT_SET_ALL.
      // If the usage mask is a UINT, the option belongs to that group.
      // If the usage mask is a vector of UINT's, the option belongs to all the
      // groups listed.
      // If a subelement of the vector is a vector of two ints, then the option
      // belongs to the inclusive range from the first to the second element.
      Status error;
      if (!obj_sp) {
        usage_mask = LLDB_OPT_SET_ALL;
        return error;
      }
      
      usage_mask = 0;
      
      StructuredData::UnsignedInteger *uint_val = 
          obj_sp->GetAsUnsignedInteger();
      if (uint_val) {
        // If this is an integer, then this specifies a single group:
        uint32_t value = uint_val->GetValue();
        if (value == 0) {
          error.SetErrorStringWithFormatv(
              "0 is not a valid group for option {0}", counter);
          return error;
        }
        usage_mask = (1 << (value - 1));
        return error;
      }
      // Otherwise it has to be an array:
      StructuredData::Array *array_val = obj_sp->GetAsArray();
      if (!array_val) {
        error.SetErrorStringWithFormatv(
            "required field is not a array for option {0}", counter);
        return error;
      }
      // This is the array ForEach for accumulating a group usage mask from
      // an array of string descriptions of groups.
      auto groups_accumulator 
          = [counter, &usage_mask, &error] 
            (StructuredData::Object *obj) -> bool {
        StructuredData::UnsignedInteger *int_val = obj->GetAsUnsignedInteger();
        if (int_val) {
          uint32_t value = int_val->GetValue();
          if (value == 0) {
            error.SetErrorStringWithFormatv(
                "0 is not a valid group for element {0}", counter);
            return false;
          }
          usage_mask |= (1 << (value - 1));
          return true;
        }
        StructuredData::Array *arr_val = obj->GetAsArray();
        if (!arr_val) {
          error.SetErrorStringWithFormatv(
              "Group element not an int or array of integers for element {0}", 
              counter);
          return false; 
        }
        size_t num_range_elem = arr_val->GetSize();
        if (num_range_elem != 2) {
          error.SetErrorStringWithFormatv(
              "Subranges of a group not a start and a stop for element {0}", 
              counter);
          return false; 
        }
        int_val = arr_val->GetItemAtIndex(0)->GetAsUnsignedInteger();
        if (!int_val) {
          error.SetErrorStringWithFormatv("Start element of a subrange of a "
              "group not unsigned int for element {0}", counter);
          return false; 
        }
        uint32_t start = int_val->GetValue();
        int_val = arr_val->GetItemAtIndex(1)->GetAsUnsignedInteger();
        if (!int_val) {
          error.SetErrorStringWithFormatv("End element of a subrange of a group"
              " not unsigned int for element {0}", counter);
          return false; 
        }
        uint32_t end = int_val->GetValue();
        if (start == 0 || end == 0 || start > end) {
          error.SetErrorStringWithFormatv("Invalid subrange of a group: {0} - "
              "{1} for element {2}", start, end, counter);
          return false;
        }
        for (uint32_t i = start; i <= end; i++) {
          usage_mask |= (1 << (i - 1));
        }
        return true;
      };
      array_val->ForEach(groups_accumulator);
      return error;
    }
    
    
    Status SetOptionsFromArray(StructuredData::Dictionary &options) {
      Status error;
      m_num_options = options.GetSize();
      m_options_definition_up.reset(new OptionDefinition[m_num_options]);
      // We need to hand out pointers to contents of these vectors; we reserve
      // as much as we'll need up front so they don't get freed on resize...
      m_usage_container.resize(m_num_options);
      m_enum_storage.resize(m_num_options);
      m_enum_vector.resize(m_num_options);
      
      size_t counter = 0;
      size_t short_opt_counter = 0;
      // This is the Array::ForEach function for adding option elements:
      auto add_element = [this, &error, &counter, &short_opt_counter] 
          (llvm::StringRef long_option, StructuredData::Object *object) -> bool {
        StructuredData::Dictionary *opt_dict = object->GetAsDictionary();
        if (!opt_dict) {
          error.SetErrorString("Value in options dictionary is not a dictionary");
          return false;
        }
        OptionDefinition &option_def = m_options_definition_up.get()[counter];
        
        // We aren't exposing the validator yet, set it to null
        option_def.validator = nullptr;
        // We don't require usage masks, so set it to one group by default:
        option_def.usage_mask = 1;
        
        // Now set the fields of the OptionDefinition Array from the dictionary:
        //
        // Note that I don't check for unknown fields in the option dictionaries
        // so a scriptor can add extra elements that are helpful when they go to
        // do "set_option_value"
        
        // Usage Mask:
        StructuredData::ObjectSP obj_sp = opt_dict->GetValueForKey("groups");
        if (obj_sp) {
          error = ParseUsageMaskFromArray(obj_sp, counter, 
                                          option_def.usage_mask);
          if (error.Fail())
            return false;
        }

        // Required:
        option_def.required = false;
        obj_sp = opt_dict->GetValueForKey("required");
        if (obj_sp) {
          StructuredData::Boolean *boolean_val = obj_sp->GetAsBoolean();
          if (!boolean_val) {
            error.SetErrorStringWithFormatv("'required' field is not a boolean "
                "for option {0}", counter);
            return false;
          } 
          option_def.required = boolean_val->GetValue();      
        }
        
        // Short Option:
        int short_option;
        obj_sp = opt_dict->GetValueForKey("short_option");
        if (obj_sp) {
          // The value is a string, so pull the 
          llvm::StringRef short_str = obj_sp->GetStringValue();
          if (short_str.empty()) {
            error.SetErrorStringWithFormatv("short_option field empty for "
                "option {0}", counter);
            return false;
          } else if (short_str.size() != 1) {
            error.SetErrorStringWithFormatv("short_option field has extra "
                "characters for option {0}", counter);
            return false;
          }
          short_option = (int) short_str[0];
        } else {
          // If the short option is not provided, then we need a unique value 
          // less than the lowest printable ASCII character.
          short_option = short_opt_counter++;
        }
        option_def.short_option = short_option;
        
        // Long Option is the key from the outer dict:
        if (long_option.empty()) {
          error.SetErrorStringWithFormatv("empty long_option for option {0}", 
              counter);
          return false;
        }
        auto inserted = g_string_storer.insert(long_option.str());
        option_def.long_option = ((*(inserted.first)).data());
        
        // Value Type:
        obj_sp = opt_dict->GetValueForKey("value_type");
        if (obj_sp) {
          StructuredData::UnsignedInteger *uint_val 
              = obj_sp->GetAsUnsignedInteger();
          if (!uint_val) {
            error.SetErrorStringWithFormatv("Value type must be an unsigned "
                "integer");
            return false;
          }
          uint64_t val_type = uint_val->GetValue();
          if (val_type >= eArgTypeLastArg) {
            error.SetErrorStringWithFormatv("Value type {0} beyond the "
                "CommandArgumentType bounds", val_type);
            return false;
          }
          option_def.argument_type = (CommandArgumentType) val_type;
          option_def.option_has_arg = true;
        } else {
          option_def.argument_type = eArgTypeNone;
          option_def.option_has_arg = false;
        }
        
        // Completion Type:
        obj_sp = opt_dict->GetValueForKey("completion_type");
        if (obj_sp) {
          StructuredData::UnsignedInteger *uint_val = obj_sp->GetAsUnsignedInteger();
          if (!uint_val) {
            error.SetErrorStringWithFormatv("Completion type must be an "
                "unsigned integer for option {0}", counter);
            return false;
          }
          uint64_t completion_type = uint_val->GetValue();
          if (completion_type > eCustomCompletion) {
            error.SetErrorStringWithFormatv("Completion type for option {0} "
                "beyond the CompletionType bounds", completion_type);
            return false;
          }
          option_def.completion_type = (CommandArgumentType) completion_type;
        } else
          option_def.completion_type = eNoCompletion;
        
        // Usage Text:
        std::string usage_text;
        obj_sp = opt_dict->GetValueForKey("help");
        if (!obj_sp) {
          error.SetErrorStringWithFormatv("required usage missing from option "
              "{0}", counter);
          return false;
        }
        llvm::StringRef usage_stref;
        usage_stref = obj_sp->GetStringValue();
        if (usage_stref.empty()) {
          error.SetErrorStringWithFormatv("empty usage text for option {0}", 
              counter);
          return false;
        }
        m_usage_container[counter] = usage_stref.str().c_str();
        option_def.usage_text = m_usage_container[counter].data();

        // Enum Values:
        
        obj_sp = opt_dict->GetValueForKey("enum_values");
        if (obj_sp) {
          StructuredData::Array *array = obj_sp->GetAsArray();
          if (!array) {
            error.SetErrorStringWithFormatv("enum values must be an array for "
                "option {0}", counter);
            return false;
          }
          size_t num_elem = array->GetSize();
          size_t enum_ctr = 0;
          m_enum_storage[counter] = std::vector<EnumValueStorage>(num_elem);
          std::vector<EnumValueStorage> &curr_elem = m_enum_storage[counter];
          
          // This is the Array::ForEach function for adding enum elements:
          // Since there are only two fields to specify the enum, use a simple
          // two element array with value first, usage second.
          // counter is only used for reporting so I pass it by value here.
          auto add_enum = [&enum_ctr, &curr_elem, counter, &error] 
              (StructuredData::Object *object) -> bool {
            StructuredData::Array *enum_arr = object->GetAsArray();
            if (!enum_arr) {
              error.SetErrorStringWithFormatv("Enum values for option {0} not "
                  "an array", counter);
              return false;
            }
            size_t num_enum_elements = enum_arr->GetSize();
            if (num_enum_elements != 2) {
              error.SetErrorStringWithFormatv("Wrong number of elements: {0} "
                  "for enum {1} in option {2}",
                  num_enum_elements, enum_ctr, counter);
              return false;
            }
            // Enum Value:
            StructuredData::ObjectSP obj_sp = enum_arr->GetItemAtIndex(0);
            llvm::StringRef val_stref = obj_sp->GetStringValue();
            std::string value_cstr_str = val_stref.str().c_str();
            
            // Enum Usage:
            obj_sp = enum_arr->GetItemAtIndex(1);
            if (!obj_sp) {
              error.SetErrorStringWithFormatv("No usage for enum {0} in option "
                  "{1}",  enum_ctr, counter);
              return false;
            }
            llvm::StringRef usage_stref = obj_sp->GetStringValue();
            std::string usage_cstr_str = usage_stref.str().c_str();
            curr_elem[enum_ctr] = EnumValueStorage(value_cstr_str, 
                usage_cstr_str, enum_ctr);
            
            enum_ctr++;
            return true;
          }; // end of add_enum
          
          array->ForEach(add_enum);
          if (!error.Success())
            return false;
          // We have to have a vector of elements to set in the options, make 
          // that here:
          for (auto &elem : curr_elem)
            m_enum_vector[counter].emplace_back(elem.element);

          option_def.enum_values = llvm::ArrayRef(m_enum_vector[counter]);
        }
        counter++;
        return true;
      }; // end of add_element
      
      options.ForEach(add_element);
      return error;
    }

    size_t GetNumOptions() { return m_num_options; }

  private:
    struct EnumValueStorage {
      EnumValueStorage() {
        element.string_value = "value not set";
        element.usage = "usage not set";
        element.value = 0;
      }
      
      EnumValueStorage(std::string in_str_val, std::string in_usage, 
          size_t in_value) : value(std::move(in_str_val)), usage(std::move(in_usage)) {
        SetElement(in_value);
      }
      
      EnumValueStorage(const EnumValueStorage &in) : value(in.value), 
          usage(in.usage) {
        SetElement(in.element.value);
      }
      
      EnumValueStorage &operator=(const EnumValueStorage &in) {
        value = in.value;
        usage = in.usage;
        SetElement(in.element.value);
        return *this;
      }
      
      void SetElement(size_t in_value) {
        element.value = in_value;
        element.string_value = value.data();
        element.usage = usage.data(); 
      }
      
      std::string value;
      std::string usage;
      OptionEnumValueElement element;
    };
    // We have to provide char * values for the long option, usage and enum
    // values, that's what the option definitions hold.
    // The long option strings are quite likely to be reused in other added
    // commands, so those are stored in a global set: g_string_storer.
    // But the usages are much less likely to be reused, so those are stored in
    // a vector in the command instance.  It gets resized to the correct size
    // and then filled with null-terminated strings in the std::string, so the 
    // are valid C-strings that won't move around.
    // The enum values and descriptions are treated similarly - these aren't
    // all that common so it's not worth the effort to dedup them.  
    size_t m_num_options = 0;
    std::unique_ptr<OptionDefinition> m_options_definition_up;
    std::vector<std::vector<EnumValueStorage>> m_enum_storage;
    std::vector<std::vector<OptionEnumValueElement>> m_enum_vector;
    std::vector<std::string> m_usage_container;
    CommandInterpreter &m_interpreter;
    StructuredData::GenericSP m_cmd_obj_sp;
    static std::unordered_set<std::string> g_string_storer;
  };

public:
  static CommandObjectSP Create(CommandInterpreter &interpreter, 
                std::string name,
                StructuredData::GenericSP cmd_obj_sp,
                ScriptedCommandSynchronicity synch, 
                CommandReturnObject &result) {
    CommandObjectSP new_cmd_sp(new CommandObjectScriptingObjectParsed(
        interpreter, name, cmd_obj_sp, synch));

    CommandObjectScriptingObjectParsed *parsed_cmd 
        = static_cast<CommandObjectScriptingObjectParsed *>(new_cmd_sp.get());
    // Now check all the failure modes, and report if found.
    Status opt_error = parsed_cmd->GetOptionsError();
    Status arg_error = parsed_cmd->GetArgsError();

    if (opt_error.Fail())
      result.AppendErrorWithFormat("failed to parse option definitions: %s",
                                   opt_error.AsCString());
    if (arg_error.Fail())
      result.AppendErrorWithFormat("%sfailed to parse argument definitions: %s",
                                   opt_error.Fail() ? ", also " : "", 
                                   arg_error.AsCString());

    if (!result.Succeeded())
      return {};

    return new_cmd_sp;
  }

  CommandObjectScriptingObjectParsed(CommandInterpreter &interpreter,
                               std::string name,
                               StructuredData::GenericSP cmd_obj_sp,
                               ScriptedCommandSynchronicity synch)
      : CommandObjectParsed(interpreter, name.c_str()), 
        m_cmd_obj_sp(cmd_obj_sp), m_synchro(synch), 
        m_options(interpreter, cmd_obj_sp), m_fetched_help_short(false), 
        m_fetched_help_long(false) {
    StreamString stream;
    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();
    if (!scripter) {
      m_options_error.SetErrorString("No script interpreter");
      return;
    }

    // Set the flags:
    GetFlags().Set(scripter->GetFlagsForCommandObject(cmd_obj_sp));

    // Now set up the options definitions from the options:
    StructuredData::ObjectSP options_object_sp 
        = scripter->GetOptionsForCommandObject(cmd_obj_sp);
    // It's okay not to have an options dict.
    if (options_object_sp) {
      // The options come as a dictionary of dictionaries.  The key of the
      // outer dict is the long option name (since that's required).  The
      // value holds all the other option specification bits.
      StructuredData::Dictionary *options_dict 
          = options_object_sp->GetAsDictionary();
      // but if it exists, it has to be an array.
      if (options_dict) {
        m_options_error = m_options.SetOptionsFromArray(*(options_dict));
        // If we got an error don't bother with the arguments...
        if (m_options_error.Fail())
          return;
      } else {
        m_options_error.SetErrorString("Options array not an array");
        return;
      }
    }
    // Then fetch the args.  Since the arguments can have usage masks you need
    // an array of arrays.
    StructuredData::ObjectSP args_object_sp 
      = scripter->GetArgumentsForCommandObject(cmd_obj_sp);
    if (args_object_sp) {
      StructuredData::Array *args_array = args_object_sp->GetAsArray();        
      if (!args_array) {
        m_args_error.SetErrorString("Argument specification is not an array");
        return;
      }
      size_t counter = 0;
      
      // This is the Array::ForEach function that handles the
      // CommandArgumentEntry arrays one by one:
      auto arg_array_adder = [this, &counter] (StructuredData::Object *object) 
          -> bool {
        // This is the Array::ForEach function to add argument entries:
        CommandArgumentEntry this_entry;
        size_t elem_counter = 0;
        auto args_adder = [this, counter, &elem_counter, &this_entry] 
            (StructuredData::Object *object) -> bool {
          // The arguments definition has three fields, the argument type, the
          // repeat and the usage mask. 
          CommandArgumentType arg_type = eArgTypeNone;
          ArgumentRepetitionType arg_repetition = eArgRepeatOptional;
          uint32_t arg_opt_set_association;
          
          auto report_error = [this, elem_counter, counter] 
              (const char *err_txt) -> bool {
            m_args_error.SetErrorStringWithFormatv("Element {0} of arguments "
                "list element {1}: %s.", elem_counter, counter, err_txt);
            return false;
          };
          
          StructuredData::Dictionary *arg_dict = object->GetAsDictionary();
          if (!arg_dict) {
            report_error("is not a dictionary.");
            return false;
          }
          // Argument Type:
          StructuredData::ObjectSP obj_sp 
              = arg_dict->GetValueForKey("arg_type");
          if (obj_sp) {
            StructuredData::UnsignedInteger *uint_val 
                = obj_sp->GetAsUnsignedInteger();
            if (!uint_val) {
              report_error("value type must be an unsigned integer");
              return false;
            }
            uint64_t arg_type_int = uint_val->GetValue();
            if (arg_type_int >= eArgTypeLastArg) {
              report_error("value type beyond ArgumentRepetitionType bounds");
              return false;
            }
            arg_type = (CommandArgumentType) arg_type_int;
          }
          // Repeat Value:
          obj_sp = arg_dict->GetValueForKey("repeat");
          std::optional<ArgumentRepetitionType> repeat;
          if (obj_sp) {
            llvm::StringRef repeat_str = obj_sp->GetStringValue();
            if (repeat_str.empty()) {
              report_error("repeat value is empty");
              return false;
            }
            repeat = ArgRepetitionFromString(repeat_str);
            if (!repeat) {
              report_error("invalid repeat value");
              return false;
            }
            arg_repetition = *repeat;
          } 
          
          // Usage Mask:
          obj_sp = arg_dict->GetValueForKey("groups");
          m_args_error = CommandOptions::ParseUsageMaskFromArray(obj_sp, 
              counter, arg_opt_set_association);
          this_entry.emplace_back(arg_type, arg_repetition, 
              arg_opt_set_association);
          elem_counter++;
          return true;
        };
        StructuredData::Array *args_array = object->GetAsArray();
        if (!args_array) {
          m_args_error.SetErrorStringWithFormatv("Argument definition element "
              "{0} is not an array", counter);
        }
        
        args_array->ForEach(args_adder);
        if (m_args_error.Fail())
          return false;
        if (this_entry.empty()) {
          m_args_error.SetErrorStringWithFormatv("Argument definition element "
              "{0} is empty", counter);
          return false;
        }
        m_arguments.push_back(this_entry);
        counter++;
        return true;
      }; // end of arg_array_adder
      // Here we actually parse the args definition:
      args_array->ForEach(arg_array_adder);
    }
  }

  ~CommandObjectScriptingObjectParsed() override = default;

  Status GetOptionsError() { return m_options_error; }
  Status GetArgsError() { return m_args_error; }
  bool WantsCompletion() override { return true; }

  bool IsRemovable() const override { return true; }

  ScriptedCommandSynchronicity GetSynchronicity() { return m_synchro; }

  std::optional<std::string> GetRepeatCommand(Args &args,
                                              uint32_t index) override {
    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();
    if (!scripter)
      return std::nullopt;

    return scripter->GetRepeatCommandForScriptedCommand(m_cmd_obj_sp, args);
  }

  llvm::StringRef GetHelp() override {
    if (m_fetched_help_short)
      return CommandObjectParsed::GetHelp();
    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();
    if (!scripter)
      return CommandObjectParsed::GetHelp();
    std::string docstring;
    m_fetched_help_short =
        scripter->GetShortHelpForCommandObject(m_cmd_obj_sp, docstring);
    if (!docstring.empty())
      SetHelp(docstring);

    return CommandObjectParsed::GetHelp();
  }

  llvm::StringRef GetHelpLong() override {
    if (m_fetched_help_long)
      return CommandObjectParsed::GetHelpLong();

    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();
    if (!scripter)
      return CommandObjectParsed::GetHelpLong();

    std::string docstring;
    m_fetched_help_long =
        scripter->GetLongHelpForCommandObject(m_cmd_obj_sp, docstring);
    if (!docstring.empty())
      SetHelpLong(docstring);
    return CommandObjectParsed::GetHelpLong();
  }

  Options *GetOptions() override {
    // CommandObjectParsed requires that a command with no options return
    // nullptr.
    if (m_options.GetNumOptions() == 0)
      return nullptr;
    return &m_options;
  }

protected:
  void DoExecute(Args &args,
                 CommandReturnObject &result) override {
    ScriptInterpreter *scripter = GetDebugger().GetScriptInterpreter();

    Status error;

    result.SetStatus(eReturnStatusInvalid);
    
    if (!scripter ||
        !scripter->RunScriptBasedParsedCommand(m_cmd_obj_sp, args,
                                         m_synchro, result, error, m_exe_ctx)) {
      result.AppendError(error.AsCString());
    } else {
      // Don't change the status if the command already set it...
      if (result.GetStatus() == eReturnStatusInvalid) {
        if (result.GetOutputData().empty())
          result.SetStatus(eReturnStatusSuccessFinishNoResult);
        else
          result.SetStatus(eReturnStatusSuccessFinishResult);
      }
    }
  }

private:
  StructuredData::GenericSP m_cmd_obj_sp;
  ScriptedCommandSynchronicity m_synchro;
  CommandOptions m_options;
  Status m_options_error;
  Status m_args_error;
  bool m_fetched_help_short : 1;
  bool m_fetched_help_long : 1;
};

std::unordered_set<std::string>
    CommandObjectScriptingObjectParsed::CommandOptions::g_string_storer;

// CommandObjectCommandsScriptImport
#define LLDB_OPTIONS_script_import
#include "CommandOptions.inc"

class CommandObjectCommandsScriptImport : public CommandObjectParsed {
public:
  CommandObjectCommandsScriptImport(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "command script import",
                            "Import a scripting module in LLDB.", nullptr) {
    AddSimpleArgumentList(eArgTypeFilename, eArgRepeatPlus);
  }

  ~CommandObjectCommandsScriptImport() override = default;

  Options *GetOptions() override { return &m_options; }

protected:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'r':
        // NO-OP
        break;
      case 'c':
        relative_to_command_file = true;
        break;
      case 's':
        silent = true;
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      relative_to_command_file = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_script_import_options);
    }
    bool relative_to_command_file = false;
    bool silent = false;
  };

  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (command.empty()) {
      result.AppendError("command script import needs one or more arguments");
      return;
    }

    FileSpec source_dir = {};
    if (m_options.relative_to_command_file) {
      source_dir = GetDebugger().GetCommandInterpreter().GetCurrentSourceDir();
      if (!source_dir) {
        result.AppendError("command script import -c can only be specified "
                           "from a command file");
        return;
      }
    }

    for (auto &entry : command.entries()) {
      Status error;

      LoadScriptOptions options;
      options.SetInitSession(true);
      options.SetSilent(m_options.silent);

      // FIXME: this is necessary because CommandObject::CheckRequirements()
      // assumes that commands won't ever be recursively invoked, but it's
      // actually possible to craft a Python script that does other "command
      // script imports" in __lldb_init_module the real fix is to have
      // recursive commands possible with a CommandInvocation object separate
      // from the CommandObject itself, so that recursive command invocations
      // won't stomp on each other (wrt to execution contents, options, and
      // more)
      m_exe_ctx.Clear();
      if (GetDebugger().GetScriptInterpreter()->LoadScriptingModule(
              entry.c_str(), options, error, /*module_sp=*/nullptr,
              source_dir)) {
        result.SetStatus(eReturnStatusSuccessFinishNoResult);
      } else {
        result.AppendErrorWithFormat("module importing failed: %s",
                                     error.AsCString());
      }
    }
  }

  CommandOptions m_options;
};

#define LLDB_OPTIONS_script_add
#include "CommandOptions.inc"

class CommandObjectCommandsScriptAdd : public CommandObjectParsed,
                                       public IOHandlerDelegateMultiline {
public:
  CommandObjectCommandsScriptAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "command script add",
                            "Add a scripted function as an LLDB command.",
                            "Add a scripted function as an lldb command. "
                            "If you provide a single argument, the command "
                            "will be added at the root level of the command "
                            "hierarchy.  If there are more arguments they "
                            "must be a path to a user-added container "
                            "command, and the last element will be the new "
                            "command name."),
        IOHandlerDelegateMultiline("DONE") {
    AddSimpleArgumentList(eArgTypeCommand, eArgRepeatPlus);
  }

  ~CommandObjectCommandsScriptAdd() override = default;

  Options *GetOptions() override { return &m_options; }

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    CommandCompletions::CompleteModifiableCmdPathArgs(m_interpreter, request,
                                                      opt_element_vector);
  }

protected:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'f':
        if (!option_arg.empty())
          m_funct_name = std::string(option_arg);
        break;
      case 'c':
        if (!option_arg.empty())
          m_class_name = std::string(option_arg);
        break;
      case 'h':
        if (!option_arg.empty())
          m_short_help = std::string(option_arg);
        break;
      case 'o':
        m_overwrite_lazy = eLazyBoolYes;
        break;
      case 'p':
        m_parsed_command = true;
        break;
      case 's':
        m_synchronicity =
            (ScriptedCommandSynchronicity)OptionArgParser::ToOptionEnum(
                option_arg, GetDefinitions()[option_idx].enum_values, 0, error);
        if (!error.Success())
          error.SetErrorStringWithFormat(
              "unrecognized value for synchronicity '%s'",
              option_arg.str().c_str());
        break;
      case 'C': {
        Status error;
        OptionDefinition definition = GetDefinitions()[option_idx];
        lldb::CompletionType completion_type =
            static_cast<lldb::CompletionType>(OptionArgParser::ToOptionEnum(
                option_arg, definition.enum_values, eNoCompletion, error));
        if (!error.Success())
          error.SetErrorStringWithFormat(
              "unrecognized value for command completion type '%s'",
              option_arg.str().c_str());
        m_completion_type = completion_type;
      } break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_class_name.clear();
      m_funct_name.clear();
      m_short_help.clear();
      m_completion_type = eNoCompletion;
      m_overwrite_lazy = eLazyBoolCalculate;
      m_synchronicity = eScriptedCommandSynchronicitySynchronous;
      m_parsed_command = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_script_add_options);
    }

    // Instance variables to hold the values for command options.

    std::string m_class_name;
    std::string m_funct_name;
    std::string m_short_help;
    LazyBool m_overwrite_lazy = eLazyBoolCalculate;
    ScriptedCommandSynchronicity m_synchronicity =
        eScriptedCommandSynchronicitySynchronous;
    CompletionType m_completion_type = eNoCompletion;
    bool m_parsed_command = false;
  };

  void IOHandlerActivated(IOHandler &io_handler, bool interactive) override {
    StreamFileSP output_sp(io_handler.GetOutputStreamFileSP());
    if (output_sp && interactive) {
      output_sp->PutCString(g_python_command_instructions);
      output_sp->Flush();
    }
  }

  void IOHandlerInputComplete(IOHandler &io_handler,
                              std::string &data) override {
    StreamFileSP error_sp = io_handler.GetErrorStreamFileSP();

    ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();
    if (interpreter) {
      StringList lines;
      lines.SplitIntoLines(data);
      if (lines.GetSize() > 0) {
        std::string funct_name_str;
        if (interpreter->GenerateScriptAliasFunction(lines, funct_name_str)) {
          if (funct_name_str.empty()) {
            error_sp->Printf("error: unable to obtain a function name, didn't "
                             "add python command.\n");
            error_sp->Flush();
          } else {
            // everything should be fine now, let's add this alias

            CommandObjectSP command_obj_sp(new CommandObjectPythonFunction(
                m_interpreter, m_cmd_name, funct_name_str, m_short_help,
                m_synchronicity, m_completion_type));
            if (!m_container) {
              Status error = m_interpreter.AddUserCommand(
                  m_cmd_name, command_obj_sp, m_overwrite);
              if (error.Fail()) {
                error_sp->Printf("error: unable to add selected command: '%s'",
                                 error.AsCString());
                error_sp->Flush();
              }
            } else {
              llvm::Error llvm_error = m_container->LoadUserSubcommand(
                  m_cmd_name, command_obj_sp, m_overwrite);
              if (llvm_error) {
                error_sp->Printf("error: unable to add selected command: '%s'",
                               llvm::toString(std::move(llvm_error)).c_str());
                error_sp->Flush();
              }
            }
          }
        } else {
          error_sp->Printf(
              "error: unable to create function, didn't add python command\n");
          error_sp->Flush();
        }
      } else {
        error_sp->Printf("error: empty function, didn't add python command\n");
        error_sp->Flush();
      }
    } else {
      error_sp->Printf(
          "error: script interpreter missing, didn't add python command\n");
      error_sp->Flush();
    }

    io_handler.SetIsDone(true);
  }

  void DoExecute(Args &command, CommandReturnObject &result) override {
    if (GetDebugger().GetScriptLanguage() != lldb::eScriptLanguagePython) {
      result.AppendError("only scripting language supported for scripted "
                         "commands is currently Python");
      return;
    }

    if (command.GetArgumentCount() == 0) {
      result.AppendError("'command script add' requires at least one argument");
      return;
    }
    // Store the options in case we get multi-line input, also figure out the
    // default if not user supplied:
    switch (m_options.m_overwrite_lazy) {
      case eLazyBoolCalculate:
        m_overwrite = !GetDebugger().GetCommandInterpreter().GetRequireCommandOverwrite();
        break;
      case eLazyBoolYes:
        m_overwrite = true;
        break;
      case eLazyBoolNo:
        m_overwrite = false;
    }
    
    Status path_error;
    m_container = GetCommandInterpreter().VerifyUserMultiwordCmdPath(
        command, true, path_error);

    if (path_error.Fail()) {
      result.AppendErrorWithFormat("error in command path: %s",
                                   path_error.AsCString());
      return;
    }

    if (!m_container) {
      // This is getting inserted into the root of the interpreter.
      m_cmd_name = std::string(command[0].ref());
    } else {
      size_t num_args = command.GetArgumentCount();
      m_cmd_name = std::string(command[num_args - 1].ref());
    }

    m_short_help.assign(m_options.m_short_help);
    m_synchronicity = m_options.m_synchronicity;
    m_completion_type = m_options.m_completion_type;

    // Handle the case where we prompt for the script code first:
    if (m_options.m_class_name.empty() && m_options.m_funct_name.empty()) {
      m_interpreter.GetPythonCommandsFromIOHandler("     ", // Prompt
                                                   *this);  // IOHandlerDelegate
      return;
    }

    CommandObjectSP new_cmd_sp;
    if (m_options.m_class_name.empty()) {
      new_cmd_sp.reset(new CommandObjectPythonFunction(
          m_interpreter, m_cmd_name, m_options.m_funct_name,
          m_options.m_short_help, m_synchronicity, m_completion_type));
    } else {
      ScriptInterpreter *interpreter = GetDebugger().GetScriptInterpreter();
      if (!interpreter) {
        result.AppendError("cannot find ScriptInterpreter");
        return;
      }

      auto cmd_obj_sp = interpreter->CreateScriptCommandObject(
          m_options.m_class_name.c_str());
      if (!cmd_obj_sp) {
        result.AppendErrorWithFormatv("cannot create helper object for: "
                                      "'{0}'", m_options.m_class_name);
        return;
      }
      
      if (m_options.m_parsed_command) {
        new_cmd_sp = CommandObjectScriptingObjectParsed::Create(m_interpreter, 
            m_cmd_name, cmd_obj_sp, m_synchronicity, result);
        if (!result.Succeeded())
          return;
      } else
        new_cmd_sp.reset(new CommandObjectScriptingObjectRaw(
            m_interpreter, m_cmd_name, cmd_obj_sp, m_synchronicity,
            m_completion_type));
    }
    
    // Assume we're going to succeed...
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    if (!m_container) {
      Status add_error =
          m_interpreter.AddUserCommand(m_cmd_name, new_cmd_sp, m_overwrite);
      if (add_error.Fail())
        result.AppendErrorWithFormat("cannot add command: %s",
                                     add_error.AsCString());
    } else {
      llvm::Error llvm_error =
          m_container->LoadUserSubcommand(m_cmd_name, new_cmd_sp, m_overwrite);
      if (llvm_error)
        result.AppendErrorWithFormat(
            "cannot add command: %s",
            llvm::toString(std::move(llvm_error)).c_str());
    }
  }

  CommandOptions m_options;
  std::string m_cmd_name;
  CommandObjectMultiword *m_container = nullptr;
  std::string m_short_help;
  bool m_overwrite = false;
  ScriptedCommandSynchronicity m_synchronicity =
      eScriptedCommandSynchronicitySynchronous;
  CompletionType m_completion_type = eNoCompletion;
};

// CommandObjectCommandsScriptList

class CommandObjectCommandsScriptList : public CommandObjectParsed {
public:
  CommandObjectCommandsScriptList(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "command script list",
                            "List defined top-level scripted commands.",
                            nullptr) {}

  ~CommandObjectCommandsScriptList() override = default;

  void DoExecute(Args &command, CommandReturnObject &result) override {
    m_interpreter.GetHelp(result, CommandInterpreter::eCommandTypesUserDef);

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// CommandObjectCommandsScriptClear

class CommandObjectCommandsScriptClear : public CommandObjectParsed {
public:
  CommandObjectCommandsScriptClear(CommandInterpreter &interpreter)
      : CommandObjectParsed(interpreter, "command script clear",
                            "Delete all scripted commands.", nullptr) {}

  ~CommandObjectCommandsScriptClear() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    m_interpreter.RemoveAllUser();

    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

// CommandObjectCommandsScriptDelete

class CommandObjectCommandsScriptDelete : public CommandObjectParsed {
public:
  CommandObjectCommandsScriptDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "command script delete",
            "Delete a scripted command by specifying the path to the command.",
            nullptr) {
    AddSimpleArgumentList(eArgTypeCommand, eArgRepeatPlus);
  }

  ~CommandObjectCommandsScriptDelete() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::CompleteModifiableCmdPathArgs(
        m_interpreter, request, opt_element_vector);
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {

    llvm::StringRef root_cmd = command[0].ref();
    size_t num_args = command.GetArgumentCount();

    if (root_cmd.empty()) {
      result.AppendErrorWithFormat("empty root command name");
      return;
    }
    if (!m_interpreter.HasUserCommands() &&
        !m_interpreter.HasUserMultiwordCommands()) {
      result.AppendErrorWithFormat("can only delete user defined commands, "
                                   "but no user defined commands found");
      return;
    }

    CommandObjectSP cmd_sp = m_interpreter.GetCommandSPExact(root_cmd);
    if (!cmd_sp) {
      result.AppendErrorWithFormat("command '%s' not found.",
                                   command[0].c_str());
      return;
    }
    if (!cmd_sp->IsUserCommand()) {
      result.AppendErrorWithFormat("command '%s' is not a user command.",
                                   command[0].c_str());
      return;
    }
    if (cmd_sp->GetAsMultiwordCommand() && num_args == 1) {
      result.AppendErrorWithFormat("command '%s' is a multi-word command.\n "
                                   "Delete with \"command container delete\"",
                                   command[0].c_str());
      return;
    }

    if (command.GetArgumentCount() == 1) {
      m_interpreter.RemoveUser(root_cmd);
      result.SetStatus(eReturnStatusSuccessFinishResult);
      return;
    }
    // We're deleting a command from a multiword command.  Verify the command
    // path:
    Status error;
    CommandObjectMultiword *container =
        GetCommandInterpreter().VerifyUserMultiwordCmdPath(command, true,
                                                           error);
    if (error.Fail()) {
      result.AppendErrorWithFormat("could not resolve command path: %s",
                                   error.AsCString());
      return;
    }
    if (!container) {
      // This means that command only had a leaf command, so the container is
      // the root.  That should have been handled above.
      result.AppendErrorWithFormat("could not find a container for '%s'",
                                   command[0].c_str());
      return;
    }
    const char *leaf_cmd = command[num_args - 1].c_str();
    llvm::Error llvm_error =
        container->RemoveUserSubcommand(leaf_cmd,
                                        /* multiword not okay */ false);
    if (llvm_error) {
      result.AppendErrorWithFormat(
          "could not delete command '%s': %s", leaf_cmd,
          llvm::toString(std::move(llvm_error)).c_str());
      return;
    }

    Stream &out_stream = result.GetOutputStream();

    out_stream << "Deleted command:";
    for (size_t idx = 0; idx < num_args; idx++) {
      out_stream << ' ';
      out_stream << command[idx].c_str();
    }
    out_stream << '\n';
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
};

#pragma mark CommandObjectMultiwordCommandsScript

// CommandObjectMultiwordCommandsScript

class CommandObjectMultiwordCommandsScript : public CommandObjectMultiword {
public:
  CommandObjectMultiwordCommandsScript(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "command script",
            "Commands for managing custom "
            "commands implemented by "
            "interpreter scripts.",
            "command script <subcommand> [<subcommand-options>]") {
    LoadSubCommand("add", CommandObjectSP(
                              new CommandObjectCommandsScriptAdd(interpreter)));
    LoadSubCommand(
        "delete",
        CommandObjectSP(new CommandObjectCommandsScriptDelete(interpreter)));
    LoadSubCommand(
        "clear",
        CommandObjectSP(new CommandObjectCommandsScriptClear(interpreter)));
    LoadSubCommand("list", CommandObjectSP(new CommandObjectCommandsScriptList(
                               interpreter)));
    LoadSubCommand(
        "import",
        CommandObjectSP(new CommandObjectCommandsScriptImport(interpreter)));
  }

  ~CommandObjectMultiwordCommandsScript() override = default;
};

#pragma mark CommandObjectCommandContainer
#define LLDB_OPTIONS_container_add
#include "CommandOptions.inc"

class CommandObjectCommandsContainerAdd : public CommandObjectParsed {
public:
  CommandObjectCommandsContainerAdd(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "command container add",
            "Add a container command to lldb.  Adding to built-"
            "in container commands is not allowed.",
            "command container add [[path1]...] container-name") {
    AddSimpleArgumentList(eArgTypeCommand, eArgRepeatPlus);
  }

  ~CommandObjectCommandsContainerAdd() override = default;

  Options *GetOptions() override { return &m_options; }

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::CompleteModifiableCmdPathArgs(
        m_interpreter, request, opt_element_vector);
  }

protected:
  class CommandOptions : public Options {
  public:
    CommandOptions() = default;

    ~CommandOptions() override = default;

    Status SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                          ExecutionContext *execution_context) override {
      Status error;
      const int short_option = m_getopt_table[option_idx].val;

      switch (short_option) {
      case 'h':
        if (!option_arg.empty())
          m_short_help = std::string(option_arg);
        break;
      case 'o':
        m_overwrite = true;
        break;
      case 'H':
        if (!option_arg.empty())
          m_long_help = std::string(option_arg);
        break;
      default:
        llvm_unreachable("Unimplemented option");
      }

      return error;
    }

    void OptionParsingStarting(ExecutionContext *execution_context) override {
      m_short_help.clear();
      m_long_help.clear();
      m_overwrite = false;
    }

    llvm::ArrayRef<OptionDefinition> GetDefinitions() override {
      return llvm::ArrayRef(g_container_add_options);
    }

    // Instance variables to hold the values for command options.

    std::string m_short_help;
    std::string m_long_help;
    bool m_overwrite = false;
  };
  void DoExecute(Args &command, CommandReturnObject &result) override {
    size_t num_args = command.GetArgumentCount();

    if (num_args == 0) {
      result.AppendError("no command was specified");
      return;
    }

    if (num_args == 1) {
      // We're adding this as a root command, so use the interpreter.
      const char *cmd_name = command.GetArgumentAtIndex(0);
      auto cmd_sp = CommandObjectSP(new CommandObjectMultiword(
          GetCommandInterpreter(), cmd_name, m_options.m_short_help.c_str(),
          m_options.m_long_help.c_str()));
      cmd_sp->GetAsMultiwordCommand()->SetRemovable(true);
      Status add_error = GetCommandInterpreter().AddUserCommand(
          cmd_name, cmd_sp, m_options.m_overwrite);
      if (add_error.Fail()) {
        result.AppendErrorWithFormat("error adding command: %s",
                                     add_error.AsCString());
        return;
      }
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return;
    }

    // We're adding this to a subcommand, first find the subcommand:
    Status path_error;
    CommandObjectMultiword *add_to_me =
        GetCommandInterpreter().VerifyUserMultiwordCmdPath(command, true,
                                                           path_error);

    if (!add_to_me) {
      result.AppendErrorWithFormat("error adding command: %s",
                                   path_error.AsCString());
      return;
    }

    const char *cmd_name = command.GetArgumentAtIndex(num_args - 1);
    auto cmd_sp = CommandObjectSP(new CommandObjectMultiword(
        GetCommandInterpreter(), cmd_name, m_options.m_short_help.c_str(),
        m_options.m_long_help.c_str()));
    llvm::Error llvm_error =
        add_to_me->LoadUserSubcommand(cmd_name, cmd_sp, m_options.m_overwrite);
    if (llvm_error) {
      result.AppendErrorWithFormat("error adding subcommand: %s",
                                   llvm::toString(std::move(llvm_error)).c_str());
      return;
    }

    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }

private:
  CommandOptions m_options;
};

#define LLDB_OPTIONS_multiword_delete
#include "CommandOptions.inc"
class CommandObjectCommandsContainerDelete : public CommandObjectParsed {
public:
  CommandObjectCommandsContainerDelete(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "command container delete",
            "Delete a container command previously added to "
            "lldb.",
            "command container delete [[path1] ...] container-cmd") {
    AddSimpleArgumentList(eArgTypeCommand, eArgRepeatPlus);
  }

  ~CommandObjectCommandsContainerDelete() override = default;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override {
    lldb_private::CommandCompletions::CompleteModifiableCmdPathArgs(
        m_interpreter, request, opt_element_vector);
  }

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    size_t num_args = command.GetArgumentCount();

    if (num_args == 0) {
      result.AppendError("No command was specified.");
      return;
    }

    if (num_args == 1) {
      // We're removing a root command, so we need to delete it from the
      // interpreter.
      const char *cmd_name = command.GetArgumentAtIndex(0);
      // Let's do a little more work here so we can do better error reporting.
      CommandInterpreter &interp = GetCommandInterpreter();
      CommandObjectSP cmd_sp = interp.GetCommandSPExact(cmd_name);
      if (!cmd_sp) {
        result.AppendErrorWithFormat("container command %s doesn't exist.",
                                     cmd_name);
        return;
      }
      if (!cmd_sp->IsUserCommand()) {
        result.AppendErrorWithFormat(
            "container command %s is not a user command", cmd_name);
        return;
      }
      if (!cmd_sp->GetAsMultiwordCommand()) {
        result.AppendErrorWithFormat("command %s is not a container command",
                                     cmd_name);
        return;
      }

      bool did_remove = GetCommandInterpreter().RemoveUserMultiword(cmd_name);
      if (!did_remove) {
        result.AppendErrorWithFormat("error removing command %s.", cmd_name);
        return;
      }

      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return;
    }

    // We're removing a subcommand, first find the subcommand's owner:
    Status path_error;
    CommandObjectMultiword *container =
        GetCommandInterpreter().VerifyUserMultiwordCmdPath(command, true,
                                                           path_error);

    if (!container) {
      result.AppendErrorWithFormat("error removing container command: %s",
                                   path_error.AsCString());
      return;
    }
    const char *leaf = command.GetArgumentAtIndex(num_args - 1);
    llvm::Error llvm_error =
        container->RemoveUserSubcommand(leaf, /* multiword okay */ true);
    if (llvm_error) {
      result.AppendErrorWithFormat("error removing container command: %s",
                                   llvm::toString(std::move(llvm_error)).c_str());
      return;
    }
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
  }
};

class CommandObjectCommandContainer : public CommandObjectMultiword {
public:
  CommandObjectCommandContainer(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "command container",
            "Commands for adding container commands to lldb.  "
            "Container commands are containers for other commands.  You can "
            "add nested container commands by specifying a command path, "
            "but you can't add commands into the built-in command hierarchy.",
            "command container <subcommand> [<subcommand-options>]") {
    LoadSubCommand("add", CommandObjectSP(new CommandObjectCommandsContainerAdd(
                              interpreter)));
    LoadSubCommand(
        "delete",
        CommandObjectSP(new CommandObjectCommandsContainerDelete(interpreter)));
  }

  ~CommandObjectCommandContainer() override = default;
};

#pragma mark CommandObjectMultiwordCommands

// CommandObjectMultiwordCommands

CommandObjectMultiwordCommands::CommandObjectMultiwordCommands(
    CommandInterpreter &interpreter)
    : CommandObjectMultiword(interpreter, "command",
                             "Commands for managing custom LLDB commands.",
                             "command <subcommand> [<subcommand-options>]") {
  LoadSubCommand("source",
                 CommandObjectSP(new CommandObjectCommandsSource(interpreter)));
  LoadSubCommand("alias",
                 CommandObjectSP(new CommandObjectCommandsAlias(interpreter)));
  LoadSubCommand("unalias", CommandObjectSP(
                                new CommandObjectCommandsUnalias(interpreter)));
  LoadSubCommand("delete",
                 CommandObjectSP(new CommandObjectCommandsDelete(interpreter)));
  LoadSubCommand("container", CommandObjectSP(new CommandObjectCommandContainer(
                                  interpreter)));
  LoadSubCommand(
      "regex", CommandObjectSP(new CommandObjectCommandsAddRegex(interpreter)));
  LoadSubCommand(
      "script",
      CommandObjectSP(new CommandObjectMultiwordCommandsScript(interpreter)));
}

CommandObjectMultiwordCommands::~CommandObjectMultiwordCommands() = default;
