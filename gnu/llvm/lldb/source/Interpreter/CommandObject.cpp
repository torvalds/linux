//===-- CommandObject.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/CommandObject.h"

#include <map>
#include <sstream>
#include <string>

#include <cctype>
#include <cstdlib>

#include "lldb/Core/Address.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/ArchSpec.h"
#include "llvm/ADT/ScopeExit.h"

// These are for the Sourcename completers.
// FIXME: Make a separate file for the completers.
#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/FileSpecList.h"

#include "lldb/Target/Language.h"

#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/CommandReturnObject.h"

using namespace lldb;
using namespace lldb_private;

// CommandObject

CommandObject::CommandObject(CommandInterpreter &interpreter,
                             llvm::StringRef name, llvm::StringRef help,
                             llvm::StringRef syntax, uint32_t flags)
    : m_interpreter(interpreter), m_cmd_name(std::string(name)),
      m_flags(flags), m_deprecated_command_override_callback(nullptr),
      m_command_override_callback(nullptr), m_command_override_baton(nullptr) {
  m_cmd_help_short = std::string(help);
  m_cmd_syntax = std::string(syntax);
}

Debugger &CommandObject::GetDebugger() { return m_interpreter.GetDebugger(); }

llvm::StringRef CommandObject::GetHelp() { return m_cmd_help_short; }

llvm::StringRef CommandObject::GetHelpLong() { return m_cmd_help_long; }

llvm::StringRef CommandObject::GetSyntax() {
  if (!m_cmd_syntax.empty())
    return m_cmd_syntax;

  StreamString syntax_str;
  syntax_str.PutCString(GetCommandName());

  if (!IsDashDashCommand() && GetOptions() != nullptr)
    syntax_str.PutCString(" <cmd-options>");

  if (!m_arguments.empty()) {
    syntax_str.PutCString(" ");

    if (!IsDashDashCommand() && WantsRawCommandString() && GetOptions() &&
        GetOptions()->NumCommandOptions())
      syntax_str.PutCString("-- ");
    GetFormattedCommandArguments(syntax_str);
  }
  m_cmd_syntax = std::string(syntax_str.GetString());

  return m_cmd_syntax;
}

llvm::StringRef CommandObject::GetCommandName() const { return m_cmd_name; }

void CommandObject::SetCommandName(llvm::StringRef name) {
  m_cmd_name = std::string(name);
}

void CommandObject::SetHelp(llvm::StringRef str) {
  m_cmd_help_short = std::string(str);
}

void CommandObject::SetHelpLong(llvm::StringRef str) {
  m_cmd_help_long = std::string(str);
}

void CommandObject::SetSyntax(llvm::StringRef str) {
  m_cmd_syntax = std::string(str);
}

Options *CommandObject::GetOptions() {
  // By default commands don't have options unless this virtual function is
  // overridden by base classes.
  return nullptr;
}

bool CommandObject::ParseOptions(Args &args, CommandReturnObject &result) {
  // See if the subclass has options?
  Options *options = GetOptions();
  if (options != nullptr) {
    Status error;

    auto exe_ctx = GetCommandInterpreter().GetExecutionContext();
    options->NotifyOptionParsingStarting(&exe_ctx);

    const bool require_validation = true;
    llvm::Expected<Args> args_or = options->Parse(
        args, &exe_ctx, GetCommandInterpreter().GetPlatform(true),
        require_validation);

    if (args_or) {
      args = std::move(*args_or);
      error = options->NotifyOptionParsingFinished(&exe_ctx);
    } else
      error = args_or.takeError();

    if (error.Success()) {
      if (options->VerifyOptions(result))
        return true;
    } else {
      const char *error_cstr = error.AsCString();
      if (error_cstr) {
        // We got an error string, lets use that
        result.AppendError(error_cstr);
      } else {
        // No error string, output the usage information into result
        options->GenerateOptionUsage(
            result.GetErrorStream(), *this,
            GetCommandInterpreter().GetDebugger().GetTerminalWidth());
      }
    }
    result.SetStatus(eReturnStatusFailed);
    return false;
  }
  return true;
}

