//===-- CommandObjectApropos.h -----------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTAPROPOS_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTAPROPOS_H

#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

// CommandObjectApropos

class CommandObjectApropos : public CommandObjectParsed {
public:
  CommandObjectApropos(CommandInterpreter &interpreter);

  ~CommandObjectApropos() override;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTAPROPOS_H
