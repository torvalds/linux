//===-- CommandObjectScripting.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_INTERPRETER_COMMANDOBJECTSCRIPTING_H
#define LLDB_SOURCE_INTERPRETER_COMMANDOBJECTSCRIPTING_H

#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {

class CommandObjectMultiwordScripting : public CommandObjectMultiword {
public:
  CommandObjectMultiwordScripting(CommandInterpreter &interpreter);

  ~CommandObjectMultiwordScripting() override;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_INTERPRETER_COMMANDOBJECTSCRIPTING_H