bool CommandObject::CheckRequirements(CommandReturnObject &result) {
  // Nothing should be stored in m_exe_ctx between running commands as
  // m_exe_ctx has shared pointers to the target, process, thread and frame and
  // we don't want any CommandObject instances to keep any of these objects
  // around longer than for a single command. Every command should call
  // CommandObject::Cleanup() after it has completed.
  assert(!m_exe_ctx.GetTargetPtr());
  assert(!m_exe_ctx.GetProcessPtr());
  assert(!m_exe_ctx.GetThreadPtr());
  assert(!m_exe_ctx.GetFramePtr());

  // Lock down the interpreter's execution context prior to running the command
  // so we guarantee the selected target, process, thread and frame can't go
  // away during the execution
  m_exe_ctx = m_interpreter.GetExecutionContext();

  const uint32_t flags = GetFlags().Get();
  if (flags & (eCommandRequiresTarget | eCommandRequiresProcess |
               eCommandRequiresThread | eCommandRequiresFrame |
               eCommandTryTargetAPILock)) {

    if ((flags & eCommandRequiresTarget) && !m_exe_ctx.HasTargetScope()) {
      result.AppendError(GetInvalidTargetDescription());
      return false;
    }

    if ((flags & eCommandRequiresProcess) && !m_exe_ctx.HasProcessScope()) {
      if (!m_exe_ctx.HasTargetScope())
        result.AppendError(GetInvalidTargetDescription());
      else
        result.AppendError(GetInvalidProcessDescription());
      return false;
    }

    if ((flags & eCommandRequiresThread) && !m_exe_ctx.HasThreadScope()) {
      if (!m_exe_ctx.HasTargetScope())
        result.AppendError(GetInvalidTargetDescription());
      else if (!m_exe_ctx.HasProcessScope())
        result.AppendError(GetInvalidProcessDescription());
      else
        result.AppendError(GetInvalidThreadDescription());
      return false;
    }

    if ((flags & eCommandRequiresFrame) && !m_exe_ctx.HasFrameScope()) {
      if (!m_exe_ctx.HasTargetScope())
        result.AppendError(GetInvalidTargetDescription());
      else if (!m_exe_ctx.HasProcessScope())
        result.AppendError(GetInvalidProcessDescription());
      else if (!m_exe_ctx.HasThreadScope())
        result.AppendError(GetInvalidThreadDescription());
      else
        result.AppendError(GetInvalidFrameDescription());
      return false;
    }

    if ((flags & eCommandRequiresRegContext) &&
        (m_exe_ctx.GetRegisterContext() == nullptr)) {
      result.AppendError(GetInvalidRegContextDescription());
      return false;
    }

    if (flags & eCommandTryTargetAPILock) {
      Target *target = m_exe_ctx.GetTargetPtr();
      if (target)
        m_api_locker =
            std::unique_lock<std::recursive_mutex>(target->GetAPIMutex());
    }
  }

  if (GetFlags().AnySet(eCommandProcessMustBeLaunched |
                        eCommandProcessMustBePaused)) {
    Process *process = m_interpreter.GetExecutionContext().GetProcessPtr();
    if (process == nullptr) {
      // A process that is not running is considered paused.
      if (GetFlags().Test(eCommandProcessMustBeLaunched)) {
        result.AppendError("Process must exist.");
        return false;
      }
    } else {
      StateType state = process->GetState();
      switch (state) {
      case eStateInvalid:
      case eStateSuspended:
      case eStateCrashed:
      case eStateStopped:
        break;

      case eStateConnected:
      case eStateAttaching:
      case eStateLaunching:
      case eStateDetached:
      case eStateExited:
      case eStateUnloaded:
        if (GetFlags().Test(eCommandProcessMustBeLaunched)) {
          result.AppendError("Process must be launched.");
          return false;
        }
        break;

      case eStateRunning:
      case eStateStepping:
        if (GetFlags().Test(eCommandProcessMustBePaused)) {
          result.AppendError("Process is running.  Use 'process interrupt' to "
                             "pause execution.");
          return false;
        }
      }
    }
  }

  if (GetFlags().Test(eCommandProcessMustBeTraced)) {
    Target *target = m_exe_ctx.GetTargetPtr();
    if (target && !target->GetTrace()) {
      result.AppendError("Process is not being traced.");
      return false;
    }
  }

  return true;
}

void CommandObject::Cleanup() {
  m_exe_ctx.Clear();
  if (m_api_locker.owns_lock())
    m_api_locker.unlock();
}

