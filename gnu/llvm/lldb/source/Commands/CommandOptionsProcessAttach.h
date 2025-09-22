//===-- CommandOptionsProcessAttach.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOPTIONSPROCESSATTACH_H
#define LLDB_SOURCE_COMMANDS_COMMANDOPTIONSPROCESSATTACH_H

#include "lldb/Interpreter/Options.h"
#include "lldb/Target/Process.h"

namespace lldb_private {

// CommandOptionsProcessAttach

class CommandOptionsProcessAttach : public lldb_private::OptionGroup {
public:
  CommandOptionsProcessAttach() {
    // Keep default values of all options in one place: OptionParsingStarting
    // ()
    OptionParsingStarting(nullptr);
  }

  ~CommandOptionsProcessAttach() override = default;

  lldb_private::Status
  SetOptionValue(uint32_t option_idx, llvm::StringRef option_arg,
                 lldb_private::ExecutionContext *execution_context) override;

  void OptionParsingStarting(
      lldb_private::ExecutionContext *execution_context) override {
    attach_info.Clear();
  }

  llvm::ArrayRef<lldb_private::OptionDefinition> GetDefinitions() override;

  // Instance variables to hold the values for command options.

  lldb_private::ProcessAttachInfo attach_info;
}; // CommandOptionsProcessAttach

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOPTIONSPROCESSATTACH_H
