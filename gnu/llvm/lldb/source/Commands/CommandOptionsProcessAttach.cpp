//===-- CommandOptionsProcessAttach.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandOptionsProcessAttach.h"

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

#define LLDB_OPTIONS_process_attach
#include "CommandOptions.inc"

Status CommandOptionsProcessAttach::SetOptionValue(
    uint32_t option_idx, llvm::StringRef option_arg,
    ExecutionContext *execution_context) {
  Status error;
  const int short_option = g_process_attach_options[option_idx].short_option;
  switch (short_option) {
  case 'c':
    attach_info.SetContinueOnceAttached(true);
    break;

  case 'p': {
    lldb::pid_t pid;
    if (option_arg.getAsInteger(0, pid)) {
      error.SetErrorStringWithFormat("invalid process ID '%s'",
                                     option_arg.str().c_str());
    } else {
      attach_info.SetProcessID(pid);
    }
  } break;

  case 'P':
    attach_info.SetProcessPluginName(option_arg);
    break;

  case 'n':
    attach_info.GetExecutableFile().SetFile(option_arg,
                                            FileSpec::Style::native);
    break;

  case 'w':
    attach_info.SetWaitForLaunch(true);
    break;

  case 'i':
    attach_info.SetIgnoreExisting(false);
    break;

  default:
    llvm_unreachable("Unimplemented option");
  }
  return error;
}

llvm::ArrayRef<OptionDefinition> CommandOptionsProcessAttach::GetDefinitions() {
  return llvm::ArrayRef(g_process_attach_options);
}