void CommandObject::HandleCompletion(CompletionRequest &request) {

  m_exe_ctx = m_interpreter.GetExecutionContext();
  auto reset_ctx = llvm::make_scope_exit([this]() { Cleanup(); });

  // Default implementation of WantsCompletion() is !WantsRawCommandString().
  // Subclasses who want raw command string but desire, for example, argument
  // completion should override WantsCompletion() to return true, instead.
  if (WantsRawCommandString() && !WantsCompletion()) {
    // FIXME: Abstract telling the completion to insert the completion
    // character.
    return;
  } else {
    // Can we do anything generic with the options?
    Options *cur_options = GetOptions();
    CommandReturnObject result(m_interpreter.GetDebugger().GetUseColor());
    OptionElementVector opt_element_vector;

    if (cur_options != nullptr) {
      opt_element_vector = cur_options->ParseForCompletion(
          request.GetParsedLine(), request.GetCursorIndex());

      bool handled_by_options = cur_options->HandleOptionCompletion(
          request, opt_element_vector, GetCommandInterpreter());
      if (handled_by_options)
        return;
    }

    // If we got here, the last word is not an option or an option argument.
    HandleArgumentCompletion(request, opt_element_vector);
  }
}

void CommandObject::HandleArgumentCompletion(
    CompletionRequest &request, OptionElementVector &opt_element_vector) {
  size_t num_arg_entries = GetNumArgumentEntries();
  if (num_arg_entries != 1)
    return;

  CommandArgumentEntry *entry_ptr = GetArgumentEntryAtIndex(0);
  if (!entry_ptr) {
    assert(entry_ptr && "We said there was one entry, but there wasn't.");
    return; // Not worth crashing if asserts are off...
  }
  
  CommandArgumentEntry &entry = *entry_ptr;
  // For now, we only handle the simple case of one homogenous argument type.
  if (entry.size() != 1)
    return;

  // Look up the completion type, and if it has one, invoke it:
  const CommandObject::ArgumentTableEntry *arg_entry =
      FindArgumentDataByType(entry[0].arg_type);
  const ArgumentRepetitionType repeat = entry[0].arg_repetition;

  if (arg_entry == nullptr || arg_entry->completion_type == lldb::eNoCompletion)
    return;

  // FIXME: This should be handled higher in the Command Parser.
  // Check the case where this command only takes one argument, and don't do
  // the completion if we aren't on the first entry:
  if (repeat == eArgRepeatPlain && request.GetCursorIndex() != 0)
    return;

  lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
      GetCommandInterpreter(), arg_entry->completion_type, request, nullptr);

}


bool CommandObject::HelpTextContainsWord(llvm::StringRef search_word,
                                         bool search_short_help,
                                         bool search_long_help,
                                         bool search_syntax,
                                         bool search_options) {
  std::string options_usage_help;

  bool found_word = false;

  llvm::StringRef short_help = GetHelp();
  llvm::StringRef long_help = GetHelpLong();
  llvm::StringRef syntax_help = GetSyntax();

  if (search_short_help && short_help.contains_insensitive(search_word))
    found_word = true;
  else if (search_long_help && long_help.contains_insensitive(search_word))
    found_word = true;
  else if (search_syntax && syntax_help.contains_insensitive(search_word))
    found_word = true;

  if (!found_word && search_options && GetOptions() != nullptr) {
    StreamString usage_help;
    GetOptions()->GenerateOptionUsage(
        usage_help, *this,
        GetCommandInterpreter().GetDebugger().GetTerminalWidth());
    if (!usage_help.Empty()) {
      llvm::StringRef usage_text = usage_help.GetString();
      if (usage_text.contains_insensitive(search_word))
        found_word = true;
    }
  }

  return found_word;
}

bool CommandObject::ParseOptionsAndNotify(Args &args,
                                          CommandReturnObject &result,
                                          OptionGroupOptions &group_options,
                                          ExecutionContext &exe_ctx) {
  if (!ParseOptions(args, result))
    return false;

  Status error(group_options.NotifyOptionParsingFinished(&exe_ctx));
  if (error.Fail()) {
    result.AppendError(error.AsCString());
    return false;
  }
  return true;
}

