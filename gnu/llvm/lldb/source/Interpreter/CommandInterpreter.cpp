//===-- CommandInterpreter.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <chrono>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Commands/CommandObjectApropos.h"
#include "Commands/CommandObjectBreakpoint.h"
#include "Commands/CommandObjectCommands.h"
#include "Commands/CommandObjectDWIMPrint.h"
#include "Commands/CommandObjectDiagnostics.h"
#include "Commands/CommandObjectDisassemble.h"
#include "Commands/CommandObjectExpression.h"
#include "Commands/CommandObjectFrame.h"
#include "Commands/CommandObjectGUI.h"
#include "Commands/CommandObjectHelp.h"
#include "Commands/CommandObjectLanguage.h"
#include "Commands/CommandObjectLog.h"
#include "Commands/CommandObjectMemory.h"
#include "Commands/CommandObjectPlatform.h"
#include "Commands/CommandObjectPlugin.h"
#include "Commands/CommandObjectProcess.h"
#include "Commands/CommandObjectQuit.h"
#include "Commands/CommandObjectRegexCommand.h"
#include "Commands/CommandObjectRegister.h"
#include "Commands/CommandObjectScripting.h"
#include "Commands/CommandObjectSession.h"
#include "Commands/CommandObjectSettings.h"
#include "Commands/CommandObjectSource.h"
#include "Commands/CommandObjectStats.h"
#include "Commands/CommandObjectTarget.h"
#include "Commands/CommandObjectThread.h"
#include "Commands/CommandObjectTrace.h"
#include "Commands/CommandObjectType.h"
#include "Commands/CommandObjectVersion.h"
#include "Commands/CommandObjectWatchpoint.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Utility/ErrorMessages.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/Timer.h"

#include "lldb/Host/Config.h"
#if LLDB_ENABLE_LIBEDIT
#include "lldb/Host/Editline.h"
#endif
#include "lldb/Host/File.h"
#include "lldb/Host/FileCache.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"

#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Utility/Args.h"

#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/TargetList.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/UnixSignals.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/ScopedPrinter.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

using namespace lldb;
using namespace lldb_private;

static const char *k_white_space = " \t\v";

static constexpr const char *InitFileWarning =
    "There is a .lldbinit file in the current directory which is not being "
    "read.\n"
    "To silence this warning without sourcing in the local .lldbinit,\n"
    "add the following to the lldbinit file in your home directory:\n"
    "    settings set target.load-cwd-lldbinit false\n"
    "To allow lldb to source .lldbinit files in the current working "
    "directory,\n"
    "set the value of this variable to true.  Only do so if you understand "
    "and\n"
    "accept the security risk.";

const char *CommandInterpreter::g_no_argument = "<no-argument>";
const char *CommandInterpreter::g_need_argument = "<need-argument>";
const char *CommandInterpreter::g_argument = "<argument>";


#define LLDB_PROPERTIES_interpreter
#include "InterpreterProperties.inc"

enum {
#define LLDB_PROPERTIES_interpreter
#include "InterpreterPropertiesEnum.inc"
};

llvm::StringRef CommandInterpreter::GetStaticBroadcasterClass() {
  static constexpr llvm::StringLiteral class_name("lldb.commandInterpreter");
  return class_name;
}

CommandInterpreter::CommandInterpreter(Debugger &debugger,
                                       bool synchronous_execution)
    : Broadcaster(debugger.GetBroadcasterManager(),
                  CommandInterpreter::GetStaticBroadcasterClass().str()),
      Properties(
          OptionValuePropertiesSP(new OptionValueProperties("interpreter"))),
      IOHandlerDelegate(IOHandlerDelegate::Completion::LLDBCommand),
      m_debugger(debugger), m_synchronous_execution(true),
      m_skip_lldbinit_files(false), m_skip_app_init_files(false),
      m_comment_char('#'), m_batch_command_mode(false),
      m_truncation_warning(eNoOmission), m_max_depth_warning(eNoOmission),
      m_command_source_depth(0) {
  SetEventName(eBroadcastBitThreadShouldExit, "thread-should-exit");
  SetEventName(eBroadcastBitResetPrompt, "reset-prompt");
  SetEventName(eBroadcastBitQuitCommandReceived, "quit");
  SetSynchronous(synchronous_execution);
  CheckInWithManager();
  m_collection_sp->Initialize(g_interpreter_properties);
}

