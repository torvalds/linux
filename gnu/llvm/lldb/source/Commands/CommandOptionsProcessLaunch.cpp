//===-- CommandOptionsProcessLaunch.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandOptionsProcessLaunch.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/OptionParser.h"
#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandOptionArgumentTable.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Platform.h"
#include "lldb/Target/Target.h"

#include "llvm/ADT/ArrayRef.h"

using namespace llvm;
using namespace lldb;
using namespace lldb_private;

#define LLDB_OPTIONS_process_launch
#include "CommandOptions.inc"

Status CommandOptionsProcessLaunch::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;
  const int short_option = g_process_launch_options[option_idx].short_option;

  TargetSP target_sp =
      execution_context ? execution_context->GetTargetSP() : TargetSP();
  switch (short_option) {
  case 's': // Stop at program entry point
    launch_info.GetFlags().Set(eLaunchFlagStopAtEntry);
    break;
  case 'm': // Stop at user entry point
    target_sp->CreateBreakpointAtUserEntry(error);
    break;
  case 'i': // STDIN for read only
  {
    FileAction action;
    if (action.Open(STDIN_FILENO, FileSpec(option_arg), true, false))
      launch_info.AppendFileAction(action);
    break;
  }

  case 'o': // Open STDOUT for write only
  {
    FileAction action;
    if (action.Open(STDOUT_FILENO, FileSpec(option_arg), false, true))
      launch_info.AppendFileAction(action);
    break;
  }

  case 'e': // STDERR for write only
  {
    FileAction action;
    if (action.Open(STDERR_FILENO, FileSpec(option_arg), false, true))
      launch_info.AppendFileAction(action);
    break;
  }

  case 'P': // Process plug-in name
    launch_info.SetProcessPluginName(option_arg);
    break;

  case 'n': // Disable STDIO
  {
    FileAction action;
    const FileSpec dev_null(FileSystem::DEV_NULL);
    if (action.Open(STDIN_FILENO, dev_null, true, false))
      launch_info.AppendFileAction(action);
    if (action.Open(STDOUT_FILENO, dev_null, false, true))
      launch_info.AppendFileAction(action);
    if (action.Open(STDERR_FILENO, dev_null, false, true))
      launch_info.AppendFileAction(action);
    break;
  }

  case 'w':
    launch_info.SetWorkingDirectory(FileSpec(option_arg));
    break;

  case 't': // Open process in new terminal window
    launch_info.GetFlags().Set(eLaunchFlagLaunchInTTY);
    break;

  case 'a': {
    PlatformSP platform_sp =
        target_sp ? target_sp->GetPlatform() : PlatformSP();
    launch_info.GetArchitecture() =
        Platform::GetAugmentedArchSpec(platform_sp.get(), option_arg);
  } break;

  case 'A': // Disable ASLR.
  {
    bool success;
    const bool disable_aslr_arg =
        OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      disable_aslr = disable_aslr_arg ? eLazyBoolYes : eLazyBoolNo;
    else
      error.SetErrorStringWithFormat(
          "Invalid boolean value for disable-aslr option: '%s'",
          option_arg.empty() ? "<null>" : option_arg.str().c_str());
    break;
  }

  case 'X': // shell expand args.
  {
    bool success;
    const bool expand_args =
        OptionArgParser::ToBoolean(option_arg, true, &success);
    if (success)
      launch_info.SetShellExpandArguments(expand_args);
    else
      error.SetErrorStringWithFormat(
          "Invalid boolean value for shell-expand-args option: '%s'",
          option_arg.empty() ? "<null>" : option_arg.str().c_str());
    break;
  }

  case 'c':
    if (!option_arg.empty())
      launch_info.SetShell(FileSpec(option_arg));
    else
      launch_info.SetShell(HostInfo::GetDefaultShell());
    break;

  case 'E':
    launch_info.GetEnvironment().insert(option_arg);
    break;

  default:
    error.SetErrorStringWithFormat("unrecognized short option character '%c'",
                                   short_option);
    break;
  }
  return error;
}

llvm::ArrayRef<OptionDefinition> CommandOptionsProcessLaunch::GetDefinitions() {
  return llvm::ArrayRef(g_process_launch_options);
}