void CommandObject::AddSimpleArgumentList(
    CommandArgumentType arg_type, ArgumentRepetitionType repetition_type) {

  CommandArgumentEntry arg_entry;
  CommandArgumentData simple_arg;

  // Define the first (and only) variant of this arg.
  simple_arg.arg_type = arg_type;
  simple_arg.arg_repetition = repetition_type;

  // There is only one variant this argument could be; put it into the argument
  // entry.
  arg_entry.push_back(simple_arg);

  // Push the data for the first argument into the m_arguments vector.
  m_arguments.push_back(arg_entry);
}

int CommandObject::GetNumArgumentEntries() { return m_arguments.size(); }

CommandObject::CommandArgumentEntry *
CommandObject::GetArgumentEntryAtIndex(int idx) {
  if (static_cast<size_t>(idx) < m_arguments.size())
    return &(m_arguments[idx]);

  return nullptr;
}

const CommandObject::ArgumentTableEntry *
CommandObject::FindArgumentDataByType(CommandArgumentType arg_type) {
  for (int i = 0; i < eArgTypeLastArg; ++i)
    if (g_argument_table[i].arg_type == arg_type)
      return &(g_argument_table[i]);

  return nullptr;
}

void CommandObject::GetArgumentHelp(Stream &str, CommandArgumentType arg_type,
                                    CommandInterpreter &interpreter) {
  const ArgumentTableEntry *entry = &(g_argument_table[arg_type]);

  // The table is *supposed* to be kept in arg_type order, but someone *could*
  // have messed it up...

  if (entry->arg_type != arg_type)
    entry = CommandObject::FindArgumentDataByType(arg_type);

  if (!entry)
    return;

  StreamString name_str;
  name_str.Printf("<%s>", entry->arg_name);

  if (entry->help_function) {
    llvm::StringRef help_text = entry->help_function();
    if (!entry->help_function.self_formatting) {
      interpreter.OutputFormattedHelpText(str, name_str.GetString(), "--",
                                          help_text, name_str.GetSize());
    } else {
      interpreter.OutputHelpText(str, name_str.GetString(), "--", help_text,
                                 name_str.GetSize());
    }
  } else {
    interpreter.OutputFormattedHelpText(str, name_str.GetString(), "--",
                                        entry->help_text, name_str.GetSize());

    // Print enum values and their description if any.
    OptionEnumValues enum_values = g_argument_table[arg_type].enum_values;
    if (!enum_values.empty()) {
      str.EOL();
      size_t longest = 0;
      for (const OptionEnumValueElement &element : enum_values)
        longest =
            std::max(longest, llvm::StringRef(element.string_value).size());
      str.IndentMore(5);
      for (const OptionEnumValueElement &element : enum_values) {
        str.Indent();
        interpreter.OutputHelpText(str, element.string_value, ":",
                                   element.usage, longest);
      }
      str.IndentLess(5);
      str.EOL();
    }
  }
}

const char *CommandObject::GetArgumentName(CommandArgumentType arg_type) {
  const ArgumentTableEntry *entry = &(g_argument_table[arg_type]);

  // The table is *supposed* to be kept in arg_type order, but someone *could*
  // have messed it up...

  if (entry->arg_type != arg_type)
    entry = CommandObject::FindArgumentDataByType(arg_type);

  if (entry)
    return entry->arg_name;

  return nullptr;
}

bool CommandObject::IsPairType(ArgumentRepetitionType arg_repeat_type) {
  return (arg_repeat_type == eArgRepeatPairPlain) ||
         (arg_repeat_type == eArgRepeatPairOptional) ||
         (arg_repeat_type == eArgRepeatPairPlus) ||
         (arg_repeat_type == eArgRepeatPairStar) ||
         (arg_repeat_type == eArgRepeatPairRange) ||
         (arg_repeat_type == eArgRepeatPairRangeOptional);
}

std::optional<ArgumentRepetitionType> 
CommandObject::ArgRepetitionFromString(llvm::StringRef string) {
  return llvm::StringSwitch<ArgumentRepetitionType>(string)
  .Case("plain", eArgRepeatPlain)  
  .Case("optional", eArgRepeatOptional)
  .Case("plus", eArgRepeatPlus)
  .Case("star", eArgRepeatStar) 
  .Case("range", eArgRepeatRange)
  .Case("pair-plain", eArgRepeatPairPlain)
  .Case("pair-optional", eArgRepeatPairOptional)
  .Case("pair-plus", eArgRepeatPairPlus)
  .Case("pair-star", eArgRepeatPairStar)
  .Case("pair-range", eArgRepeatPairRange)
  .Case("pair-range-optional", eArgRepeatPairRangeOptional)
  .Default({});
}

