//===-- CommandObjectLanguage.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTLANGUAGE_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTLANGUAGE_H

#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {
class CommandObjectLanguage : public CommandObjectMultiword {
public:
  CommandObjectLanguage(CommandInterpreter &interpreter);

  ~CommandObjectLanguage() override;

protected:
  void DoExecute(Args &command, CommandReturnObject &result);
};
} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTLANGUAGE_H
