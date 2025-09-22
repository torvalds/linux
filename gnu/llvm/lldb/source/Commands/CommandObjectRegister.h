//===-- CommandObjectRegister.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTREGISTER_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTREGISTER_H

#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {

// CommandObjectRegister

class CommandObjectRegister : public CommandObjectMultiword {
public:
  // Constructors and Destructors
  CommandObjectRegister(CommandInterpreter &interpreter);

  ~CommandObjectRegister() override;

private:
  // For CommandObjectRegister only
  CommandObjectRegister(const CommandObjectRegister &) = delete;
  const CommandObjectRegister &
  operator=(const CommandObjectRegister &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTREGISTER_H