static CommandObject::CommandArgumentEntry
OptSetFiltered(uint32_t opt_set_mask,
               CommandObject::CommandArgumentEntry &cmd_arg_entry) {
  CommandObject::CommandArgumentEntry ret_val;
  for (unsigned i = 0; i < cmd_arg_entry.size(); ++i)
    if (opt_set_mask & cmd_arg_entry[i].arg_opt_set_association)
      ret_val.push_back(cmd_arg_entry[i]);
  return ret_val;
}

// Default parameter value of opt_set_mask is LLDB_OPT_SET_ALL, which means
// take all the argument data into account.  On rare cases where some argument
// sticks with certain option sets, this function returns the option set
// filtered args.
void CommandObject::GetFormattedCommandArguments(Stream &str,
                                                 uint32_t opt_set_mask) {
  int num_args = m_arguments.size();
  for (int i = 0; i < num_args; ++i) {
    if (i > 0)
      str.Printf(" ");
    CommandArgumentEntry arg_entry =
        opt_set_mask == LLDB_OPT_SET_ALL
            ? m_arguments[i]
            : OptSetFiltered(opt_set_mask, m_arguments[i]);
    // This argument is not associated with the current option set, so skip it.
    if (arg_entry.empty())
      continue;
    int num_alternatives = arg_entry.size();

    if ((num_alternatives == 2) && IsPairType(arg_entry[0].arg_repetition)) {
      const char *first_name = GetArgumentName(arg_entry[0].arg_type);
      const char *second_name = GetArgumentName(arg_entry[1].arg_type);
      switch (arg_entry[0].arg_repetition) {
      case eArgRepeatPairPlain:
        str.Printf("<%s> <%s>", first_name, second_name);
        break;
      case eArgRepeatPairOptional:
        str.Printf("[<%s> <%s>]", first_name, second_name);
        break;
      case eArgRepeatPairPlus:
        str.Printf("<%s> <%s> [<%s> <%s> [...]]", first_name, second_name,
                   first_name, second_name);
        break;
      case eArgRepeatPairStar:
        str.Printf("[<%s> <%s> [<%s> <%s> [...]]]", first_name, second_name,
                   first_name, second_name);
        break;
      case eArgRepeatPairRange:
        str.Printf("<%s_1> <%s_1> ... <%s_n> <%s_n>", first_name, second_name,
                   first_name, second_name);
        break;
      case eArgRepeatPairRangeOptional:
        str.Printf("[<%s_1> <%s_1> ... <%s_n> <%s_n>]", first_name, second_name,
                   first_name, second_name);
        break;
      // Explicitly test for all the rest of the cases, so if new types get
      // added we will notice the missing case statement(s).
      case eArgRepeatPlain:
      case eArgRepeatOptional:
      case eArgRepeatPlus:
      case eArgRepeatStar:
      case eArgRepeatRange:
        // These should not be reached, as they should fail the IsPairType test
        // above.
        break;
      }
    } else {
      StreamString names;
      for (int j = 0; j < num_alternatives; ++j) {
        if (j > 0)
          names.Printf(" | ");
        names.Printf("%s", GetArgumentName(arg_entry[j].arg_type));
      }

      std::string name_str = std::string(names.GetString());
      switch (arg_entry[0].arg_repetition) {
      case eArgRepeatPlain:
        str.Printf("<%s>", name_str.c_str());
        break;
      case eArgRepeatPlus:
        str.Printf("<%s> [<%s> [...]]", name_str.c_str(), name_str.c_str());
        break;
      case eArgRepeatStar:
        str.Printf("[<%s> [<%s> [...]]]", name_str.c_str(), name_str.c_str());
        break;
      case eArgRepeatOptional:
        str.Printf("[<%s>]", name_str.c_str());
        break;
      case eArgRepeatRange:
        str.Printf("<%s_1> .. <%s_n>", name_str.c_str(), name_str.c_str());
        break;
      // Explicitly test for all the rest of the cases, so if new types get
      // added we will notice the missing case statement(s).
      case eArgRepeatPairPlain:
      case eArgRepeatPairOptional:
      case eArgRepeatPairPlus:
      case eArgRepeatPairStar:
      case eArgRepeatPairRange:
      case eArgRepeatPairRangeOptional:
        // These should not be hit, as they should pass the IsPairType test
        // above, and control should have gone into the other branch of the if
        // statement.
        break;
      }
    }
  }
}

