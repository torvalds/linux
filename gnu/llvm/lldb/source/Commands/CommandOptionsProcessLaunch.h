//===-- CommandOptionsProcessLaunch.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOPTIONSPROCESSLAUNCH_H
#define LLDB_SOURCE_COMMANDS_COMMANDOPTIONSPROCESSLAUNCH_H

#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Interpreter/Options.h"

namespace lldb_private {

// CommandOptionsProcessLaunch

class CommandOptionsProcessLaunch : public lldb_private::OptionGroup {
public:
  CommandOptionsProcessLaunch() {
    // Keep default values of all options in one place: OptionParsingStarting
    // ()
    OptionParsingStarting(nullptr);
  }

  ~CommandOptionsProcessLaunch() override = default;

  lldb_private::Status
  SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                 lldb_private::ExecutionContext *execution_context) override;

  void OptionParsingStarting(
      lldb_private::ExecutionContext *execution_context) override {
    launch_info.Clear();
    disable_aslr = lldb_private::eLazyBoolCalculate;
  }

  llvm::ArrayRef<lldb_private::OptionDefinition> GetDefinitions() override;

  // Instance variables to hold the values for command options.

  lldb_private::ProcessLaunchInfo launch_info;
  lldb_private::LazyBool disable_aslr;
}; // CommandOptionsProcessLaunch

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOPTIONSPROCESSLAUNCH_H
