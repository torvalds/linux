//===-- CommandObjectQuit.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTQUIT_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTQUIT_H

#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

// CommandObjectQuit

class CommandObjectQuit : public CommandObjectParsed {
public:
  CommandObjectQuit(CommandInterpreter &interpreter);

  ~CommandObjectQuit() override;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override;

  bool ShouldAskForConfirmation(bool &is_a_detach);
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTQUIT_H