CommandArgumentType
CommandObject::LookupArgumentName(llvm::StringRef arg_name) {
  CommandArgumentType return_type = eArgTypeLastArg;

  arg_name = arg_name.ltrim('<').rtrim('>');

  for (int i = 0; i < eArgTypeLastArg; ++i)
    if (arg_name == g_argument_table[i].arg_name)
      return_type = g_argument_table[i].arg_type;

  return return_type;
}

void CommandObject::FormatLongHelpText(Stream &output_strm,
                                       llvm::StringRef long_help) {
  CommandInterpreter &interpreter = GetCommandInterpreter();
  std::stringstream lineStream{std::string(long_help)};
  std::string line;
  while (std::getline(lineStream, line)) {
    if (line.empty()) {
      output_strm << "\n";
      continue;
    }
    size_t result = line.find_first_not_of(" \t");
    if (result == std::string::npos) {
      result = 0;
    }
    std::string whitespace_prefix = line.substr(0, result);
    std::string remainder = line.substr(result);
    interpreter.OutputFormattedHelpText(output_strm, whitespace_prefix,
                                        remainder);
  }
}

void CommandObject::GenerateHelpText(CommandReturnObject &result) {
  GenerateHelpText(result.GetOutputStream());

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
}

void CommandObject::GenerateHelpText(Stream &output_strm) {
  CommandInterpreter &interpreter = GetCommandInterpreter();
  std::string help_text(GetHelp());
  if (WantsRawCommandString()) {
    help_text.append("  Expects 'raw' input (see 'help raw-input'.)");
  }
  interpreter.OutputFormattedHelpText(output_strm, "", help_text);
  output_strm << "\nSyntax: " << GetSyntax() << "\n";
  Options *options = GetOptions();
  if (options != nullptr) {
    options->GenerateOptionUsage(
        output_strm, *this,
        GetCommandInterpreter().GetDebugger().GetTerminalWidth());
  }
  llvm::StringRef long_help = GetHelpLong();
  if (!long_help.empty()) {
    FormatLongHelpText(output_strm, long_help);
  }
  if (!IsDashDashCommand() && options && options->NumCommandOptions() > 0) {
    if (WantsRawCommandString() && !WantsCompletion()) {
      // Emit the message about using ' -- ' between the end of the command
      // options and the raw input conditionally, i.e., only if the command
      // object does not want completion.
      interpreter.OutputFormattedHelpText(
          output_strm, "", "",
          "\nImportant Note: Because this command takes 'raw' input, if you "
          "use any command options"
          " you must use ' -- ' between the end of the command options and the "
          "beginning of the raw input.",
          1);
    } else if (GetNumArgumentEntries() > 0) {
      // Also emit a warning about using "--" in case you are using a command
      // that takes options and arguments.
      interpreter.OutputFormattedHelpText(
          output_strm, "", "",
          "\nThis command takes options and free-form arguments.  If your "
          "arguments resemble"
          " option specifiers (i.e., they start with a - or --), you must use "
          "' -- ' between"
          " the end of the command options and the beginning of the arguments.",
          1);
    }
  }
}

void CommandObject::AddIDsArgumentData(CommandObject::IDType type) {
  CommandArgumentEntry arg;
  CommandArgumentData id_arg;
  CommandArgumentData id_range_arg;

  // Create the first variant for the first (and only) argument for this
  // command.
  switch (type) {
  case eBreakpointArgs:
    id_arg.arg_type = eArgTypeBreakpointID;
    id_range_arg.arg_type = eArgTypeBreakpointIDRange;
    break;
  case eWatchpointArgs:
    id_arg.arg_type = eArgTypeWatchpointID;
    id_range_arg.arg_type = eArgTypeWatchpointIDRange;
    break;
  }
  id_arg.arg_repetition = eArgRepeatOptional;
  id_range_arg.arg_repetition = eArgRepeatOptional;

  // The first (and only) argument for this command could be either an id or an
  // id_range. Push both variants into the entry for the first argument for
  // this command.
  arg.push_back(id_arg);
  arg.push_back(id_range_arg);
  m_arguments.push_back(arg);
}