bool CommandInterpreter::GetExpandRegexAliases() const {
  const uint32_t idx = ePropertyExpandRegexAliases;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

bool CommandInterpreter::GetPromptOnQuit() const {
  const uint32_t idx = ePropertyPromptOnQuit;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

void CommandInterpreter::SetPromptOnQuit(bool enable) {
  const uint32_t idx = ePropertyPromptOnQuit;
  SetPropertyAtIndex(idx, enable);
}

bool CommandInterpreter::GetSaveTranscript() const {
  const uint32_t idx = ePropertySaveTranscript;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

void CommandInterpreter::SetSaveTranscript(bool enable) {
  const uint32_t idx = ePropertySaveTranscript;
  SetPropertyAtIndex(idx, enable);
}

bool CommandInterpreter::GetSaveSessionOnQuit() const {
  const uint32_t idx = ePropertySaveSessionOnQuit;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

void CommandInterpreter::SetSaveSessionOnQuit(bool enable) {
  const uint32_t idx = ePropertySaveSessionOnQuit;
  SetPropertyAtIndex(idx, enable);
}

bool CommandInterpreter::GetOpenTranscriptInEditor() const {
  const uint32_t idx = ePropertyOpenTranscriptInEditor;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

void CommandInterpreter::SetOpenTranscriptInEditor(bool enable) {
  const uint32_t idx = ePropertyOpenTranscriptInEditor;
  SetPropertyAtIndex(idx, enable);
}

FileSpec CommandInterpreter::GetSaveSessionDirectory() const {
  const uint32_t idx = ePropertySaveSessionDirectory;
  return GetPropertyAtIndexAs<FileSpec>(idx, {});
}

void CommandInterpreter::SetSaveSessionDirectory(llvm::StringRef path) {
  const uint32_t idx = ePropertySaveSessionDirectory;
  SetPropertyAtIndex(idx, path);
}

bool CommandInterpreter::GetEchoCommands() const {
  const uint32_t idx = ePropertyEchoCommands;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

void CommandInterpreter::SetEchoCommands(bool enable) {
  const uint32_t idx = ePropertyEchoCommands;
  SetPropertyAtIndex(idx, enable);
}

bool CommandInterpreter::GetEchoCommentCommands() const {
  const uint32_t idx = ePropertyEchoCommentCommands;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

void CommandInterpreter::SetEchoCommentCommands(bool enable) {
  const uint32_t idx = ePropertyEchoCommentCommands;
  SetPropertyAtIndex(idx, enable);
}

void CommandInterpreter::AllowExitCodeOnQuit(bool allow) {
  m_allow_exit_code = allow;
  if (!allow)
    m_quit_exit_code.reset();
}

bool CommandInterpreter::SetQuitExitCode(int exit_code) {
  if (!m_allow_exit_code)
    return false;
  m_quit_exit_code = exit_code;
  return true;
}

int CommandInterpreter::GetQuitExitCode(bool &exited) const {
  exited = m_quit_exit_code.has_value();
  if (exited)
    return *m_quit_exit_code;
  return 0;
}

void CommandInterpreter::ResolveCommand(const char *command_line,
                                        CommandReturnObject &result) {
  std::string command = command_line;
  if (ResolveCommandImpl(command, result) != nullptr) {
    result.AppendMessageWithFormat("%s", command.c_str());
    result.SetStatus(eReturnStatusSuccessFinishResult);
  }
}

bool CommandInterpreter::GetStopCmdSourceOnError() const {
  const uint32_t idx = ePropertyStopCmdSourceOnError;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

bool CommandInterpreter::GetSpaceReplPrompts() const {
  const uint32_t idx = ePropertySpaceReplPrompts;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

bool CommandInterpreter::GetRepeatPreviousCommand() const {
  const uint32_t idx = ePropertyRepeatPreviousCommand;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

bool CommandInterpreter::GetRequireCommandOverwrite() const {
  const uint32_t idx = ePropertyRequireCommandOverwrite;
  return GetPropertyAtIndexAs<bool>(
      idx, g_interpreter_properties[idx].default_uint_value != 0);
}

void CommandInterpreter::Initialize() {
  LLDB_SCOPED_TIMER();

  CommandReturnObject result(m_debugger.GetUseColor());

  LoadCommandDictionary();

  // An alias arguments vector to reuse - reset it before use...
  OptionArgVectorSP alias_arguments_vector_sp(new OptionArgVector);

  // Set up some initial aliases.
  CommandObjectSP cmd_obj_sp = GetCommandSPExact("quit");
  if (cmd_obj_sp) {
    AddAlias("q", cmd_obj_sp);
    AddAlias("exit", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("_regexp-attach");
  if (cmd_obj_sp)
    AddAlias("attach", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());

  cmd_obj_sp = GetCommandSPExact("process detach");
  if (cmd_obj_sp) {
    AddAlias("detach", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("process continue");
  if (cmd_obj_sp) {
    AddAlias("c", cmd_obj_sp);
    AddAlias("continue", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("_regexp-break");
  if (cmd_obj_sp)
    AddAlias("b", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());

  cmd_obj_sp = GetCommandSPExact("_regexp-tbreak");
  if (cmd_obj_sp)
    AddAlias("tbreak", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());

  cmd_obj_sp = GetCommandSPExact("thread step-inst");
  if (cmd_obj_sp) {
    AddAlias("stepi", cmd_obj_sp);
    AddAlias("si", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("thread step-inst-over");
  if (cmd_obj_sp) {
    AddAlias("nexti", cmd_obj_sp);
    AddAlias("ni", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("thread step-in");
  if (cmd_obj_sp) {
    AddAlias("s", cmd_obj_sp);
    AddAlias("step", cmd_obj_sp);
    CommandAlias *sif_alias = AddAlias(
        "sif", cmd_obj_sp, "--end-linenumber block --step-in-target %1");
    if (sif_alias) {
      sif_alias->SetHelp("Step through the current block, stopping if you step "
                         "directly into a function whose name matches the "
                         "TargetFunctionName.");
      sif_alias->SetSyntax("sif <TargetFunctionName>");
    }
  }

  cmd_obj_sp = GetCommandSPExact("thread step-over");
  if (cmd_obj_sp) {
    AddAlias("n", cmd_obj_sp);
    AddAlias("next", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("thread step-out");
  if (cmd_obj_sp) {
    AddAlias("finish", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("frame select");
  if (cmd_obj_sp) {
    AddAlias("f", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("thread select");
  if (cmd_obj_sp) {
    AddAlias("t", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("_regexp-jump");
  if (cmd_obj_sp) {
    AddAlias("j", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());
    AddAlias("jump", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());
  }

  cmd_obj_sp = GetCommandSPExact("_regexp-list");
  if (cmd_obj_sp) {
    AddAlias("l", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());
    AddAlias("list", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());
  }

  cmd_obj_sp = GetCommandSPExact("_regexp-env");
  if (cmd_obj_sp)
    AddAlias("env", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());

  cmd_obj_sp = GetCommandSPExact("memory read");
  if (cmd_obj_sp)
    AddAlias("x", cmd_obj_sp);

  cmd_obj_sp = GetCommandSPExact("_regexp-up");
  if (cmd_obj_sp)
    AddAlias("up", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());

  cmd_obj_sp = GetCommandSPExact("_regexp-down");
  if (cmd_obj_sp)
    AddAlias("down", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());

  cmd_obj_sp = GetCommandSPExact("_regexp-display");
  if (cmd_obj_sp)
    AddAlias("display", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());

  cmd_obj_sp = GetCommandSPExact("disassemble");
  if (cmd_obj_sp)
    AddAlias("dis", cmd_obj_sp);

  cmd_obj_sp = GetCommandSPExact("disassemble");
  if (cmd_obj_sp)
    AddAlias("di", cmd_obj_sp);

  cmd_obj_sp = GetCommandSPExact("_regexp-undisplay");
  if (cmd_obj_sp)
    AddAlias("undisplay", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());

  cmd_obj_sp = GetCommandSPExact("_regexp-bt");
  if (cmd_obj_sp)
    AddAlias("bt", cmd_obj_sp)->SetSyntax(cmd_obj_sp->GetSyntax());

  cmd_obj_sp = GetCommandSPExact("target create");
  if (cmd_obj_sp)
    AddAlias("file", cmd_obj_sp);

  cmd_obj_sp = GetCommandSPExact("target modules");
  if (cmd_obj_sp)
    AddAlias("image", cmd_obj_sp);

  alias_arguments_vector_sp = std::make_shared<OptionArgVector>();

  cmd_obj_sp = GetCommandSPExact("dwim-print");
  if (cmd_obj_sp) {
    AddAlias("p", cmd_obj_sp, "--")->SetHelpLong("");
    AddAlias("print", cmd_obj_sp, "--")->SetHelpLong("");
    if (auto *po = AddAlias("po", cmd_obj_sp, "-O --")) {
      po->SetHelp("Evaluate an expression on the current thread.  Displays any "
                  "returned value with formatting "
                  "controlled by the type's author.");
      po->SetHelpLong("");
    }
  }

  cmd_obj_sp = GetCommandSPExact("expression");
  if (cmd_obj_sp) {
    AddAlias("call", cmd_obj_sp, "--")->SetHelpLong("");
    CommandAlias *parray_alias =
        AddAlias("parray", cmd_obj_sp, "--element-count %1 --");
    if (parray_alias) {
        parray_alias->SetHelp
          ("parray <COUNT> <EXPRESSION> -- lldb will evaluate EXPRESSION "
           "to get a typed-pointer-to-an-array in memory, and will display "
           "COUNT elements of that type from the array.");
        parray_alias->SetHelpLong("");
    }
    CommandAlias *poarray_alias = AddAlias("poarray", cmd_obj_sp,
             "--object-description --element-count %1 --");
    if (poarray_alias) {
      poarray_alias->SetHelp("poarray <COUNT> <EXPRESSION> -- lldb will "
          "evaluate EXPRESSION to get the address of an array of COUNT "
          "objects in memory, and will call po on them.");
      poarray_alias->SetHelpLong("");
    }
  }

  cmd_obj_sp = GetCommandSPExact("platform shell");
  if (cmd_obj_sp) {
    CommandAlias *shell_alias = AddAlias("shell", cmd_obj_sp, " --host --");
    if (shell_alias) {
      shell_alias->SetHelp("Run a shell command on the host.");
      shell_alias->SetHelpLong("");
      shell_alias->SetSyntax("shell <shell-command>");
    }
  }

  cmd_obj_sp = GetCommandSPExact("process kill");
  if (cmd_obj_sp) {
    AddAlias("kill", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("process launch");
  if (cmd_obj_sp) {
    alias_arguments_vector_sp = std::make_shared<OptionArgVector>();
#if defined(__APPLE__)
#if TARGET_OS_IPHONE
    AddAlias("r", cmd_obj_sp, "--");
    AddAlias("run", cmd_obj_sp, "--");
#else
    AddAlias("r", cmd_obj_sp, "--shell-expand-args true --");
    AddAlias("run", cmd_obj_sp, "--shell-expand-args true --");
#endif
#else
    StreamString defaultshell;
    defaultshell.Printf("--shell=%s --",
                        HostInfo::GetDefaultShell().GetPath().c_str());
    AddAlias("r", cmd_obj_sp, defaultshell.GetString());
    AddAlias("run", cmd_obj_sp, defaultshell.GetString());
#endif
  }

  cmd_obj_sp = GetCommandSPExact("target symbols add");
  if (cmd_obj_sp) {
    AddAlias("add-dsym", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("breakpoint set");
  if (cmd_obj_sp) {
    AddAlias("rbreak", cmd_obj_sp, "--func-regex %1");
  }

  cmd_obj_sp = GetCommandSPExact("frame variable");
  if (cmd_obj_sp) {
    AddAlias("v", cmd_obj_sp);
    AddAlias("var", cmd_obj_sp);
    AddAlias("vo", cmd_obj_sp, "--object-description");
  }

  cmd_obj_sp = GetCommandSPExact("register");
  if (cmd_obj_sp) {
    AddAlias("re", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("scripting run");
  if (cmd_obj_sp) {
    AddAlias("sc", cmd_obj_sp);
    AddAlias("scr", cmd_obj_sp);
    AddAlias("scri", cmd_obj_sp);
    AddAlias("scrip", cmd_obj_sp);
    AddAlias("script", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("session history");
  if (cmd_obj_sp) {
    AddAlias("history", cmd_obj_sp);
  }

  cmd_obj_sp = GetCommandSPExact("help");
  if (cmd_obj_sp) {
    AddAlias("h", cmd_obj_sp);
  }
}

void CommandInterpreter::Clear() {
  m_command_io_handler_sp.reset();
}

const char *CommandInterpreter::ProcessEmbeddedScriptCommands(const char *arg) {
  // This function has not yet been implemented.

  // Look for any embedded script command
  // If found,
  //    get interpreter object from the command dictionary,
  //    call execute_one_command on it,
  //    get the results as a string,
  //    substitute that string for current stuff.

  return arg;
}

#define REGISTER_COMMAND_OBJECT(NAME, CLASS)                                   \
  m_command_dict[NAME] = std::make_shared<CLASS>(*this);

void CommandInterpreter::LoadCommandDictionary() {
  LLDB_SCOPED_TIMER();

  REGISTER_COMMAND_OBJECT("apropos", CommandObjectApropos);
  REGISTER_COMMAND_OBJECT("breakpoint", CommandObjectMultiwordBreakpoint);
  REGISTER_COMMAND_OBJECT("command", CommandObjectMultiwordCommands);
  REGISTER_COMMAND_OBJECT("diagnostics", CommandObjectDiagnostics);
  REGISTER_COMMAND_OBJECT("disassemble", CommandObjectDisassemble);
  REGISTER_COMMAND_OBJECT("dwim-print", CommandObjectDWIMPrint);
  REGISTER_COMMAND_OBJECT("expression", CommandObjectExpression);
  REGISTER_COMMAND_OBJECT("frame", CommandObjectMultiwordFrame);
  REGISTER_COMMAND_OBJECT("gui", CommandObjectGUI);
  REGISTER_COMMAND_OBJECT("help", CommandObjectHelp);
  REGISTER_COMMAND_OBJECT("log", CommandObjectLog);
  REGISTER_COMMAND_OBJECT("memory", CommandObjectMemory);
  REGISTER_COMMAND_OBJECT("platform", CommandObjectPlatform);
  REGISTER_COMMAND_OBJECT("plugin", CommandObjectPlugin);
  REGISTER_COMMAND_OBJECT("process", CommandObjectMultiwordProcess);
  REGISTER_COMMAND_OBJECT("quit", CommandObjectQuit);
  REGISTER_COMMAND_OBJECT("register", CommandObjectRegister);
  REGISTER_COMMAND_OBJECT("scripting", CommandObjectMultiwordScripting);
  REGISTER_COMMAND_OBJECT("settings", CommandObjectMultiwordSettings);
  REGISTER_COMMAND_OBJECT("session", CommandObjectSession);
  REGISTER_COMMAND_OBJECT("source", CommandObjectMultiwordSource);
  REGISTER_COMMAND_OBJECT("statistics", CommandObjectStats);
  REGISTER_COMMAND_OBJECT("target", CommandObjectMultiwordTarget);
  REGISTER_COMMAND_OBJECT("thread", CommandObjectMultiwordThread);
  REGISTER_COMMAND_OBJECT("trace", CommandObjectTrace);
  REGISTER_COMMAND_OBJECT("type", CommandObjectType);
  REGISTER_COMMAND_OBJECT("version", CommandObjectVersion);
  REGISTER_COMMAND_OBJECT("watchpoint", CommandObjectMultiwordWatchpoint);
  REGISTER_COMMAND_OBJECT("language", CommandObjectLanguage);

  // clang-format off
  const char *break_regexes[][2] = {
      {"^(.*[^[:space:]])[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:]]*$",
       "breakpoint set --file '%1' --line %2 --column %3"},
      {"^(.*[^[:space:]])[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:]]*$",
       "breakpoint set --file '%1' --line %2"},
      {"^/([^/]+)/$", "breakpoint set --source-pattern-regexp '%1'"},
      {"^([[:digit:]]+)[[:space:]]*$", "breakpoint set --line %1"},
      {"^\\*?(0x[[:xdigit:]]+)[[:space:]]*$", "breakpoint set --address %1"},
      {"^[\"']?([-+]?\\[.*\\])[\"']?[[:space:]]*$",
       "breakpoint set --name '%1'"},
      {"^(-.*)$", "breakpoint set %1"},
      {"^(.*[^[:space:]])`(.*[^[:space:]])[[:space:]]*$",
       "breakpoint set --name '%2' --shlib '%1'"},
      {"^\\&(.*[^[:space:]])[[:space:]]*$",
       "breakpoint set --name '%1' --skip-prologue=0"},
      {"^[\"']?(.*[^[:space:]\"'])[\"']?[[:space:]]*$",
       "breakpoint set --name '%1'"}};
  // clang-format on

  size_t num_regexes = std::size(break_regexes);

  std::unique_ptr<CommandObjectRegexCommand> break_regex_cmd_up(
      new CommandObjectRegexCommand(
          *this, "_regexp-break",
          "Set a breakpoint using one of several shorthand formats.",
          "\n"
          "_regexp-break <filename>:<linenum>:<colnum>\n"
          "              main.c:12:21          // Break at line 12 and column "
          "21 of main.c\n\n"
          "_regexp-break <filename>:<linenum>\n"
          "              main.c:12             // Break at line 12 of "
          "main.c\n\n"
          "_regexp-break <linenum>\n"
          "              12                    // Break at line 12 of current "
          "file\n\n"
          "_regexp-break 0x<address>\n"
          "              0x1234000             // Break at address "
          "0x1234000\n\n"
          "_regexp-break <name>\n"
          "              main                  // Break in 'main' after the "
          "prologue\n\n"
          "_regexp-break &<name>\n"
          "              &main                 // Break at first instruction "
          "in 'main'\n\n"
          "_regexp-break <module>`<name>\n"
          "              libc.so`malloc        // Break in 'malloc' from "
          "'libc.so'\n\n"
          "_regexp-break /<source-regex>/\n"
          "              /break here/          // Break on source lines in "
          "current file\n"
          "                                    // containing text 'break "
          "here'.\n",
          lldb::eSymbolCompletion | lldb::eSourceFileCompletion, false));

  if (break_regex_cmd_up) {
    bool success = true;
    for (size_t i = 0; i < num_regexes; i++) {
      success = break_regex_cmd_up->AddRegexCommand(break_regexes[i][0],
                                                    break_regexes[i][1]);
      if (!success)
        break;
    }
    success =
        break_regex_cmd_up->AddRegexCommand("^$", "breakpoint list --full");

    if (success) {
      CommandObjectSP break_regex_cmd_sp(break_regex_cmd_up.release());
      m_command_dict[std::string(break_regex_cmd_sp->GetCommandName())] =
          break_regex_cmd_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> tbreak_regex_cmd_up(
      new CommandObjectRegexCommand(
          *this, "_regexp-tbreak",
          "Set a one-shot breakpoint using one of several shorthand formats.",
          "\n"
          "_regexp-break <filename>:<linenum>:<colnum>\n"
          "              main.c:12:21          // Break at line 12 and column "
          "21 of main.c\n\n"
          "_regexp-break <filename>:<linenum>\n"
          "              main.c:12             // Break at line 12 of "
          "main.c\n\n"
          "_regexp-break <linenum>\n"
          "              12                    // Break at line 12 of current "
          "file\n\n"
          "_regexp-break 0x<address>\n"
          "              0x1234000             // Break at address "
          "0x1234000\n\n"
          "_regexp-break <name>\n"
          "              main                  // Break in 'main' after the "
          "prologue\n\n"
          "_regexp-break &<name>\n"
          "              &main                 // Break at first instruction "
          "in 'main'\n\n"
          "_regexp-break <module>`<name>\n"
          "              libc.so`malloc        // Break in 'malloc' from "
          "'libc.so'\n\n"
          "_regexp-break /<source-regex>/\n"
          "              /break here/          // Break on source lines in "
          "current file\n"
          "                                    // containing text 'break "
          "here'.\n",
          lldb::eSymbolCompletion | lldb::eSourceFileCompletion, false));

  if (tbreak_regex_cmd_up) {
    bool success = true;
    for (size_t i = 0; i < num_regexes; i++) {
      std::string command = break_regexes[i][1];
      command += " -o 1";
      success =
          tbreak_regex_cmd_up->AddRegexCommand(break_regexes[i][0], command);
      if (!success)
        break;
    }
    success =
        tbreak_regex_cmd_up->AddRegexCommand("^$", "breakpoint list --full");

    if (success) {
      CommandObjectSP tbreak_regex_cmd_sp(tbreak_regex_cmd_up.release());
      m_command_dict[std::string(tbreak_regex_cmd_sp->GetCommandName())] =
          tbreak_regex_cmd_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> attach_regex_cmd_up(
      new CommandObjectRegexCommand(
          *this, "_regexp-attach", "Attach to process by ID or name.",
          "_regexp-attach <pid> | <process-name>", 0, false));
  if (attach_regex_cmd_up) {
    if (attach_regex_cmd_up->AddRegexCommand("^([0-9]+)[[:space:]]*$",
                                             "process attach --pid %1") &&
        attach_regex_cmd_up->AddRegexCommand(
            "^(-.*|.* -.*)$", "process attach %1") && // Any options that are
                                                      // specified get passed to
                                                      // 'process attach'
        attach_regex_cmd_up->AddRegexCommand("^(.+)$",
                                             "process attach --name '%1'") &&
        attach_regex_cmd_up->AddRegexCommand("^$", "process attach")) {
      CommandObjectSP attach_regex_cmd_sp(attach_regex_cmd_up.release());
      m_command_dict[std::string(attach_regex_cmd_sp->GetCommandName())] =
          attach_regex_cmd_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> down_regex_cmd_up(
      new CommandObjectRegexCommand(*this, "_regexp-down",
                                    "Select a newer stack frame.  Defaults to "
                                    "moving one frame, a numeric argument can "
                                    "specify an arbitrary number.",
                                    "_regexp-down [<count>]", 0, false));
  if (down_regex_cmd_up) {
    if (down_regex_cmd_up->AddRegexCommand("^$", "frame select -r -1") &&
        down_regex_cmd_up->AddRegexCommand("^([0-9]+)$",
                                           "frame select -r -%1")) {
      CommandObjectSP down_regex_cmd_sp(down_regex_cmd_up.release());
      m_command_dict[std::string(down_regex_cmd_sp->GetCommandName())] =
          down_regex_cmd_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> up_regex_cmd_up(
      new CommandObjectRegexCommand(
          *this, "_regexp-up",
          "Select an older stack frame.  Defaults to moving one "
          "frame, a numeric argument can specify an arbitrary number.",
          "_regexp-up [<count>]", 0, false));
  if (up_regex_cmd_up) {
    if (up_regex_cmd_up->AddRegexCommand("^$", "frame select -r 1") &&
        up_regex_cmd_up->AddRegexCommand("^([0-9]+)$", "frame select -r %1")) {
      CommandObjectSP up_regex_cmd_sp(up_regex_cmd_up.release());
      m_command_dict[std::string(up_regex_cmd_sp->GetCommandName())] =
          up_regex_cmd_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> display_regex_cmd_up(
      new CommandObjectRegexCommand(
          *this, "_regexp-display",
          "Evaluate an expression at every stop (see 'help target stop-hook'.)",
          "_regexp-display expression", 0, false));
  if (display_regex_cmd_up) {
    if (display_regex_cmd_up->AddRegexCommand(
            "^(.+)$", "target stop-hook add -o \"expr -- %1\"")) {
      CommandObjectSP display_regex_cmd_sp(display_regex_cmd_up.release());
      m_command_dict[std::string(display_regex_cmd_sp->GetCommandName())] =
          display_regex_cmd_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> undisplay_regex_cmd_up(
      new CommandObjectRegexCommand(*this, "_regexp-undisplay",
                                    "Stop displaying expression at every "
                                    "stop (specified by stop-hook index.)",
                                    "_regexp-undisplay stop-hook-number", 0,
                                    false));
  if (undisplay_regex_cmd_up) {
    if (undisplay_regex_cmd_up->AddRegexCommand("^([0-9]+)$",
                                                "target stop-hook delete %1")) {
      CommandObjectSP undisplay_regex_cmd_sp(undisplay_regex_cmd_up.release());
      m_command_dict[std::string(undisplay_regex_cmd_sp->GetCommandName())] =
          undisplay_regex_cmd_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> connect_gdb_remote_cmd_up(
      new CommandObjectRegexCommand(
          *this, "gdb-remote",
          "Connect to a process via remote GDB server.\n"
          "If no host is specifed, localhost is assumed.\n"
          "gdb-remote is an abbreviation for 'process connect --plugin "
          "gdb-remote connect://<hostname>:<port>'\n",
          "gdb-remote [<hostname>:]<portnum>", 0, false));
  if (connect_gdb_remote_cmd_up) {
    if (connect_gdb_remote_cmd_up->AddRegexCommand(
            "^([^:]+|\\[[0-9a-fA-F:]+.*\\]):([0-9]+)$",
            "process connect --plugin gdb-remote connect://%1:%2") &&
        connect_gdb_remote_cmd_up->AddRegexCommand(
            "^([[:digit:]]+)$",
            "process connect --plugin gdb-remote connect://localhost:%1")) {
      CommandObjectSP command_sp(connect_gdb_remote_cmd_up.release());
      m_command_dict[std::string(command_sp->GetCommandName())] = command_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> connect_kdp_remote_cmd_up(
      new CommandObjectRegexCommand(
          *this, "kdp-remote",
          "Connect to a process via remote KDP server.\n"
          "If no UDP port is specified, port 41139 is assumed.\n"
          "kdp-remote is an abbreviation for 'process connect --plugin "
          "kdp-remote udp://<hostname>:<port>'\n",
          "kdp-remote <hostname>[:<portnum>]", 0, false));
  if (connect_kdp_remote_cmd_up) {
    if (connect_kdp_remote_cmd_up->AddRegexCommand(
            "^([^:]+:[[:digit:]]+)$",
            "process connect --plugin kdp-remote udp://%1") &&
        connect_kdp_remote_cmd_up->AddRegexCommand(
            "^(.+)$", "process connect --plugin kdp-remote udp://%1:41139")) {
      CommandObjectSP command_sp(connect_kdp_remote_cmd_up.release());
      m_command_dict[std::string(command_sp->GetCommandName())] = command_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> bt_regex_cmd_up(
      new CommandObjectRegexCommand(
          *this, "_regexp-bt",
          "Show backtrace of the current thread's call stack.  Any numeric "
          "argument displays at most that many frames.  The argument 'all' "
          "displays all threads.  Use 'settings set frame-format' to customize "
          "the printing of individual frames and 'settings set thread-format' "
          "to customize the thread header.",
          "bt [<digit> | all]", 0, false));
  if (bt_regex_cmd_up) {
    // accept but don't document "bt -c <number>" -- before bt was a regex
    // command if you wanted to backtrace three frames you would do "bt -c 3"
    // but the intention is to have this emulate the gdb "bt" command and so
    // now "bt 3" is the preferred form, in line with gdb.
    if (bt_regex_cmd_up->AddRegexCommand("^([[:digit:]]+)[[:space:]]*$",
                                         "thread backtrace -c %1") &&
        bt_regex_cmd_up->AddRegexCommand("^-c ([[:digit:]]+)[[:space:]]*$",
                                         "thread backtrace -c %1") &&
        bt_regex_cmd_up->AddRegexCommand("^all[[:space:]]*$", "thread backtrace all") &&
        bt_regex_cmd_up->AddRegexCommand("^[[:space:]]*$", "thread backtrace")) {
      CommandObjectSP command_sp(bt_regex_cmd_up.release());
      m_command_dict[std::string(command_sp->GetCommandName())] = command_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> list_regex_cmd_up(
      new CommandObjectRegexCommand(
          *this, "_regexp-list",
          "List relevant source code using one of several shorthand formats.",
          "\n"
          "_regexp-list <file>:<line>   // List around specific file/line\n"
          "_regexp-list <line>          // List current file around specified "
          "line\n"
          "_regexp-list <function-name> // List specified function\n"
          "_regexp-list 0x<address>     // List around specified address\n"
          "_regexp-list -[<count>]      // List previous <count> lines\n"
          "_regexp-list                 // List subsequent lines",
          lldb::eSourceFileCompletion, false));
  if (list_regex_cmd_up) {
    if (list_regex_cmd_up->AddRegexCommand("^([0-9]+)[[:space:]]*$",
                                           "source list --line %1") &&
        list_regex_cmd_up->AddRegexCommand(
            "^(.*[^[:space:]])[[:space:]]*:[[:space:]]*([[:digit:]]+)[[:space:]"
            "]*$",
            "source list --file '%1' --line %2") &&
        list_regex_cmd_up->AddRegexCommand(
            "^\\*?(0x[[:xdigit:]]+)[[:space:]]*$",
            "source list --address %1") &&
        list_regex_cmd_up->AddRegexCommand("^-[[:space:]]*$",
                                           "source list --reverse") &&
        list_regex_cmd_up->AddRegexCommand(
            "^-([[:digit:]]+)[[:space:]]*$",
            "source list --reverse --count %1") &&
        list_regex_cmd_up->AddRegexCommand("^(.+)$",
                                           "source list --name \"%1\"") &&
        list_regex_cmd_up->AddRegexCommand("^$", "source list")) {
      CommandObjectSP list_regex_cmd_sp(list_regex_cmd_up.release());
      m_command_dict[std::string(list_regex_cmd_sp->GetCommandName())] =
          list_regex_cmd_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> env_regex_cmd_up(
      new CommandObjectRegexCommand(
          *this, "_regexp-env",
          "Shorthand for viewing and setting environment variables.",
          "\n"
          "_regexp-env                  // Show environment\n"
          "_regexp-env <name>=<value>   // Set an environment variable",
          0, false));
  if (env_regex_cmd_up) {
    if (env_regex_cmd_up->AddRegexCommand("^$",
                                          "settings show target.env-vars") &&
        env_regex_cmd_up->AddRegexCommand("^([A-Za-z_][A-Za-z_0-9]*=.*)$",
                                          "settings set target.env-vars %1")) {
      CommandObjectSP env_regex_cmd_sp(env_regex_cmd_up.release());
      m_command_dict[std::string(env_regex_cmd_sp->GetCommandName())] =
          env_regex_cmd_sp;
    }
  }

  std::unique_ptr<CommandObjectRegexCommand> jump_regex_cmd_up(
      new CommandObjectRegexCommand(
          *this, "_regexp-jump", "Set the program counter to a new address.",
          "\n"
          "_regexp-jump <line>\n"
          "_regexp-jump +<line-offset> | -<line-offset>\n"
          "_regexp-jump <file>:<line>\n"
          "_regexp-jump *<addr>\n",
          0, false));
  if (jump_regex_cmd_up) {
    if (jump_regex_cmd_up->AddRegexCommand("^\\*(.*)$",
                                           "thread jump --addr %1") &&
        jump_regex_cmd_up->AddRegexCommand("^([0-9]+)$",
                                           "thread jump --line %1") &&
        jump_regex_cmd_up->AddRegexCommand("^([^:]+):([0-9]+)$",
                                           "thread jump --file %1 --line %2") &&
        jump_regex_cmd_up->AddRegexCommand("^([+\\-][0-9]+)$",
                                           "thread jump --by %1")) {
      CommandObjectSP jump_regex_cmd_sp(jump_regex_cmd_up.release());
      m_command_dict[std::string(jump_regex_cmd_sp->GetCommandName())] =
          jump_regex_cmd_sp;
    }
  }
}

int CommandInterpreter::GetCommandNamesMatchingPartialString(
    const char *cmd_str, bool include_aliases, StringList &matches,
    StringList &descriptions) {
  AddNamesMatchingPartialString(m_command_dict, cmd_str, matches,
                                &descriptions);

  if (include_aliases) {
    AddNamesMatchingPartialString(m_alias_dict, cmd_str, matches,
                                  &descriptions);
  }

  return matches.GetSize();
}

CommandObjectMultiword *CommandInterpreter::VerifyUserMultiwordCmdPath(
    Args &path, bool leaf_is_command, Status &result) {
  result.Clear();

  auto get_multi_or_report_error =
      [&result](CommandObjectSP cmd_sp,
                           const char *name) -> CommandObjectMultiword * {
    if (!cmd_sp) {
      result.SetErrorStringWithFormat("Path component: '%s' not found", name);
      return nullptr;
    }
    if (!cmd_sp->IsUserCommand()) {
      result.SetErrorStringWithFormat("Path component: '%s' is not a user "
                                      "command",
                                      name);
      return nullptr;
    }
    CommandObjectMultiword *cmd_as_multi = cmd_sp->GetAsMultiwordCommand();
    if (!cmd_as_multi) {
      result.SetErrorStringWithFormat("Path component: '%s' is not a container "
                                      "command",
                                      name);
      return nullptr;
    }
    return cmd_as_multi;
  };

  size_t num_args = path.GetArgumentCount();
  if (num_args == 0) {
    result.SetErrorString("empty command path");
    return nullptr;
  }

  if (num_args == 1 && leaf_is_command) {
    // We just got a leaf command to be added to the root.  That's not an error,
    // just return null for the container.
    return nullptr;
  }

  // Start by getting the root command from the interpreter.
  const char *cur_name = path.GetArgumentAtIndex(0);
  CommandObjectSP cur_cmd_sp = GetCommandSPExact(cur_name);
  CommandObjectMultiword *cur_as_multi =
      get_multi_or_report_error(cur_cmd_sp, cur_name);
  if (cur_as_multi == nullptr)
    return nullptr;

  size_t num_path_elements = num_args - (leaf_is_command ? 1 : 0);
  for (size_t cursor = 1; cursor < num_path_elements && cur_as_multi != nullptr;
       cursor++) {
    cur_name = path.GetArgumentAtIndex(cursor);
    cur_cmd_sp = cur_as_multi->GetSubcommandSPExact(cur_name);
    cur_as_multi = get_multi_or_report_error(cur_cmd_sp, cur_name);
  }
  return cur_as_multi;
}

CommandObjectSP
CommandInterpreter::GetCommandSP(llvm::StringRef cmd_str, bool include_aliases,
                                 bool exact, StringList *matches,
                                 StringList *descriptions) const {
  CommandObjectSP command_sp;

  std::string cmd = std::string(cmd_str);

  if (HasCommands()) {
    auto pos = m_command_dict.find(cmd);
    if (pos != m_command_dict.end())
      command_sp = pos->second;
  }

  if (include_aliases && HasAliases()) {
    auto alias_pos = m_alias_dict.find(cmd);
    if (alias_pos != m_alias_dict.end())
      command_sp = alias_pos->second;
  }

  if (HasUserCommands()) {
    auto pos = m_user_dict.find(cmd);
    if (pos != m_user_dict.end())
      command_sp = pos->second;
  }

  if (HasUserMultiwordCommands()) {
    auto pos = m_user_mw_dict.find(cmd);
    if (pos != m_user_mw_dict.end())
      command_sp = pos->second;
  }

  if (!exact && !command_sp) {
    // We will only get into here if we didn't find any exact matches.

    CommandObjectSP user_match_sp, user_mw_match_sp, alias_match_sp,
        real_match_sp;

    StringList local_matches;
    if (matches == nullptr)
      matches = &local_matches;

    unsigned int num_cmd_matches = 0;
    unsigned int num_alias_matches = 0;
    unsigned int num_user_matches = 0;
    unsigned int num_user_mw_matches = 0;

    // Look through the command dictionaries one by one, and if we get only one
    // match from any of them in toto, then return that, otherwise return an
    // empty CommandObjectSP and the list of matches.

    if (HasCommands()) {
      num_cmd_matches = AddNamesMatchingPartialString(m_command_dict, cmd_str,
                                                      *matches, descriptions);
    }

    if (num_cmd_matches == 1) {
      cmd.assign(matches->GetStringAtIndex(0));
      auto pos = m_command_dict.find(cmd);
      if (pos != m_command_dict.end())
        real_match_sp = pos->second;
    }

    if (include_aliases && HasAliases()) {
      num_alias_matches = AddNamesMatchingPartialString(m_alias_dict, cmd_str,
                                                        *matches, descriptions);
    }

    if (num_alias_matches == 1) {
      cmd.assign(matches->GetStringAtIndex(num_cmd_matches));
      auto alias_pos = m_alias_dict.find(cmd);
      if (alias_pos != m_alias_dict.end())
        alias_match_sp = alias_pos->second;
    }

    if (HasUserCommands()) {
      num_user_matches = AddNamesMatchingPartialString(m_user_dict, cmd_str,
                                                       *matches, descriptions);
    }

    if (num_user_matches == 1) {
      cmd.assign(
          matches->GetStringAtIndex(num_cmd_matches + num_alias_matches));

      auto pos = m_user_dict.find(cmd);
      if (pos != m_user_dict.end())
        user_match_sp = pos->second;
    }

    if (HasUserMultiwordCommands()) {
      num_user_mw_matches = AddNamesMatchingPartialString(
          m_user_mw_dict, cmd_str, *matches, descriptions);
    }

    if (num_user_mw_matches == 1) {
      cmd.assign(matches->GetStringAtIndex(num_cmd_matches + num_alias_matches +
                                           num_user_matches));

      auto pos = m_user_mw_dict.find(cmd);
      if (pos != m_user_mw_dict.end())
        user_mw_match_sp = pos->second;
    }

    // If we got exactly one match, return that, otherwise return the match
    // list.

    if (num_user_matches + num_user_mw_matches + num_cmd_matches +
            num_alias_matches ==
        1) {
      if (num_cmd_matches)
        return real_match_sp;
      else if (num_alias_matches)
        return alias_match_sp;
      else if (num_user_mw_matches)
        return user_mw_match_sp;
      else
        return user_match_sp;
    }
  } else if (matches && command_sp) {
    matches->AppendString(cmd_str);
    if (descriptions)
      descriptions->AppendString(command_sp->GetHelp());
  }

  return command_sp;
}

bool CommandInterpreter::AddCommand(llvm::StringRef name,
                                    const lldb::CommandObjectSP &cmd_sp,
                                    bool can_replace) {
  if (cmd_sp.get())
    lldbassert((this == &cmd_sp->GetCommandInterpreter()) &&
               "tried to add a CommandObject from a different interpreter");

  if (name.empty())
    return false;

  cmd_sp->SetIsUserCommand(false);

  std::string name_sstr(name);
  auto name_iter = m_command_dict.find(name_sstr);
  if (name_iter != m_command_dict.end()) {
    if (!can_replace || !name_iter->second->IsRemovable())
      return false;
    name_iter->second = cmd_sp;
  } else {
    m_command_dict[name_sstr] = cmd_sp;
  }
  return true;
}

Status CommandInterpreter::AddUserCommand(llvm::StringRef name,
                                          const lldb::CommandObjectSP &cmd_sp,
                                          bool can_replace) {
  Status result;
  if (cmd_sp.get())
    lldbassert((this == &cmd_sp->GetCommandInterpreter()) &&
               "tried to add a CommandObject from a different interpreter");
  if (name.empty()) {
    result.SetErrorString("can't use the empty string for a command name");
    return result;
  }
  // do not allow replacement of internal commands
  if (CommandExists(name)) {
    result.SetErrorString("can't replace builtin command");
    return result;
  }

  if (UserCommandExists(name)) {
    if (!can_replace) {
      result.SetErrorStringWithFormatv(
          "user command \"{0}\" already exists and force replace was not set "
          "by --overwrite or 'settings set interpreter.require-overwrite "
          "false'",
          name);
      return result;
    }
    if (cmd_sp->IsMultiwordObject()) {
      if (!m_user_mw_dict[std::string(name)]->IsRemovable()) {
        result.SetErrorString(
            "can't replace explicitly non-removable multi-word command");
        return result;
      }
    } else {
      if (!m_user_dict[std::string(name)]->IsRemovable()) {
        result.SetErrorString("can't replace explicitly non-removable command");
        return result;
      }
    }
  }

  cmd_sp->SetIsUserCommand(true);

  if (cmd_sp->IsMultiwordObject())
    m_user_mw_dict[std::string(name)] = cmd_sp;
  else
    m_user_dict[std::string(name)] = cmd_sp;
  return result;
}

CommandObjectSP
CommandInterpreter::GetCommandSPExact(llvm::StringRef cmd_str,
                                      bool include_aliases) const {
  // Break up the command string into words, in case it's a multi-word command.
  Args cmd_words(cmd_str);

  if (cmd_str.empty())
    return {};

  if (cmd_words.GetArgumentCount() == 1)
    return GetCommandSP(cmd_str, include_aliases, true);

  // We have a multi-word command (seemingly), so we need to do more work.
  // First, get the cmd_obj_sp for the first word in the command.
  CommandObjectSP cmd_obj_sp =
      GetCommandSP(cmd_words.GetArgumentAtIndex(0), include_aliases, true);
  if (!cmd_obj_sp)
    return {};

  // Loop through the rest of the words in the command (everything passed in
  // was supposed to be part of a command name), and find the appropriate
  // sub-command SP for each command word....
  size_t end = cmd_words.GetArgumentCount();
  for (size_t i = 1; i < end; ++i) {
    if (!cmd_obj_sp->IsMultiwordObject()) {
      // We have more words in the command name, but we don't have a
      // multiword object. Fail and return.
      return {};
    }

    cmd_obj_sp = cmd_obj_sp->GetSubcommandSP(cmd_words.GetArgumentAtIndex(i));
    if (!cmd_obj_sp) {
      // The sub-command name was invalid.  Fail and return.
      return {};
    }
  }

  // We successfully looped through all the command words and got valid
  // command objects for them.
  return cmd_obj_sp;
}

CommandObject *
CommandInterpreter::GetCommandObject(llvm::StringRef cmd_str,
                                     StringList *matches,
                                     StringList *descriptions) const {
  // Try to find a match among commands and aliases. Allowing inexact matches,
  // but perferring exact matches.
  return GetCommandSP(cmd_str, /*include_aliases=*/true, /*exact=*/false,
                             matches, descriptions)
                    .get();
}

CommandObject *CommandInterpreter::GetUserCommandObject(
    llvm::StringRef cmd, StringList *matches, StringList *descriptions) const {
  std::string cmd_str(cmd);
  auto find_exact = [&](const CommandObject::CommandMap &map) {
    auto found_elem = map.find(std::string(cmd));
    if (found_elem == map.end())
      return (CommandObject *)nullptr;
    CommandObject *exact_cmd = found_elem->second.get();
    if (exact_cmd) {
      if (matches)
        matches->AppendString(exact_cmd->GetCommandName());
      if (descriptions)
        descriptions->AppendString(exact_cmd->GetHelp());
      return exact_cmd;
    }
    return (CommandObject *)nullptr;
  };

  CommandObject *exact_cmd = find_exact(GetUserCommands());
  if (exact_cmd)
    return exact_cmd;

  exact_cmd = find_exact(GetUserMultiwordCommands());
  if (exact_cmd)
    return exact_cmd;

  // We didn't have an exact command, so now look for partial matches.
  StringList tmp_list;
  StringList *matches_ptr = matches ? matches : &tmp_list;
  AddNamesMatchingPartialString(GetUserCommands(), cmd_str, *matches_ptr);
  AddNamesMatchingPartialString(GetUserMultiwordCommands(),
                                cmd_str, *matches_ptr);

  return {};
}

bool CommandInterpreter::CommandExists(llvm::StringRef cmd) const {
  return m_command_dict.find(std::string(cmd)) != m_command_dict.end();
}

bool CommandInterpreter::GetAliasFullName(llvm::StringRef cmd,
                                          std::string &full_name) const {
  bool exact_match =
      (m_alias_dict.find(std::string(cmd)) != m_alias_dict.end());
  if (exact_match) {
    full_name.assign(std::string(cmd));
    return exact_match;
  } else {
    StringList matches;
    size_t num_alias_matches;
    num_alias_matches =
        AddNamesMatchingPartialString(m_alias_dict, cmd, matches);
    if (num_alias_matches == 1) {
      // Make sure this isn't shadowing a command in the regular command space:
      StringList regular_matches;
      const bool include_aliases = false;
      const bool exact = false;
      CommandObjectSP cmd_obj_sp(
          GetCommandSP(cmd, include_aliases, exact, &regular_matches));
      if (cmd_obj_sp || regular_matches.GetSize() > 0)
        return false;
      else {
        full_name.assign(matches.GetStringAtIndex(0));
        return true;
      }
    } else
      return false;
  }
}

bool CommandInterpreter::AliasExists(llvm::StringRef cmd) const {
  return m_alias_dict.find(std::string(cmd)) != m_alias_dict.end();
}

bool CommandInterpreter::UserCommandExists(llvm::StringRef cmd) const {
  return m_user_dict.find(std::string(cmd)) != m_user_dict.end();
}

bool CommandInterpreter::UserMultiwordCommandExists(llvm::StringRef cmd) const {
  return m_user_mw_dict.find(std::string(cmd)) != m_user_mw_dict.end();
}

CommandAlias *
CommandInterpreter::AddAlias(llvm::StringRef alias_name,
                             lldb::CommandObjectSP &command_obj_sp,
                             llvm::StringRef args_string) {
  if (command_obj_sp.get())
    lldbassert((this == &command_obj_sp->GetCommandInterpreter()) &&
               "tried to add a CommandObject from a different interpreter");

  std::unique_ptr<CommandAlias> command_alias_up(
      new CommandAlias(*this, command_obj_sp, args_string, alias_name));

  if (command_alias_up && command_alias_up->IsValid()) {
    m_alias_dict[std::string(alias_name)] =
        CommandObjectSP(command_alias_up.get());
    return command_alias_up.release();
  }

  return nullptr;
}

bool CommandInterpreter::RemoveAlias(llvm::StringRef alias_name) {
  auto pos = m_alias_dict.find(std::string(alias_name));
  if (pos != m_alias_dict.end()) {
    m_alias_dict.erase(pos);
    return true;
  }
  return false;
}

bool CommandInterpreter::RemoveCommand(llvm::StringRef cmd, bool force) {
  auto pos = m_command_dict.find(std::string(cmd));
  if (pos != m_command_dict.end()) {
    if (force || pos->second->IsRemovable()) {
      // Only regular expression objects or python commands are removable under
      // normal circumstances.
      m_command_dict.erase(pos);
      return true;
    }
  }
  return false;
}

bool CommandInterpreter::RemoveUser(llvm::StringRef user_name) {
  CommandObject::CommandMap::iterator pos =
      m_user_dict.find(std::string(user_name));
  if (pos != m_user_dict.end()) {
    m_user_dict.erase(pos);
    return true;
  }
  return false;
}

bool CommandInterpreter::RemoveUserMultiword(llvm::StringRef multi_name) {
  CommandObject::CommandMap::iterator pos =
      m_user_mw_dict.find(std::string(multi_name));
  if (pos != m_user_mw_dict.end()) {
    m_user_mw_dict.erase(pos);
    return true;
  }
  return false;
}

void CommandInterpreter::GetHelp(CommandReturnObject &result,
                                 uint32_t cmd_types) {
  llvm::StringRef help_prologue(GetDebugger().GetIOHandlerHelpPrologue());
  if (!help_prologue.empty()) {
    OutputFormattedHelpText(result.GetOutputStream(), llvm::StringRef(),
                            help_prologue);
  }

  CommandObject::CommandMap::const_iterator pos;
  size_t max_len = FindLongestCommandWord(m_command_dict);

  if ((cmd_types & eCommandTypesBuiltin) == eCommandTypesBuiltin) {
    result.AppendMessage("Debugger commands:");
    result.AppendMessage("");

    for (pos = m_command_dict.begin(); pos != m_command_dict.end(); ++pos) {
      if (!(cmd_types & eCommandTypesHidden) &&
          (pos->first.compare(0, 1, "_") == 0))
        continue;

      OutputFormattedHelpText(result.GetOutputStream(), pos->first, "--",
                              pos->second->GetHelp(), max_len);
    }
    result.AppendMessage("");
  }

  if (!m_alias_dict.empty() &&
      ((cmd_types & eCommandTypesAliases) == eCommandTypesAliases)) {
    result.AppendMessageWithFormat(
        "Current command abbreviations "
        "(type '%shelp command alias' for more info):\n",
        GetCommandPrefix());
    result.AppendMessage("");
    max_len = FindLongestCommandWord(m_alias_dict);

    for (auto alias_pos = m_alias_dict.begin(); alias_pos != m_alias_dict.end();
         ++alias_pos) {
      OutputFormattedHelpText(result.GetOutputStream(), alias_pos->first, "--",
                              alias_pos->second->GetHelp(), max_len);
    }
    result.AppendMessage("");
  }

  if (!m_user_dict.empty() &&
      ((cmd_types & eCommandTypesUserDef) == eCommandTypesUserDef)) {
    result.AppendMessage("Current user-defined commands:");
    result.AppendMessage("");
    max_len = FindLongestCommandWord(m_user_dict);
    for (pos = m_user_dict.begin(); pos != m_user_dict.end(); ++pos) {
      OutputFormattedHelpText(result.GetOutputStream(), pos->first, "--",
                              pos->second->GetHelp(), max_len);
    }
    result.AppendMessage("");
  }

  if (!m_user_mw_dict.empty() &&
      ((cmd_types & eCommandTypesUserMW) == eCommandTypesUserMW)) {
    result.AppendMessage("Current user-defined container commands:");
    result.AppendMessage("");
    max_len = FindLongestCommandWord(m_user_mw_dict);
    for (pos = m_user_mw_dict.begin(); pos != m_user_mw_dict.end(); ++pos) {
      OutputFormattedHelpText(result.GetOutputStream(), pos->first, "--",
                              pos->second->GetHelp(), max_len);
    }
    result.AppendMessage("");
  }

  result.AppendMessageWithFormat(
      "For more information on any command, type '%shelp <command-name>'.\n",
      GetCommandPrefix());
}

CommandObject *CommandInterpreter::GetCommandObjectForCommand(
    llvm::StringRef &command_string) {
  // This function finds the final, lowest-level, alias-resolved command object
  // whose 'Execute' function will eventually be invoked by the given command
  // line.

  CommandObject *cmd_obj = nullptr;
  size_t start = command_string.find_first_not_of(k_white_space);
  size_t end = 0;
  bool done = false;
  while (!done) {
    if (start != std::string::npos) {
      // Get the next word from command_string.
      end = command_string.find_first_of(k_white_space, start);
      if (end == std::string::npos)
        end = command_string.size();
      std::string cmd_word =
          std::string(command_string.substr(start, end - start));

      if (cmd_obj == nullptr)
        // Since cmd_obj is NULL we are on our first time through this loop.
        // Check to see if cmd_word is a valid command or alias.
        cmd_obj = GetCommandObject(cmd_word);
      else if (cmd_obj->IsMultiwordObject()) {
        // Our current object is a multi-word object; see if the cmd_word is a
        // valid sub-command for our object.
        CommandObject *sub_cmd_obj =
            cmd_obj->GetSubcommandObject(cmd_word.c_str());
        if (sub_cmd_obj)
          cmd_obj = sub_cmd_obj;
        else // cmd_word was not a valid sub-command word, so we are done
          done = true;
      } else
        // We have a cmd_obj and it is not a multi-word object, so we are done.
        done = true;

      // If we didn't find a valid command object, or our command object is not
      // a multi-word object, or we are at the end of the command_string, then
      // we are done.  Otherwise, find the start of the next word.

      if (!cmd_obj || !cmd_obj->IsMultiwordObject() ||
          end >= command_string.size())
        done = true;
      else
        start = command_string.find_first_not_of(k_white_space, end);
    } else
      // Unable to find any more words.
      done = true;
  }

  command_string = command_string.substr(end);
  return cmd_obj;
}

static const char *k_valid_command_chars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_";
static void StripLeadingSpaces(std::string &s) {
  if (!s.empty()) {
    size_t pos = s.find_first_not_of(k_white_space);
    if (pos == std::string::npos)
      s.clear();
    else if (pos == 0)
      return;
    s.erase(0, pos);
  }
}

static size_t FindArgumentTerminator(const std::string &s) {
  const size_t s_len = s.size();
  size_t offset = 0;
  while (offset < s_len) {
    size_t pos = s.find("--", offset);
    if (pos == std::string::npos)
      break;
    if (pos > 0) {
      if (llvm::isSpace(s[pos - 1])) {
        // Check if the string ends "\s--" (where \s is a space character) or
        // if we have "\s--\s".
        if ((pos + 2 >= s_len) || llvm::isSpace(s[pos + 2])) {
          return pos;
        }
      }
    }
    offset = pos + 2;
  }
  return std::string::npos;
}

static bool ExtractCommand(std::string &command_string, std::string &command,
                           std::string &suffix, char &quote_char) {
  command.clear();
  suffix.clear();
  StripLeadingSpaces(command_string);

  bool result = false;
  quote_char = '\0';

  if (!command_string.empty()) {
    const char first_char = command_string[0];
    if (first_char == '\'' || first_char == '"') {
      quote_char = first_char;
      const size_t end_quote_pos = command_string.find(quote_char, 1);
      if (end_quote_pos == std::string::npos) {
        command.swap(command_string);
        command_string.erase();
      } else {
        command.assign(command_string, 1, end_quote_pos - 1);
        if (end_quote_pos + 1 < command_string.size())
          command_string.erase(0, command_string.find_first_not_of(
                                      k_white_space, end_quote_pos + 1));
        else
          command_string.erase();
      }
    } else {
      const size_t first_space_pos =
          command_string.find_first_of(k_white_space);
      if (first_space_pos == std::string::npos) {
        command.swap(command_string);
        command_string.erase();
      } else {
        command.assign(command_string, 0, first_space_pos);
        command_string.erase(0, command_string.find_first_not_of(
                                    k_white_space, first_space_pos));
      }
    }
    result = true;
  }

  if (!command.empty()) {
    // actual commands can't start with '-' or '_'
    if (command[0] != '-' && command[0] != '_') {
      size_t pos = command.find_first_not_of(k_valid_command_chars);
      if (pos > 0 && pos != std::string::npos) {
        suffix.assign(command.begin() + pos, command.end());
        command.erase(pos);
      }
    }
  }

  return result;
}

CommandObject *CommandInterpreter::BuildAliasResult(
    llvm::StringRef alias_name, std::string &raw_input_string,
    std::string &alias_result, CommandReturnObject &result) {
  CommandObject *alias_cmd_obj = nullptr;
  Args cmd_args(raw_input_string);
  alias_cmd_obj = GetCommandObject(alias_name);
  StreamString result_str;

  if (!alias_cmd_obj || !alias_cmd_obj->IsAlias()) {
    alias_result.clear();
    return alias_cmd_obj;
  }
  std::pair<CommandObjectSP, OptionArgVectorSP> desugared =
      ((CommandAlias *)alias_cmd_obj)->Desugar();
  OptionArgVectorSP option_arg_vector_sp = desugared.second;
  alias_cmd_obj = desugared.first.get();
  std::string alias_name_str = std::string(alias_name);
  if ((cmd_args.GetArgumentCount() == 0) ||
      (alias_name_str != cmd_args.GetArgumentAtIndex(0)))
    cmd_args.Unshift(alias_name_str);

  result_str.Printf("%s", alias_cmd_obj->GetCommandName().str().c_str());

  if (!option_arg_vector_sp.get()) {
    alias_result = std::string(result_str.GetString());
    return alias_cmd_obj;
  }
  OptionArgVector *option_arg_vector = option_arg_vector_sp.get();

  int value_type;
  std::string option;
  std::string value;
  for (const auto &entry : *option_arg_vector) {
    std::tie(option, value_type, value) = entry;
    if (option == g_argument) {
      result_str.Printf(" %s", value.c_str());
      continue;
    }

    result_str.Printf(" %s", option.c_str());
    if (value_type == OptionParser::eNoArgument)
      continue;

    if (value_type != OptionParser::eOptionalArgument)
      result_str.Printf(" ");
    int index = GetOptionArgumentPosition(value.c_str());
    if (index == 0)
      result_str.Printf("%s", value.c_str());
    else if (static_cast<size_t>(index) >= cmd_args.GetArgumentCount()) {

      result.AppendErrorWithFormat("Not enough arguments provided; you "
                                   "need at least %d arguments to use "
                                   "this alias.\n",
                                   index);
      return nullptr;
    } else {
      const Args::ArgEntry &entry = cmd_args[index];
      size_t strpos = raw_input_string.find(entry.c_str());
      const char quote_char = entry.GetQuoteChar();
      if (strpos != std::string::npos) {
        const size_t start_fudge = quote_char == '\0' ? 0 : 1;
        const size_t len_fudge = quote_char == '\0' ? 0 : 2;

        // Make sure we aren't going outside the bounds of the cmd string:
        if (strpos < start_fudge) {
          result.AppendError("Unmatched quote at command beginning.");
          return nullptr;
        }
        llvm::StringRef arg_text = entry.ref();
        if (strpos - start_fudge + arg_text.size() + len_fudge >
            raw_input_string.size()) {
          result.AppendError("Unmatched quote at command end.");
          return nullptr;
        }
        raw_input_string = raw_input_string.erase(
            strpos - start_fudge,
            strlen(cmd_args.GetArgumentAtIndex(index)) + len_fudge);
      }
      if (quote_char == '\0')
        result_str.Printf("%s", cmd_args.GetArgumentAtIndex(index));
      else
        result_str.Printf("%c%s%c", quote_char, entry.c_str(), quote_char);
    }
  }

  alias_result = std::string(result_str.GetString());
  return alias_cmd_obj;
}

Status CommandInterpreter::PreprocessCommand(std::string &command) {
  // The command preprocessor needs to do things to the command line before any
  // parsing of arguments or anything else is done. The only current stuff that
  // gets preprocessed is anything enclosed in backtick ('`') characters is
  // evaluated as an expression and the result of the expression must be a
  // scalar that can be substituted into the command. An example would be:
  // (lldb) memory read `$rsp + 20`
  Status error; // Status for any expressions that might not evaluate
  size_t start_backtick;
  size_t pos = 0;
  while ((start_backtick = command.find('`', pos)) != std::string::npos) {
    // Stop if an error was encountered during the previous iteration.
    if (error.Fail())
      break;

    if (start_backtick > 0 && command[start_backtick - 1] == '\\') {
      // The backtick was preceded by a '\' character, remove the slash and
      // don't treat the backtick as the start of an expression.
      command.erase(start_backtick - 1, 1);
      // No need to add one to start_backtick since we just deleted a char.
      pos = start_backtick;
      continue;
    }

    const size_t expr_content_start = start_backtick + 1;
    const size_t end_backtick = command.find('`', expr_content_start);

    if (end_backtick == std::string::npos) {
      // Stop if there's no end backtick.
      break;
    }

    if (end_backtick == expr_content_start) {
      // Skip over empty expression. (two backticks in a row)
      command.erase(start_backtick, 2);
      continue;
    }

    std::string expr_str(command, expr_content_start,
                         end_backtick - expr_content_start);
    error = PreprocessToken(expr_str);
    // We always stop at the first error:
    if (error.Fail())
      break;

    command.erase(start_backtick, end_backtick - start_backtick + 1);
    command.insert(start_backtick, std::string(expr_str));
    pos = start_backtick + expr_str.size();
  }
  return error;
}

Status
CommandInterpreter::PreprocessToken(std::string &expr_str) {
  Status error;
  ExecutionContext exe_ctx(GetExecutionContext());

  // Get a dummy target to allow for calculator mode while processing
  // backticks. This also helps break the infinite loop caused when target is
  // null.
  Target *exe_target = exe_ctx.GetTargetPtr();
  Target &target = exe_target ? *exe_target : m_debugger.GetDummyTarget();

  ValueObjectSP expr_result_valobj_sp;

  EvaluateExpressionOptions options;
  options.SetCoerceToId(false);
  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  options.SetKeepInMemory(false);
  options.SetTryAllThreads(true);
  options.SetTimeout(std::nullopt);

  ExpressionResults expr_result =
      target.EvaluateExpression(expr_str.c_str(), exe_ctx.GetFramePtr(),
                                expr_result_valobj_sp, options);

  if (expr_result == eExpressionCompleted) {
    Scalar scalar;
    if (expr_result_valobj_sp)
      expr_result_valobj_sp =
          expr_result_valobj_sp->GetQualifiedRepresentationIfAvailable(
              expr_result_valobj_sp->GetDynamicValueType(), true);
    if (expr_result_valobj_sp->ResolveValue(scalar)) {

      StreamString value_strm;
      const bool show_type = false;
      scalar.GetValue(value_strm, show_type);
      size_t value_string_size = value_strm.GetSize();
      if (value_string_size) {
        expr_str = value_strm.GetData();
      } else {
        error.SetErrorStringWithFormat("expression value didn't result "
                                       "in a scalar value for the "
                                       "expression '%s'",
                                       expr_str.c_str());
      }
    } else {
      error.SetErrorStringWithFormat("expression value didn't result "
                                     "in a scalar value for the "
                                     "expression '%s'",
                                     expr_str.c_str());
    }
    return error;
  }

  // If we have an error from the expression evaluation it will be in the
  // ValueObject error, which won't be success and we will just report it.
  // But if for some reason we didn't get a value object at all, then we will
  // make up some helpful errors from the expression result.
  if (expr_result_valobj_sp)
    error = expr_result_valobj_sp->GetError();

  if (error.Success()) {
    std::string result = lldb_private::toString(expr_result);
    error.SetErrorString(result + "for the expression '" + expr_str + "'");
  }
  return error;
}

bool CommandInterpreter::HandleCommand(const char *command_line,
                                       LazyBool lazy_add_to_history,
                                       const ExecutionContext &override_context,
                                       CommandReturnObject &result) {

  OverrideExecutionContext(override_context);
  bool status = HandleCommand(command_line, lazy_add_to_history, result);
  RestoreExecutionContext();
  return status;
}

bool CommandInterpreter::HandleCommand(const char *command_line,
                                       LazyBool lazy_add_to_history,
                                       CommandReturnObject &result,
                                       bool force_repeat_command) {
  std::string command_string(command_line);
  std::string original_command_string(command_line);

  Log *log = GetLog(LLDBLog::Commands);
  llvm::PrettyStackTraceFormat stack_trace("HandleCommand(command = \"%s\")",
                                   command_line);

  LLDB_LOGF(log, "Processing command: %s", command_line);
  LLDB_SCOPED_TIMERF("Processing command: %s.", command_line);

  if (INTERRUPT_REQUESTED(GetDebugger(), "Interrupted initiating command")) {
    result.AppendError("... Interrupted");
    return false;
  }

  bool add_to_history;
  if (lazy_add_to_history == eLazyBoolCalculate)
    add_to_history = (m_command_source_depth == 0);
  else
    add_to_history = (lazy_add_to_history == eLazyBoolYes);

  // The same `transcript_item` will be used below to add output and error of
  // the command.
  StructuredData::DictionarySP transcript_item;
  if (GetSaveTranscript()) {
    m_transcript_stream << "(lldb) " << command_line << '\n';

    transcript_item = std::make_shared<StructuredData::Dictionary>();
    transcript_item->AddStringItem("command", command_line);
    transcript_item->AddIntegerItem(
        "timestampInEpochSeconds",
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
    m_transcript.AddItem(transcript_item);
  }

  bool empty_command = false;
  bool comment_command = false;
  if (command_string.empty())
    empty_command = true;
  else {
    const char *k_space_characters = "\t\n\v\f\r ";

    size_t non_space = command_string.find_first_not_of(k_space_characters);
    // Check for empty line or comment line (lines whose first non-space
    // character is the comment character for this interpreter)
    if (non_space == std::string::npos)
      empty_command = true;
    else if (command_string[non_space] == m_comment_char)
      comment_command = true;
    else if (command_string[non_space] == CommandHistory::g_repeat_char) {
      llvm::StringRef search_str(command_string);
      search_str = search_str.drop_front(non_space);
      if (auto hist_str = m_command_history.FindString(search_str)) {
        add_to_history = false;
        command_string = std::string(*hist_str);
        original_command_string = std::string(*hist_str);
      } else {
        result.AppendErrorWithFormat("Could not find entry: %s in history",
                                     command_string.c_str());
        return false;
      }
    }
  }

  if (empty_command) {
    if (!GetRepeatPreviousCommand()) {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
      return true;
    }

    if (m_command_history.IsEmpty()) {
      result.AppendError("empty command");
      return false;
    }

    command_line = m_repeat_command.c_str();
    command_string = command_line;
    original_command_string = command_line;
    if (m_repeat_command.empty()) {
      result.AppendError("No auto repeat.");
      return false;
    }

    add_to_history = false;
  } else if (comment_command) {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    return true;
  }

  // Phase 1.

  // Before we do ANY kind of argument processing, we need to figure out what
  // the real/final command object is for the specified command.  This gets
  // complicated by the fact that the user could have specified an alias, and,
  // in translating the alias, there may also be command options and/or even
  // data (including raw text strings) that need to be found and inserted into
  // the command line as part of the translation.  So this first step is plain
  // look-up and replacement, resulting in:
  //    1. the command object whose Execute method will actually be called
  //    2. a revised command string, with all substitutions and replacements
  //       taken care of
  // From 1 above, we can determine whether the Execute function wants raw
  // input or not.

  CommandObject *cmd_obj = ResolveCommandImpl(command_string, result);

  // We have to preprocess the whole command string for Raw commands, since we
  // don't know the structure of the command.  For parsed commands, we only
  // treat backticks as quote characters specially.
  // FIXME: We probably want to have raw commands do their own preprocessing.
  // For instance, I don't think people expect substitution in expr expressions.
  if (cmd_obj && cmd_obj->WantsRawCommandString()) {
    Status error(PreprocessCommand(command_string));

    if (error.Fail()) {
      result.AppendError(error.AsCString());
      return false;
    }
  }

  // Although the user may have abbreviated the command, the command_string now
  // has the command expanded to the full name.  For example, if the input was
  // "br s -n main", command_string is now "breakpoint set -n main".
  if (log) {
    llvm::StringRef command_name = cmd_obj ? cmd_obj->GetCommandName() : "<not found>";
    LLDB_LOGF(log, "HandleCommand, cmd_obj : '%s'", command_name.str().c_str());
    LLDB_LOGF(log, "HandleCommand, (revised) command_string: '%s'",
              command_string.c_str());
    const bool wants_raw_input =
        (cmd_obj != nullptr) ? cmd_obj->WantsRawCommandString() : false;
    LLDB_LOGF(log, "HandleCommand, wants_raw_input:'%s'",
              wants_raw_input ? "True" : "False");
  }

  // Phase 2.
  // Take care of things like setting up the history command & calling the
  // appropriate Execute method on the CommandObject, with the appropriate
  // arguments.
  StatsDuration execute_time;
  if (cmd_obj != nullptr) {
    bool generate_repeat_command = add_to_history;
    // If we got here when empty_command was true, then this command is a
    // stored "repeat command" which we should give a chance to produce it's
    // repeat command, even though we don't add repeat commands to the history.
    generate_repeat_command |= empty_command;
    // For `command regex`, the regex command (ex `bt`) is added to history, but
    // the resolved command (ex `thread backtrace`) is _not_ added to history.
    // However, the resolved command must be given the opportunity to provide a
    // repeat command. `force_repeat_command` supports this case.
    generate_repeat_command |= force_repeat_command;
    if (generate_repeat_command) {
      Args command_args(command_string);
      std::optional<std::string> repeat_command =
          cmd_obj->GetRepeatCommand(command_args, 0);
      if (repeat_command) {
        LLDB_LOGF(log, "Repeat command: %s", repeat_command->data());
        m_repeat_command.assign(*repeat_command);
      } else {
        m_repeat_command.assign(original_command_string);
      }
    }

    if (add_to_history)
      m_command_history.AppendString(original_command_string);

    std::string remainder;
    const std::size_t actual_cmd_name_len = cmd_obj->GetCommandName().size();
    if (actual_cmd_name_len < command_string.length())
      remainder = command_string.substr(actual_cmd_name_len);

    // Remove any initial spaces
    size_t pos = remainder.find_first_not_of(k_white_space);
    if (pos != 0 && pos != std::string::npos)
      remainder.erase(0, pos);

    LLDB_LOGF(
        log, "HandleCommand, command line after removing command name(s): '%s'",
        remainder.c_str());

    // To test whether or not transcript should be saved, `transcript_item` is
    // used instead of `GetSaveTrasncript()`. This is because the latter will
    // fail when the command is "settings set interpreter.save-transcript true".
    if (transcript_item) {
      transcript_item->AddStringItem("commandName", cmd_obj->GetCommandName());
      transcript_item->AddStringItem("commandArguments", remainder);
    }

    ElapsedTime elapsed(execute_time);
    cmd_obj->Execute(remainder.c_str(), result);
  }

  LLDB_LOGF(log, "HandleCommand, command %s",
            (result.Succeeded() ? "succeeded" : "did not succeed"));

  // To test whether or not transcript should be saved, `transcript_item` is
  // used instead of `GetSaveTrasncript()`. This is because the latter will
  // fail when the command is "settings set interpreter.save-transcript true".
  if (transcript_item) {
    m_transcript_stream << result.GetOutputData();
    m_transcript_stream << result.GetErrorData();

    transcript_item->AddStringItem("output", result.GetOutputData());
    transcript_item->AddStringItem("error", result.GetErrorData());
    transcript_item->AddFloatItem("durationInSeconds",
                                  execute_time.get().count());
  }

  return result.Succeeded();
}

void CommandInterpreter::HandleCompletionMatches(CompletionRequest &request) {
  bool look_for_subcommand = false;

  // For any of the command completions a unique match will be a complete word.

  if (request.GetParsedLine().GetArgumentCount() == 0) {
    // We got nothing on the command line, so return the list of commands
    bool include_aliases = true;
    StringList new_matches, descriptions;
    GetCommandNamesMatchingPartialString("", include_aliases, new_matches,
                                         descriptions);
    request.AddCompletions(new_matches, descriptions);
  } else if (request.GetCursorIndex() == 0) {
    // The cursor is in the first argument, so just do a lookup in the
    // dictionary.
    StringList new_matches, new_descriptions;
    CommandObject *cmd_obj =
        GetCommandObject(request.GetParsedLine().GetArgumentAtIndex(0),
                         &new_matches, &new_descriptions);

    if (new_matches.GetSize() && cmd_obj && cmd_obj->IsMultiwordObject() &&
        new_matches.GetStringAtIndex(0) != nullptr &&
        strcmp(request.GetParsedLine().GetArgumentAtIndex(0),
               new_matches.GetStringAtIndex(0)) == 0) {
      if (request.GetParsedLine().GetArgumentCount() != 1) {
        look_for_subcommand = true;
        new_matches.DeleteStringAtIndex(0);
        new_descriptions.DeleteStringAtIndex(0);
        request.AppendEmptyArgument();
      }
    }
    request.AddCompletions(new_matches, new_descriptions);
  }

  if (request.GetCursorIndex() > 0 || look_for_subcommand) {
    // We are completing further on into a commands arguments, so find the
    // command and tell it to complete the command. First see if there is a
    // matching initial command:
    CommandObject *command_object =
        GetCommandObject(request.GetParsedLine().GetArgumentAtIndex(0));
    if (command_object) {
      request.ShiftArguments();
      command_object->HandleCompletion(request);
    }
  }
}

void CommandInterpreter::HandleCompletion(CompletionRequest &request) {

  // Don't complete comments, and if the line we are completing is just the
  // history repeat character, substitute the appropriate history line.
  llvm::StringRef first_arg = request.GetParsedLine().GetArgumentAtIndex(0);

  if (!first_arg.empty()) {
    if (first_arg.front() == m_comment_char)
      return;
    if (first_arg.front() == CommandHistory::g_repeat_char) {
      if (auto hist_str = m_command_history.FindString(first_arg))
        request.AddCompletion(*hist_str, "Previous command history event",
                              CompletionMode::RewriteLine);
      return;
    }
  }

  HandleCompletionMatches(request);
}

std::optional<std::string>
CommandInterpreter::GetAutoSuggestionForCommand(llvm::StringRef line) {
  if (line.empty())
    return std::nullopt;
  const size_t s = m_command_history.GetSize();
  for (int i = s - 1; i >= 0; --i) {
    llvm::StringRef entry = m_command_history.GetStringAtIndex(i);
    if (entry.consume_front(line))
      return entry.str();
  }
  return std::nullopt;
}

void CommandInterpreter::UpdatePrompt(llvm::StringRef new_prompt) {
  EventSP prompt_change_event_sp(
      new Event(eBroadcastBitResetPrompt, new EventDataBytes(new_prompt)));
  ;
  BroadcastEvent(prompt_change_event_sp);
  if (m_command_io_handler_sp)
    m_command_io_handler_sp->SetPrompt(new_prompt);
}

bool CommandInterpreter::Confirm(llvm::StringRef message, bool default_answer) {
  // Check AutoConfirm first:
  if (m_debugger.GetAutoConfirm())
    return default_answer;

  IOHandlerConfirm *confirm =
      new IOHandlerConfirm(m_debugger, message, default_answer);
  IOHandlerSP io_handler_sp(confirm);
  m_debugger.RunIOHandlerSync(io_handler_sp);
  return confirm->GetResponse();
}

const CommandAlias *
CommandInterpreter::GetAlias(llvm::StringRef alias_name) const {
  OptionArgVectorSP ret_val;

  auto pos = m_alias_dict.find(std::string(alias_name));
  if (pos != m_alias_dict.end())
    return (CommandAlias *)pos->second.get();

  return nullptr;
}

bool CommandInterpreter::HasCommands() const { return (!m_command_dict.empty()); }

bool CommandInterpreter::HasAliases() const { return (!m_alias_dict.empty()); }

bool CommandInterpreter::HasUserCommands() const { return (!m_user_dict.empty()); }

bool CommandInterpreter::HasUserMultiwordCommands() const {
  return (!m_user_mw_dict.empty());
}

bool CommandInterpreter::HasAliasOptions() const { return HasAliases(); }

void CommandInterpreter::BuildAliasCommandArgs(CommandObject *alias_cmd_obj,
                                               const char *alias_name,
                                               Args &cmd_args,
                                               std::string &raw_input_string,
                                               CommandReturnObject &result) {
  OptionArgVectorSP option_arg_vector_sp =
      GetAlias(alias_name)->GetOptionArguments();

  bool wants_raw_input = alias_cmd_obj->WantsRawCommandString();

  // Make sure that the alias name is the 0th element in cmd_args
  std::string alias_name_str = alias_name;
  if (alias_name_str != cmd_args.GetArgumentAtIndex(0))
    cmd_args.Unshift(alias_name_str);

  Args new_args(alias_cmd_obj->GetCommandName());
  if (new_args.GetArgumentCount() == 2)
    new_args.Shift();

  if (option_arg_vector_sp.get()) {
    if (wants_raw_input) {
      // We have a command that both has command options and takes raw input.
      // Make *sure* it has a " -- " in the right place in the
      // raw_input_string.
      size_t pos = raw_input_string.find(" -- ");
      if (pos == std::string::npos) {
        // None found; assume it goes at the beginning of the raw input string
        raw_input_string.insert(0, " -- ");
      }
    }

    OptionArgVector *option_arg_vector = option_arg_vector_sp.get();
    const size_t old_size = cmd_args.GetArgumentCount();
    std::vector<bool> used(old_size + 1, false);

    used[0] = true;

    int value_type;
    std::string option;
    std::string value;
    for (const auto &option_entry : *option_arg_vector) {
      std::tie(option, value_type, value) = option_entry;
      if (option == g_argument) {
        if (!wants_raw_input || (value != "--")) {
          // Since we inserted this above, make sure we don't insert it twice
          new_args.AppendArgument(value);
        }
        continue;
      }

      if (value_type != OptionParser::eOptionalArgument)
        new_args.AppendArgument(option);

      if (value == g_no_argument)
        continue;

      int index = GetOptionArgumentPosition(value.c_str());
      if (index == 0) {
        // value was NOT a positional argument; must be a real value
        if (value_type != OptionParser::eOptionalArgument)
          new_args.AppendArgument(value);
        else {
          new_args.AppendArgument(option + value);
        }

      } else if (static_cast<size_t>(index) >= cmd_args.GetArgumentCount()) {
        result.AppendErrorWithFormat("Not enough arguments provided; you "
                                     "need at least %d arguments to use "
                                     "this alias.\n",
                                     index);
        return;
      } else {
        // Find and remove cmd_args.GetArgumentAtIndex(i) from raw_input_string
        size_t strpos =
            raw_input_string.find(cmd_args.GetArgumentAtIndex(index));
        if (strpos != std::string::npos) {
          raw_input_string = raw_input_string.erase(
              strpos, strlen(cmd_args.GetArgumentAtIndex(index)));
        }

        if (value_type != OptionParser::eOptionalArgument)
          new_args.AppendArgument(cmd_args.GetArgumentAtIndex(index));
        else {
          new_args.AppendArgument(option + cmd_args.GetArgumentAtIndex(index));
        }
        used[index] = true;
      }
    }

    for (auto entry : llvm::enumerate(cmd_args.entries())) {
      if (!used[entry.index()] && !wants_raw_input)
        new_args.AppendArgument(entry.value().ref());
    }

    cmd_args.Clear();
    cmd_args.SetArguments(new_args.GetArgumentCount(),
                          new_args.GetConstArgumentVector());
  } else {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    // This alias was not created with any options; nothing further needs to be
    // done, unless it is a command that wants raw input, in which case we need
    // to clear the rest of the data from cmd_args, since its in the raw input
    // string.
    if (wants_raw_input) {
      cmd_args.Clear();
      cmd_args.SetArguments(new_args.GetArgumentCount(),
                            new_args.GetConstArgumentVector());
    }
    return;
  }

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
}

int CommandInterpreter::GetOptionArgumentPosition(const char *in_string) {
  int position = 0; // Any string that isn't an argument position, i.e. '%'
                    // followed by an integer, gets a position
                    // of zero.

  const char *cptr = in_string;

  // Does it start with '%'
  if (cptr[0] == '%') {
    ++cptr;

    // Is the rest of it entirely digits?
    if (isdigit(cptr[0])) {
      const char *start = cptr;
      while (isdigit(cptr[0]))
        ++cptr;

      // We've gotten to the end of the digits; are we at the end of the
      // string?
      if (cptr[0] == '\0')
        position = atoi(start);
    }
  }

  return position;
}

static void GetHomeInitFile(llvm::SmallVectorImpl<char> &init_file,
                            llvm::StringRef suffix = {}) {
  std::string init_file_name = ".lldbinit";
  if (!suffix.empty()) {
    init_file_name.append("-");
    init_file_name.append(suffix.str());
  }

  FileSystem::Instance().GetHomeDirectory(init_file);
  llvm::sys::path::append(init_file, init_file_name);

  FileSystem::Instance().Resolve(init_file);
}

static void GetHomeREPLInitFile(llvm::SmallVectorImpl<char> &init_file,
                                LanguageType language) {
  if (language == eLanguageTypeUnknown) {
    LanguageSet repl_languages = Language::GetLanguagesSupportingREPLs();
    if (auto main_repl_language = repl_languages.GetSingularLanguage())
      language = *main_repl_language;
    else
      return;
  }

  std::string init_file_name =
      (llvm::Twine(".lldbinit-") +
       llvm::Twine(Language::GetNameForLanguageType(language)) +
       llvm::Twine("-repl"))
          .str();
  FileSystem::Instance().GetHomeDirectory(init_file);
  llvm::sys::path::append(init_file, init_file_name);
  FileSystem::Instance().Resolve(init_file);
}

static void GetCwdInitFile(llvm::SmallVectorImpl<char> &init_file) {
  llvm::StringRef s = ".lldbinit";
  init_file.assign(s.begin(), s.end());
  FileSystem::Instance().Resolve(init_file);
}

void CommandInterpreter::SourceInitFile(FileSpec file,
                                        CommandReturnObject &result) {
  assert(!m_skip_lldbinit_files);

  if (!FileSystem::Instance().Exists(file)) {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    return;
  }

  // Use HandleCommand to 'source' the given file; this will do the actual
  // broadcasting of the commands back to any appropriate listener (see
  // CommandObjectSource::Execute for more details).
  const bool saved_batch = SetBatchCommandMode(true);
  CommandInterpreterRunOptions options;
  options.SetSilent(true);
  options.SetPrintErrors(true);
  options.SetStopOnError(false);
  options.SetStopOnContinue(true);
  HandleCommandsFromFile(file, options, result);
  SetBatchCommandMode(saved_batch);
}

void CommandInterpreter::SourceInitFileCwd(CommandReturnObject &result) {
  if (m_skip_lldbinit_files) {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    return;
  }

  llvm::SmallString<128> init_file;
  GetCwdInitFile(init_file);
  if (!FileSystem::Instance().Exists(init_file)) {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    return;
  }

  LoadCWDlldbinitFile should_load =
      Target::GetGlobalProperties().GetLoadCWDlldbinitFile();

  switch (should_load) {
  case eLoadCWDlldbinitFalse:
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    break;
  case eLoadCWDlldbinitTrue:
    SourceInitFile(FileSpec(init_file.str()), result);
    break;
  case eLoadCWDlldbinitWarn: {
    llvm::SmallString<128> home_init_file;
    GetHomeInitFile(home_init_file);
    if (llvm::sys::path::parent_path(init_file) ==
        llvm::sys::path::parent_path(home_init_file)) {
      result.SetStatus(eReturnStatusSuccessFinishNoResult);
    } else {
      result.AppendError(InitFileWarning);
    }
  }
  }
}

/// We will first see if there is an application specific ".lldbinit" file
/// whose name is "~/.lldbinit" followed by a "-" and the name of the program.
/// If this file doesn't exist, we fall back to the REPL init file or the
/// default home init file in "~/.lldbinit".
void CommandInterpreter::SourceInitFileHome(CommandReturnObject &result,
                                            bool is_repl) {
  if (m_skip_lldbinit_files) {
    result.SetStatus(eReturnStatusSuccessFinishNoResult);
    return;
  }

  llvm::SmallString<128> init_file;

  if (is_repl)
    GetHomeREPLInitFile(init_file, GetDebugger().GetREPLLanguage());

  if (init_file.empty())
    GetHomeInitFile(init_file);

  if (!m_skip_app_init_files) {
    llvm::StringRef program_name =
        HostInfo::GetProgramFileSpec().GetFilename().GetStringRef();
    llvm::SmallString<128> program_init_file;
    GetHomeInitFile(program_init_file, program_name);
    if (FileSystem::Instance().Exists(program_init_file))
      init_file = program_init_file;
  }

  SourceInitFile(FileSpec(init_file.str()), result);
}

void CommandInterpreter::SourceInitFileGlobal(CommandReturnObject &result) {
#ifdef LLDB_GLOBAL_INIT_DIRECTORY
  if (!m_skip_lldbinit_files) {
    FileSpec init_file(LLDB_GLOBAL_INIT_DIRECTORY);
    if (init_file)
      init_file.MakeAbsolute(HostInfo::GetShlibDir());

    init_file.AppendPathComponent("lldbinit");
    SourceInitFile(init_file, result);
    return;
  }
#endif
  result.SetStatus(eReturnStatusSuccessFinishNoResult);
}

const char *CommandInterpreter::GetCommandPrefix() {
  const char *prefix = GetDebugger().GetIOHandlerCommandPrefix();
  return prefix == nullptr ? "" : prefix;
}

PlatformSP CommandInterpreter::GetPlatform(bool prefer_target_platform) {
  PlatformSP platform_sp;
  if (prefer_target_platform) {
    ExecutionContext exe_ctx(GetExecutionContext());
    Target *target = exe_ctx.GetTargetPtr();
    if (target)
      platform_sp = target->GetPlatform();
  }

  if (!platform_sp)
    platform_sp = m_debugger.GetPlatformList().GetSelectedPlatform();
  return platform_sp;
}

bool CommandInterpreter::DidProcessStopAbnormally() const {
  auto exe_ctx = GetExecutionContext();
  TargetSP target_sp = exe_ctx.GetTargetSP();
  if (!target_sp)
    return false;

  ProcessSP process_sp(target_sp->GetProcessSP());
  if (!process_sp)
    return false;

  if (eStateStopped != process_sp->GetState())
    return false;

  for (const auto &thread_sp : process_sp->GetThreadList().Threads()) {
    StopInfoSP stop_info = thread_sp->GetStopInfo();
    if (!stop_info) {
      // If there's no stop_info, keep iterating through the other threads;
      // it's enough that any thread has got a stop_info that indicates
      // an abnormal stop, to consider the process to be stopped abnormally.
      continue;
    }

    const StopReason reason = stop_info->GetStopReason();
    if (reason == eStopReasonException ||
        reason == eStopReasonInstrumentation ||
        reason == eStopReasonProcessorTrace)
      return true;

    if (reason == eStopReasonSignal) {
      const auto stop_signal = static_cast<int32_t>(stop_info->GetValue());
      UnixSignalsSP signals_sp = process_sp->GetUnixSignals();
      if (!signals_sp || !signals_sp->SignalIsValid(stop_signal))
        // The signal is unknown, treat it as abnormal.
        return true;

      const auto sigint_num = signals_sp->GetSignalNumberFromName("SIGINT");
      const auto sigstop_num = signals_sp->GetSignalNumberFromName("SIGSTOP");
      if ((stop_signal != sigint_num) && (stop_signal != sigstop_num))
        // The signal very likely implies a crash.
        return true;
    }
  }

  return false;
}

void
CommandInterpreter::HandleCommands(const StringList &commands,
                                   const ExecutionContext &override_context,
                                   const CommandInterpreterRunOptions &options,
                                   CommandReturnObject &result) {

  OverrideExecutionContext(override_context);
  HandleCommands(commands, options, result);
  RestoreExecutionContext();
}

void CommandInterpreter::HandleCommands(const StringList &commands,
                                        const CommandInterpreterRunOptions &options,
                                        CommandReturnObject &result) {
  size_t num_lines = commands.GetSize();

  // If we are going to continue past a "continue" then we need to run the
  // commands synchronously. Make sure you reset this value anywhere you return
  // from the function.

  bool old_async_execution = m_debugger.GetAsyncExecution();

  if (!options.GetStopOnContinue()) {
    m_debugger.SetAsyncExecution(false);
  }

  for (size_t idx = 0; idx < num_lines; idx++) {
    const char *cmd = commands.GetStringAtIndex(idx);
    if (cmd[0] == '\0')
      continue;

    if (options.GetEchoCommands()) {
      // TODO: Add Stream support.
      result.AppendMessageWithFormat("%s %s\n",
                                     m_debugger.GetPrompt().str().c_str(), cmd);
    }

    CommandReturnObject tmp_result(m_debugger.GetUseColor());
    tmp_result.SetInteractive(result.GetInteractive());
    tmp_result.SetSuppressImmediateOutput(true);

    // We might call into a regex or alias command, in which case the
    // add_to_history will get lost.  This m_command_source_depth dingus is the
    // way we turn off adding to the history in that case, so set it up here.
    if (!options.GetAddToHistory())
      m_command_source_depth++;
    bool success = HandleCommand(cmd, options.m_add_to_history, tmp_result);
    if (!options.GetAddToHistory())
      m_command_source_depth--;

    if (options.GetPrintResults()) {
      if (tmp_result.Succeeded())
        result.AppendMessage(tmp_result.GetOutputData());
    }

    if (!success || !tmp_result.Succeeded()) {
      llvm::StringRef error_msg = tmp_result.GetErrorData();
      if (error_msg.empty())
        error_msg = "<unknown error>.\n";
      if (options.GetStopOnError()) {
        result.AppendErrorWithFormat(
            "Aborting reading of commands after command #%" PRIu64
            ": '%s' failed with %s",
            (uint64_t)idx, cmd, error_msg.str().c_str());
        m_debugger.SetAsyncExecution(old_async_execution);
        return;
      } else if (options.GetPrintResults()) {
        result.AppendMessageWithFormat(
            "Command #%" PRIu64 " '%s' failed with %s", (uint64_t)idx + 1, cmd,
            error_msg.str().c_str());
      }
    }

    if (result.GetImmediateOutputStream())
      result.GetImmediateOutputStream()->Flush();

    if (result.GetImmediateErrorStream())
      result.GetImmediateErrorStream()->Flush();

    // N.B. Can't depend on DidChangeProcessState, because the state coming
    // into the command execution could be running (for instance in Breakpoint
    // Commands. So we check the return value to see if it is has running in
    // it.
    if ((tmp_result.GetStatus() == eReturnStatusSuccessContinuingNoResult) ||
        (tmp_result.GetStatus() == eReturnStatusSuccessContinuingResult)) {
      if (options.GetStopOnContinue()) {
        // If we caused the target to proceed, and we're going to stop in that
        // case, set the status in our real result before returning.  This is
        // an error if the continue was not the last command in the set of
        // commands to be run.
        if (idx != num_lines - 1)
          result.AppendErrorWithFormat(
              "Aborting reading of commands after command #%" PRIu64
              ": '%s' continued the target.\n",
              (uint64_t)idx + 1, cmd);
        else
          result.AppendMessageWithFormat("Command #%" PRIu64
                                         " '%s' continued the target.\n",
                                         (uint64_t)idx + 1, cmd);

        result.SetStatus(tmp_result.GetStatus());
        m_debugger.SetAsyncExecution(old_async_execution);

        return;
      }
    }

    // Also check for "stop on crash here:
    if (tmp_result.GetDidChangeProcessState() && options.GetStopOnCrash() &&
        DidProcessStopAbnormally()) {
      if (idx != num_lines - 1)
        result.AppendErrorWithFormat(
            "Aborting reading of commands after command #%" PRIu64
            ": '%s' stopped with a signal or exception.\n",
            (uint64_t)idx + 1, cmd);
      else
        result.AppendMessageWithFormat(
            "Command #%" PRIu64 " '%s' stopped with a signal or exception.\n",
            (uint64_t)idx + 1, cmd);

      result.SetStatus(tmp_result.GetStatus());
      m_debugger.SetAsyncExecution(old_async_execution);

      return;
    }
  }

  result.SetStatus(eReturnStatusSuccessFinishResult);
  m_debugger.SetAsyncExecution(old_async_execution);
}

// Make flags that we can pass into the IOHandler so our delegates can do the
// right thing
enum {
  eHandleCommandFlagStopOnContinue = (1u << 0),
  eHandleCommandFlagStopOnError = (1u << 1),
  eHandleCommandFlagEchoCommand = (1u << 2),
  eHandleCommandFlagEchoCommentCommand = (1u << 3),
  eHandleCommandFlagPrintResult = (1u << 4),
  eHandleCommandFlagPrintErrors = (1u << 5),
  eHandleCommandFlagStopOnCrash = (1u << 6),
  eHandleCommandFlagAllowRepeats = (1u << 7)
};

void CommandInterpreter::HandleCommandsFromFile(
    FileSpec &cmd_file, const ExecutionContext &context,
    const CommandInterpreterRunOptions &options, CommandReturnObject &result) {
  OverrideExecutionContext(context);
  HandleCommandsFromFile(cmd_file, options, result);
  RestoreExecutionContext();
}

void CommandInterpreter::HandleCommandsFromFile(FileSpec &cmd_file,
    const CommandInterpreterRunOptions &options, CommandReturnObject &result) {
  if (!FileSystem::Instance().Exists(cmd_file)) {
    result.AppendErrorWithFormat(
        "Error reading commands from file %s - file not found.\n",
        cmd_file.GetFilename().AsCString("<Unknown>"));
    return;
  }

  std::string cmd_file_path = cmd_file.GetPath();
  auto input_file_up =
      FileSystem::Instance().Open(cmd_file, File::eOpenOptionReadOnly);
  if (!input_file_up) {
    std::string error = llvm::toString(input_file_up.takeError());
    result.AppendErrorWithFormatv(
        "error: an error occurred read file '{0}': {1}\n", cmd_file_path,
        llvm::fmt_consume(input_file_up.takeError()));
    return;
  }
  FileSP input_file_sp = FileSP(std::move(input_file_up.get()));

  Debugger &debugger = GetDebugger();

  uint32_t flags = 0;

  if (options.m_stop_on_continue == eLazyBoolCalculate) {
    if (m_command_source_flags.empty()) {
      // Stop on continue by default
      flags |= eHandleCommandFlagStopOnContinue;
    } else if (m_command_source_flags.back() &
               eHandleCommandFlagStopOnContinue) {
      flags |= eHandleCommandFlagStopOnContinue;
    }
  } else if (options.m_stop_on_continue == eLazyBoolYes) {
    flags |= eHandleCommandFlagStopOnContinue;
  }

  if (options.m_stop_on_error == eLazyBoolCalculate) {
    if (m_command_source_flags.empty()) {
      if (GetStopCmdSourceOnError())
        flags |= eHandleCommandFlagStopOnError;
    } else if (m_command_source_flags.back() & eHandleCommandFlagStopOnError) {
      flags |= eHandleCommandFlagStopOnError;
    }
  } else if (options.m_stop_on_error == eLazyBoolYes) {
    flags |= eHandleCommandFlagStopOnError;
  }

  // stop-on-crash can only be set, if it is present in all levels of
  // pushed flag sets.
  if (options.GetStopOnCrash()) {
    if (m_command_source_flags.empty()) {
      flags |= eHandleCommandFlagStopOnCrash;
    } else if (m_command_source_flags.back() & eHandleCommandFlagStopOnCrash) {
      flags |= eHandleCommandFlagStopOnCrash;
    }
  }

  if (options.m_echo_commands == eLazyBoolCalculate) {
    if (m_command_source_flags.empty()) {
      // Echo command by default
      flags |= eHandleCommandFlagEchoCommand;
    } else if (m_command_source_flags.back() & eHandleCommandFlagEchoCommand) {
      flags |= eHandleCommandFlagEchoCommand;
    }
  } else if (options.m_echo_commands == eLazyBoolYes) {
    flags |= eHandleCommandFlagEchoCommand;
  }

  // We will only ever ask for this flag, if we echo commands in general.
  if (options.m_echo_comment_commands == eLazyBoolCalculate) {
    if (m_command_source_flags.empty()) {
      // Echo comments by default
      flags |= eHandleCommandFlagEchoCommentCommand;
    } else if (m_command_source_flags.back() &
               eHandleCommandFlagEchoCommentCommand) {
      flags |= eHandleCommandFlagEchoCommentCommand;
    }
  } else if (options.m_echo_comment_commands == eLazyBoolYes) {
    flags |= eHandleCommandFlagEchoCommentCommand;
  }

  if (options.m_print_results == eLazyBoolCalculate) {
    if (m_command_source_flags.empty()) {
      // Print output by default
      flags |= eHandleCommandFlagPrintResult;
    } else if (m_command_source_flags.back() & eHandleCommandFlagPrintResult) {
      flags |= eHandleCommandFlagPrintResult;
    }
  } else if (options.m_print_results == eLazyBoolYes) {
    flags |= eHandleCommandFlagPrintResult;
  }

  if (options.m_print_errors == eLazyBoolCalculate) {
    if (m_command_source_flags.empty()) {
      // Print output by default
      flags |= eHandleCommandFlagPrintErrors;
    } else if (m_command_source_flags.back() & eHandleCommandFlagPrintErrors) {
      flags |= eHandleCommandFlagPrintErrors;
    }
  } else if (options.m_print_errors == eLazyBoolYes) {
    flags |= eHandleCommandFlagPrintErrors;
  }

  if (flags & eHandleCommandFlagPrintResult) {
    debugger.GetOutputFile().Printf("Executing commands in '%s'.\n",
                                    cmd_file_path.c_str());
  }

  // Used for inheriting the right settings when "command source" might
  // have nested "command source" commands
  lldb::StreamFileSP empty_stream_sp;
  m_command_source_flags.push_back(flags);
  IOHandlerSP io_handler_sp(new IOHandlerEditline(
      debugger, IOHandler::Type::CommandInterpreter, input_file_sp,
      empty_stream_sp, // Pass in an empty stream so we inherit the top
                       // input reader output stream
      empty_stream_sp, // Pass in an empty stream so we inherit the top
                       // input reader error stream
      flags,
      nullptr, // Pass in NULL for "editline_name" so no history is saved,
               // or written
      debugger.GetPrompt(), llvm::StringRef(),
      false, // Not multi-line
      debugger.GetUseColor(), 0, *this));
  const bool old_async_execution = debugger.GetAsyncExecution();

  // Set synchronous execution if we are not stopping on continue
  if ((flags & eHandleCommandFlagStopOnContinue) == 0)
    debugger.SetAsyncExecution(false);

  m_command_source_depth++;
  m_command_source_dirs.push_back(cmd_file.CopyByRemovingLastPathComponent());

  debugger.RunIOHandlerSync(io_handler_sp);
  if (!m_command_source_flags.empty())
    m_command_source_flags.pop_back();

  m_command_source_dirs.pop_back();
  m_command_source_depth--;

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
  debugger.SetAsyncExecution(old_async_execution);
}

bool CommandInterpreter::GetSynchronous() { return m_synchronous_execution; }

void CommandInterpreter::SetSynchronous(bool value) {
  m_synchronous_execution = value;
}

void CommandInterpreter::OutputFormattedHelpText(Stream &strm,
                                                 llvm::StringRef prefix,
                                                 llvm::StringRef help_text) {
  const uint32_t max_columns = m_debugger.GetTerminalWidth();

  size_t line_width_max = max_columns - prefix.size();
  if (line_width_max < 16)
    line_width_max = help_text.size() + prefix.size();

  strm.IndentMore(prefix.size());
  bool prefixed_yet = false;
  // Even if we have no help text we still want to emit the command name.
  if (help_text.empty())
    help_text = "No help text";
  while (!help_text.empty()) {
    // Prefix the first line, indent subsequent lines to line up
    if (!prefixed_yet) {
      strm << prefix;
      prefixed_yet = true;
    } else
      strm.Indent();

    // Never print more than the maximum on one line.
    llvm::StringRef this_line = help_text.substr(0, line_width_max);

    // Always break on an explicit newline.
    std::size_t first_newline = this_line.find_first_of("\n");

    // Don't break on space/tab unless the text is too long to fit on one line.
    std::size_t last_space = llvm::StringRef::npos;
    if (this_line.size() != help_text.size())
      last_space = this_line.find_last_of(" \t");

    // Break at whichever condition triggered first.
    this_line = this_line.substr(0, std::min(first_newline, last_space));
    strm.PutCString(this_line);
    strm.EOL();

    // Remove whitespace / newlines after breaking.
    help_text = help_text.drop_front(this_line.size()).ltrim();
  }
  strm.IndentLess(prefix.size());
}

void CommandInterpreter::OutputFormattedHelpText(Stream &strm,
                                                 llvm::StringRef word_text,
                                                 llvm::StringRef separator,
                                                 llvm::StringRef help_text,
                                                 size_t max_word_len) {
  StreamString prefix_stream;
  prefix_stream.Printf("  %-*s %*s ", (int)max_word_len, word_text.data(),
                       (int)separator.size(), separator.data());
  OutputFormattedHelpText(strm, prefix_stream.GetString(), help_text);
}

void CommandInterpreter::OutputHelpText(Stream &strm, llvm::StringRef word_text,
                                        llvm::StringRef separator,
                                        llvm::StringRef help_text,
                                        uint32_t max_word_len) {
  int indent_size = max_word_len + separator.size() + 2;

  strm.IndentMore(indent_size);

  StreamString text_strm;
  text_strm.Printf("%-*s ", (int)max_word_len, word_text.data());
  text_strm << separator << " " << help_text;

  const uint32_t max_columns = m_debugger.GetTerminalWidth();

  llvm::StringRef text = text_strm.GetString();

  uint32_t chars_left = max_columns;

  auto nextWordLength = [](llvm::StringRef S) {
    size_t pos = S.find(' ');
    return pos == llvm::StringRef::npos ? S.size() : pos;
  };

  while (!text.empty()) {
    if (text.front() == '\n' ||
        (text.front() == ' ' && nextWordLength(text.ltrim(' ')) > chars_left)) {
      strm.EOL();
      strm.Indent();
      chars_left = max_columns - indent_size;
      if (text.front() == '\n')
        text = text.drop_front();
      else
        text = text.ltrim(' ');
    } else {
      strm.PutChar(text.front());
      --chars_left;
      text = text.drop_front();
    }
  }

  strm.EOL();
  strm.IndentLess(indent_size);
}

void CommandInterpreter::FindCommandsForApropos(
    llvm::StringRef search_word, StringList &commands_found,
    StringList &commands_help, const CommandObject::CommandMap &command_map) {
  for (const auto &pair : command_map) {
    llvm::StringRef command_name = pair.first;
    CommandObject *cmd_obj = pair.second.get();

    const bool search_short_help = true;
    const bool search_long_help = false;
    const bool search_syntax = false;
    const bool search_options = false;
    if (command_name.contains_insensitive(search_word) ||
        cmd_obj->HelpTextContainsWord(search_word, search_short_help,
                                      search_long_help, search_syntax,
                                      search_options)) {
      commands_found.AppendString(command_name);
      commands_help.AppendString(cmd_obj->GetHelp());
    }

    if (auto *multiword_cmd = cmd_obj->GetAsMultiwordCommand()) {
      StringList subcommands_found;
      FindCommandsForApropos(search_word, subcommands_found, commands_help,
                             multiword_cmd->GetSubcommandDictionary());
      for (const auto &subcommand_name : subcommands_found) {
        std::string qualified_name =
            (command_name + " " + subcommand_name).str();
        commands_found.AppendString(qualified_name);
      }
    }
  }
}

void CommandInterpreter::FindCommandsForApropos(llvm::StringRef search_word,
                                                StringList &commands_found,
                                                StringList &commands_help,
                                                bool search_builtin_commands,
                                                bool search_user_commands,
                                                bool search_alias_commands,
                                                bool search_user_mw_commands) {
  CommandObject::CommandMap::const_iterator pos;

  if (search_builtin_commands)
    FindCommandsForApropos(search_word, commands_found, commands_help,
                           m_command_dict);

  if (search_user_commands)
    FindCommandsForApropos(search_word, commands_found, commands_help,
                           m_user_dict);

  if (search_user_mw_commands)
    FindCommandsForApropos(search_word, commands_found, commands_help,
                           m_user_mw_dict);

  if (search_alias_commands)
    FindCommandsForApropos(search_word, commands_found, commands_help,
                           m_alias_dict);
}

ExecutionContext CommandInterpreter::GetExecutionContext() const {
  return !m_overriden_exe_contexts.empty()
             ? m_overriden_exe_contexts.top()
             : m_debugger.GetSelectedExecutionContext();
}

void CommandInterpreter::OverrideExecutionContext(
    const ExecutionContext &override_context) {
  m_overriden_exe_contexts.push(override_context);
}

void CommandInterpreter::RestoreExecutionContext() {
  if (!m_overriden_exe_contexts.empty())
    m_overriden_exe_contexts.pop();
}

void CommandInterpreter::GetProcessOutput() {
  if (ProcessSP process_sp = GetExecutionContext().GetProcessSP())
    m_debugger.FlushProcessOutput(*process_sp, /*flush_stdout*/ true,
                                  /*flush_stderr*/ true);
}

void CommandInterpreter::StartHandlingCommand() {
  auto idle_state = CommandHandlingState::eIdle;
  if (m_command_state.compare_exchange_strong(
          idle_state, CommandHandlingState::eInProgress))
    lldbassert(m_iohandler_nesting_level == 0);
  else
    lldbassert(m_iohandler_nesting_level > 0);
  ++m_iohandler_nesting_level;
}

void CommandInterpreter::FinishHandlingCommand() {
  lldbassert(m_iohandler_nesting_level > 0);
  if (--m_iohandler_nesting_level == 0) {
    auto prev_state = m_command_state.exchange(CommandHandlingState::eIdle);
    lldbassert(prev_state != CommandHandlingState::eIdle);
  }
}

bool CommandInterpreter::InterruptCommand() {
  auto in_progress = CommandHandlingState::eInProgress;
  return m_command_state.compare_exchange_strong(
      in_progress, CommandHandlingState::eInterrupted);
}

bool CommandInterpreter::WasInterrupted() const {
  if (!m_debugger.IsIOHandlerThreadCurrentThread())
    return false;

  bool was_interrupted =
      (m_command_state == CommandHandlingState::eInterrupted);
  lldbassert(!was_interrupted || m_iohandler_nesting_level > 0);
  return was_interrupted;
}

void CommandInterpreter::PrintCommandOutput(IOHandler &io_handler,
                                            llvm::StringRef str,
                                            bool is_stdout) {

  lldb::StreamFileSP stream = is_stdout ? io_handler.GetOutputStreamFileSP()
                                        : io_handler.GetErrorStreamFileSP();
  // Split the output into lines and poll for interrupt requests
  bool had_output = !str.empty();
  while (!str.empty()) {
    llvm::StringRef line;
    std::tie(line, str) = str.split('\n');
    {
      std::lock_guard<std::recursive_mutex> guard(io_handler.GetOutputMutex());
      stream->Write(line.data(), line.size());
      stream->Write("\n", 1);
    }
  }

  std::lock_guard<std::recursive_mutex> guard(io_handler.GetOutputMutex());
  if (had_output &&
      INTERRUPT_REQUESTED(GetDebugger(), "Interrupted dumping command output"))
    stream->Printf("\n... Interrupted.\n");
  stream->Flush();
}

bool CommandInterpreter::EchoCommandNonInteractive(
    llvm::StringRef line, const Flags &io_handler_flags) const {
  if (!io_handler_flags.Test(eHandleCommandFlagEchoCommand))
    return false;

  llvm::StringRef command = line.trim();
  if (command.empty())
    return true;

  if (command.front() == m_comment_char)
    return io_handler_flags.Test(eHandleCommandFlagEchoCommentCommand);

  return true;
}

void CommandInterpreter::IOHandlerInputComplete(IOHandler &io_handler,
                                                std::string &line) {
    // If we were interrupted, bail out...
    if (WasInterrupted())
      return;

  const bool is_interactive = io_handler.GetIsInteractive();
  const bool allow_repeats =
      io_handler.GetFlags().Test(eHandleCommandFlagAllowRepeats);

  if (!is_interactive && !allow_repeats) {
    // When we are not interactive, don't execute blank lines. This will happen
    // sourcing a commands file. We don't want blank lines to repeat the
    // previous command and cause any errors to occur (like redefining an
    // alias, get an error and stop parsing the commands file).
    // But obey the AllowRepeats flag if the user has set it.
    if (line.empty())
      return;
  }
  if (!is_interactive) {
    // When using a non-interactive file handle (like when sourcing commands
    // from a file) we need to echo the command out so we don't just see the
    // command output and no command...
    if (EchoCommandNonInteractive(line, io_handler.GetFlags())) {
      std::lock_guard<std::recursive_mutex> guard(io_handler.GetOutputMutex());
      io_handler.GetOutputStreamFileSP()->Printf(
          "%s%s\n", io_handler.GetPrompt(), line.c_str());
    }
  }

  StartHandlingCommand();

  ExecutionContext exe_ctx = m_debugger.GetSelectedExecutionContext();
  bool pushed_exe_ctx = false;
  if (exe_ctx.HasTargetScope()) {
    OverrideExecutionContext(exe_ctx);
    pushed_exe_ctx = true;
  }
  auto finalize = llvm::make_scope_exit([this, pushed_exe_ctx]() {
    if (pushed_exe_ctx)
      RestoreExecutionContext();
  });

  lldb_private::CommandReturnObject result(m_debugger.GetUseColor());
  HandleCommand(line.c_str(), eLazyBoolCalculate, result);

  // Now emit the command output text from the command we just executed
  if ((result.Succeeded() &&
       io_handler.GetFlags().Test(eHandleCommandFlagPrintResult)) ||
      io_handler.GetFlags().Test(eHandleCommandFlagPrintErrors)) {
    // Display any STDOUT/STDERR _prior_ to emitting the command result text
    GetProcessOutput();

    if (!result.GetImmediateOutputStream()) {
      llvm::StringRef output = result.GetOutputData();
      PrintCommandOutput(io_handler, output, true);
    }

    // Now emit the command error text from the command we just executed
    if (!result.GetImmediateErrorStream()) {
      llvm::StringRef error = result.GetErrorData();
      PrintCommandOutput(io_handler, error, false);
    }
  }

  FinishHandlingCommand();

  switch (result.GetStatus()) {
  case eReturnStatusInvalid:
  case eReturnStatusSuccessFinishNoResult:
  case eReturnStatusSuccessFinishResult:
  case eReturnStatusStarted:
    break;

  case eReturnStatusSuccessContinuingNoResult:
  case eReturnStatusSuccessContinuingResult:
    if (io_handler.GetFlags().Test(eHandleCommandFlagStopOnContinue))
      io_handler.SetIsDone(true);
    break;

  case eReturnStatusFailed:
    m_result.IncrementNumberOfErrors();
    if (io_handler.GetFlags().Test(eHandleCommandFlagStopOnError)) {
      m_result.SetResult(lldb::eCommandInterpreterResultCommandError);
      io_handler.SetIsDone(true);
    }
    break;

  case eReturnStatusQuit:
    m_result.SetResult(lldb::eCommandInterpreterResultQuitRequested);
    io_handler.SetIsDone(true);
    break;
  }

  // Finally, if we're going to stop on crash, check that here:
  if (m_result.IsResult(lldb::eCommandInterpreterResultSuccess) &&
      result.GetDidChangeProcessState() &&
      io_handler.GetFlags().Test(eHandleCommandFlagStopOnCrash) &&
      DidProcessStopAbnormally()) {
    io_handler.SetIsDone(true);
    m_result.SetResult(lldb::eCommandInterpreterResultInferiorCrash);
  }
}

bool CommandInterpreter::IOHandlerInterrupt(IOHandler &io_handler) {
  ExecutionContext exe_ctx(GetExecutionContext());
  Process *process = exe_ctx.GetProcessPtr();

  if (InterruptCommand())
    return true;

  if (process) {
    StateType state = process->GetState();
    if (StateIsRunningState(state)) {
      process->Halt();
      return true; // Don't do any updating when we are running
    }
  }

  ScriptInterpreter *script_interpreter =
      m_debugger.GetScriptInterpreter(false);
  if (script_interpreter) {
    if (script_interpreter->Interrupt())
      return true;
  }
  return false;
}

bool CommandInterpreter::SaveTranscript(
    CommandReturnObject &result, std::optional<std::string> output_file) {
  if (output_file == std::nullopt || output_file->empty()) {
    std::string now = llvm::to_string(std::chrono::system_clock::now());
    std::replace(now.begin(), now.end(), ' ', '_');
    // Can't have file name with colons on Windows
    std::replace(now.begin(), now.end(), ':', '-');
    const std::string file_name = "lldb_session_" + now + ".log";

    FileSpec save_location = GetSaveSessionDirectory();

    if (!save_location)
      save_location = HostInfo::GetGlobalTempDir();

    FileSystem::Instance().Resolve(save_location);
    save_location.AppendPathComponent(file_name);
    output_file = save_location.GetPath();
  }

  auto error_out = [&](llvm::StringRef error_message, std::string description) {
    LLDB_LOG(GetLog(LLDBLog::Commands), "{0} ({1}:{2})", error_message,
             output_file, description);
    result.AppendErrorWithFormatv(
        "Failed to save session's transcripts to {0}!", *output_file);
    return false;
  };

  File::OpenOptions flags = File::eOpenOptionWriteOnly |
                            File::eOpenOptionCanCreate |
                            File::eOpenOptionTruncate;

  auto opened_file = FileSystem::Instance().Open(FileSpec(*output_file), flags);

  if (!opened_file)
    return error_out("Unable to create file",
                     llvm::toString(opened_file.takeError()));

  FileUP file = std::move(opened_file.get());

  size_t byte_size = m_transcript_stream.GetSize();

  Status error = file->Write(m_transcript_stream.GetData(), byte_size);

  if (error.Fail() || byte_size != m_transcript_stream.GetSize())
    return error_out("Unable to write to destination file",
                     "Bytes written do not match transcript size.");

  result.SetStatus(eReturnStatusSuccessFinishNoResult);
  result.AppendMessageWithFormat("Session's transcripts saved to %s\n",
                                 output_file->c_str());

  if (GetOpenTranscriptInEditor() && Host::IsInteractiveGraphicSession()) {
    const FileSpec file_spec;
    error = file->GetFileSpec(const_cast<FileSpec &>(file_spec));
    if (error.Success()) {
      if (llvm::Error e = Host::OpenFileInExternalEditor(
              m_debugger.GetExternalEditor(), file_spec, 1))
        result.AppendError(llvm::toString(std::move(e)));
    }
  }

  return true;
}

bool CommandInterpreter::IsInteractive() {
  return (GetIOHandler() ? GetIOHandler()->GetIsInteractive() : false);
}

FileSpec CommandInterpreter::GetCurrentSourceDir() {
  if (m_command_source_dirs.empty())
    return {};
  return m_command_source_dirs.back();
}

void CommandInterpreter::GetLLDBCommandsFromIOHandler(
    const char *prompt, IOHandlerDelegate &delegate, void *baton) {
  Debugger &debugger = GetDebugger();
  IOHandlerSP io_handler_sp(
      new IOHandlerEditline(debugger, IOHandler::Type::CommandList,
                            "lldb", // Name of input reader for history
                            llvm::StringRef(prompt), // Prompt
                            llvm::StringRef(),       // Continuation prompt
                            true,                    // Get multiple lines
                            debugger.GetUseColor(),
                            0,          // Don't show line numbers
                            delegate)); // IOHandlerDelegate

  if (io_handler_sp) {
    io_handler_sp->SetUserData(baton);
    debugger.RunIOHandlerAsync(io_handler_sp);
  }
}

void CommandInterpreter::GetPythonCommandsFromIOHandler(
    const char *prompt, IOHandlerDelegate &delegate, void *baton) {
  Debugger &debugger = GetDebugger();
  IOHandlerSP io_handler_sp(
      new IOHandlerEditline(debugger, IOHandler::Type::PythonCode,
                            "lldb-python", // Name of input reader for history
                            llvm::StringRef(prompt), // Prompt
                            llvm::StringRef(),       // Continuation prompt
                            true,                    // Get multiple lines
                            debugger.GetUseColor(),
                            0,          // Don't show line numbers
                            delegate)); // IOHandlerDelegate

  if (io_handler_sp) {
    io_handler_sp->SetUserData(baton);
    debugger.RunIOHandlerAsync(io_handler_sp);
  }
}

bool CommandInterpreter::IsActive() {
  return m_debugger.IsTopIOHandler(m_command_io_handler_sp);
}

lldb::IOHandlerSP
CommandInterpreter::GetIOHandler(bool force_create,
                                 CommandInterpreterRunOptions *options) {
  // Always re-create the IOHandlerEditline in case the input changed. The old
  // instance might have had a non-interactive input and now it does or vice
  // versa.
  if (force_create || !m_command_io_handler_sp) {
    // Always re-create the IOHandlerEditline in case the input changed. The
    // old instance might have had a non-interactive input and now it does or
    // vice versa.
    uint32_t flags = 0;

    if (options) {
      if (options->m_stop_on_continue == eLazyBoolYes)
        flags |= eHandleCommandFlagStopOnContinue;
      if (options->m_stop_on_error == eLazyBoolYes)
        flags |= eHandleCommandFlagStopOnError;
      if (options->m_stop_on_crash == eLazyBoolYes)
        flags |= eHandleCommandFlagStopOnCrash;
      if (options->m_echo_commands != eLazyBoolNo)
        flags |= eHandleCommandFlagEchoCommand;
      if (options->m_echo_comment_commands != eLazyBoolNo)
        flags |= eHandleCommandFlagEchoCommentCommand;
      if (options->m_print_results != eLazyBoolNo)
        flags |= eHandleCommandFlagPrintResult;
      if (options->m_print_errors != eLazyBoolNo)
        flags |= eHandleCommandFlagPrintErrors;
      if (options->m_allow_repeats == eLazyBoolYes)
        flags |= eHandleCommandFlagAllowRepeats;
    } else {
      flags = eHandleCommandFlagEchoCommand | eHandleCommandFlagPrintResult |
              eHandleCommandFlagPrintErrors;
    }

    m_command_io_handler_sp = std::make_shared<IOHandlerEditline>(
        m_debugger, IOHandler::Type::CommandInterpreter,
        m_debugger.GetInputFileSP(), m_debugger.GetOutputStreamSP(),
        m_debugger.GetErrorStreamSP(), flags, "lldb", m_debugger.GetPrompt(),
        llvm::StringRef(), // Continuation prompt
        false, // Don't enable multiple line input, just single line commands
        m_debugger.GetUseColor(),
        0,      // Don't show line numbers
        *this); // IOHandlerDelegate
  }
  return m_command_io_handler_sp;
}

CommandInterpreterRunResult CommandInterpreter::RunCommandInterpreter(
    CommandInterpreterRunOptions &options) {
  // Always re-create the command interpreter when we run it in case any file
  // handles have changed.
  bool force_create = true;
  m_debugger.RunIOHandlerAsync(GetIOHandler(force_create, &options));
  m_result = CommandInterpreterRunResult();

  if (options.GetAutoHandleEvents())
    m_debugger.StartEventHandlerThread();

  if (options.GetSpawnThread()) {
    m_debugger.StartIOHandlerThread();
  } else {
    // If the current thread is not managed by a host thread, we won't detect
    // that this IS the CommandInterpreter IOHandler thread, so make it so:
    HostThread new_io_handler_thread(Host::GetCurrentThread());
    HostThread old_io_handler_thread =
        m_debugger.SetIOHandlerThread(new_io_handler_thread);
    m_debugger.RunIOHandlers();
    m_debugger.SetIOHandlerThread(old_io_handler_thread);

    if (options.GetAutoHandleEvents())
      m_debugger.StopEventHandlerThread();
  }

  return m_result;
}

CommandObject *
CommandInterpreter::ResolveCommandImpl(std::string &command_line,
                                       CommandReturnObject &result) {
  std::string scratch_command(command_line); // working copy so we don't modify
                                             // command_line unless we succeed
  CommandObject *cmd_obj = nullptr;
  StreamString revised_command_line;
  bool wants_raw_input = false;
  std::string next_word;
  StringList matches;
  bool done = false;
  while (!done) {
    char quote_char = '\0';
    std::string suffix;
    ExtractCommand(scratch_command, next_word, suffix, quote_char);
    if (cmd_obj == nullptr) {
      std::string full_name;
      bool is_alias = GetAliasFullName(next_word, full_name);
      cmd_obj = GetCommandObject(next_word, &matches);
      bool is_real_command =
          (!is_alias) || (cmd_obj != nullptr && !cmd_obj->IsAlias());
      if (!is_real_command) {
        matches.Clear();
        std::string alias_result;
        cmd_obj =
            BuildAliasResult(full_name, scratch_command, alias_result, result);
        revised_command_line.Printf("%s", alias_result.c_str());
        if (cmd_obj) {
          wants_raw_input = cmd_obj->WantsRawCommandString();
        }
      } else {
        if (cmd_obj) {
          llvm::StringRef cmd_name = cmd_obj->GetCommandName();
          revised_command_line.Printf("%s", cmd_name.str().c_str());
          wants_raw_input = cmd_obj->WantsRawCommandString();
        } else {
          revised_command_line.Printf("%s", next_word.c_str());
        }
      }
    } else {
      if (cmd_obj->IsMultiwordObject()) {
        CommandObject *sub_cmd_obj =
            cmd_obj->GetSubcommandObject(next_word.c_str());
        if (sub_cmd_obj) {
          // The subcommand's name includes the parent command's name, so
          // restart rather than append to the revised_command_line.
          llvm::StringRef sub_cmd_name = sub_cmd_obj->GetCommandName();
          revised_command_line.Clear();
          revised_command_line.Printf("%s", sub_cmd_name.str().c_str());
          cmd_obj = sub_cmd_obj;
          wants_raw_input = cmd_obj->WantsRawCommandString();
        } else {
          if (quote_char)
            revised_command_line.Printf(" %c%s%s%c", quote_char,
                                        next_word.c_str(), suffix.c_str(),
                                        quote_char);
          else
            revised_command_line.Printf(" %s%s", next_word.c_str(),
                                        suffix.c_str());
          done = true;
        }
      } else {
        if (quote_char)
          revised_command_line.Printf(" %c%s%s%c", quote_char,
                                      next_word.c_str(), suffix.c_str(),
                                      quote_char);
        else
          revised_command_line.Printf(" %s%s", next_word.c_str(),
                                      suffix.c_str());
        done = true;
      }
    }

    if (cmd_obj == nullptr) {
      const size_t num_matches = matches.GetSize();
      if (matches.GetSize() > 1) {
        StreamString error_msg;
        error_msg.Printf("Ambiguous command '%s'. Possible matches:\n",
                         next_word.c_str());

        for (uint32_t i = 0; i < num_matches; ++i) {
          error_msg.Printf("\t%s\n", matches.GetStringAtIndex(i));
        }
        result.AppendRawError(error_msg.GetString());
      } else {
        // We didn't have only one match, otherwise we wouldn't get here.
        lldbassert(num_matches == 0);
        result.AppendErrorWithFormat("'%s' is not a valid command.\n",
                                     next_word.c_str());
      }
      return nullptr;
    }

    if (cmd_obj->IsMultiwordObject()) {
      if (!suffix.empty()) {
        result.AppendErrorWithFormat(
            "command '%s' did not recognize '%s%s%s' as valid (subcommand "
            "might be invalid).\n",
            cmd_obj->GetCommandName().str().c_str(),
            next_word.empty() ? "" : next_word.c_str(),
            next_word.empty() ? " -- " : " ", suffix.c_str());
        return nullptr;
      }
    } else {
      // If we found a normal command, we are done
      done = true;
      if (!suffix.empty()) {
        switch (suffix[0]) {
        case '/':
          // GDB format suffixes
          {
            Options *command_options = cmd_obj->GetOptions();
            if (command_options &&
                command_options->SupportsLongOption("gdb-format")) {
              std::string gdb_format_option("--gdb-format=");
              gdb_format_option += (suffix.c_str() + 1);

              std::string cmd = std::string(revised_command_line.GetString());
              size_t arg_terminator_idx = FindArgumentTerminator(cmd);
              if (arg_terminator_idx != std::string::npos) {
                // Insert the gdb format option before the "--" that terminates
                // options
                gdb_format_option.append(1, ' ');
                cmd.insert(arg_terminator_idx, gdb_format_option);
                revised_command_line.Clear();
                revised_command_line.PutCString(cmd);
              } else
                revised_command_line.Printf(" %s", gdb_format_option.c_str());

              if (wants_raw_input &&
                  FindArgumentTerminator(cmd) == std::string::npos)
                revised_command_line.PutCString(" --");
            } else {
              result.AppendErrorWithFormat(
                  "the '%s' command doesn't support the --gdb-format option\n",
                  cmd_obj->GetCommandName().str().c_str());
              return nullptr;
            }
          }
          break;

        default:
          result.AppendErrorWithFormat(
              "unknown command shorthand suffix: '%s'\n", suffix.c_str());
          return nullptr;
        }
      }
    }
    if (scratch_command.empty())
      done = true;
  }

  if (!scratch_command.empty())
    revised_command_line.Printf(" %s", scratch_command.c_str());

  if (cmd_obj != nullptr)
    command_line = std::string(revised_command_line.GetString());

  return cmd_obj;
}

llvm::json::Value CommandInterpreter::GetStatistics() {
  llvm::json::Object stats;
  for (const auto &command_usage : m_command_usages)
    stats.try_emplace(command_usage.getKey(), command_usage.getValue());
  return stats;
}

const StructuredData::Array &CommandInterpreter::GetTranscript() const {
  return m_transcript;
}
