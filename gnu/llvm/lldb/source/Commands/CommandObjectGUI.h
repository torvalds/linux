//===-- CommandObjectGUI.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTGUI_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTGUI_H

#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

// CommandObjectGUI

class CommandObjectGUI : public CommandObjectParsed {
public:
  CommandObjectGUI(CommandInterpreter &interpreter);

  ~CommandObjectGUI() override;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTGUI_H