const char *CommandObject::GetArgumentTypeAsCString(
    const lldb::CommandArgumentType arg_type) {
  assert(arg_type < eArgTypeLastArg &&
         "Invalid argument type passed to GetArgumentTypeAsCString");
  return g_argument_table[arg_type].arg_name;
}

const char *CommandObject::GetArgumentDescriptionAsCString(
    const lldb::CommandArgumentType arg_type) {
  assert(arg_type < eArgTypeLastArg &&
         "Invalid argument type passed to GetArgumentDescriptionAsCString");
  return g_argument_table[arg_type].help_text;
}

Target &CommandObject::GetDummyTarget() {
  return m_interpreter.GetDebugger().GetDummyTarget();
}

Target &CommandObject::GetSelectedOrDummyTarget(bool prefer_dummy) {
  return m_interpreter.GetDebugger().GetSelectedOrDummyTarget(prefer_dummy);
}

Target &CommandObject::GetSelectedTarget() {
  assert(m_flags.AnySet(eCommandRequiresTarget | eCommandProcessMustBePaused |
                        eCommandProcessMustBeLaunched | eCommandRequiresFrame |
                        eCommandRequiresThread | eCommandRequiresProcess |
                        eCommandRequiresRegContext) &&
         "GetSelectedTarget called from object that may have no target");
  return *m_interpreter.GetDebugger().GetSelectedTarget();
}

Thread *CommandObject::GetDefaultThread() {
  Thread *thread_to_use = m_exe_ctx.GetThreadPtr();
  if (thread_to_use)
    return thread_to_use;

  Process *process = m_exe_ctx.GetProcessPtr();
  if (!process) {
    Target *target = m_exe_ctx.GetTargetPtr();
    if (!target) {
      target = m_interpreter.GetDebugger().GetSelectedTarget().get();
    }
    if (target)
      process = target->GetProcessSP().get();
  }

  if (process)
    return process->GetThreadList().GetSelectedThread().get();
  else
    return nullptr;
}

void CommandObjectParsed::Execute(const char *args_string,
                                  CommandReturnObject &result) {
  bool handled = false;
  Args cmd_args(args_string);
  if (HasOverrideCallback()) {
    Args full_args(GetCommandName());
    full_args.AppendArguments(cmd_args);
    handled =
        InvokeOverrideCallback(full_args.GetConstArgumentVector(), result);
  }
  if (!handled) {
    for (auto entry : llvm::enumerate(cmd_args.entries())) {
      const Args::ArgEntry &value = entry.value();
      if (!value.ref().empty() && value.GetQuoteChar() == '`') {
        // We have to put the backtick back in place for PreprocessCommand.
        std::string opt_string = value.c_str();
        Status error;
        error = m_interpreter.PreprocessToken(opt_string);
        if (error.Success())
          cmd_args.ReplaceArgumentAtIndex(entry.index(), opt_string);
      }
    }

    if (CheckRequirements(result)) {
      if (ParseOptions(cmd_args, result)) {
        // Call the command-specific version of 'Execute', passing it the
        // already processed arguments.
        if (cmd_args.GetArgumentCount() != 0 && m_arguments.empty()) {
          result.AppendErrorWithFormatv("'{0}' doesn't take any arguments.",
                                        GetCommandName());
          Cleanup();
          return;
        }
        m_interpreter.IncreaseCommandUsage(*this);
        DoExecute(cmd_args, result);
      }
    }

    Cleanup();
  }
}

void CommandObjectRaw::Execute(const char *args_string,
                               CommandReturnObject &result) {
  bool handled = false;
  if (HasOverrideCallback()) {
    std::string full_command(GetCommandName());
    full_command += ' ';
    full_command += args_string;
    const char *argv[2] = {nullptr, nullptr};
    argv[0] = full_command.c_str();
    handled = InvokeOverrideCallback(argv, result);
  }
  if (!handled) {
    if (CheckRequirements(result))
      DoExecute(args_string, result);

    Cleanup();
  }
}
