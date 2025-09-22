//===-- CommandObjectVersion.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTVERSION_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTVERSION_H

#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

// CommandObjectVersion

class CommandObjectVersion : public CommandObjectParsed {
public:
  CommandObjectVersion(CommandInterpreter &interpreter);

  ~CommandObjectVersion() override;

protected:
  void DoExecute(Args &args, CommandReturnObject &result) override;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTVERSION_H
