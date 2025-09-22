//===-- CommandObjectDiagnostics.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTDIAGNOSTICS_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTDIAGNOSTICS_H

#include "lldb/Interpreter/CommandObjectMultiword.h"

namespace lldb_private {

class CommandObjectDiagnostics : public CommandObjectMultiword {
public:
  CommandObjectDiagnostics(CommandInterpreter &interpreter);
  ~CommandObjectDiagnostics() override;

private:
  CommandObjectDiagnostics(const CommandObjectDiagnostics &) = delete;
  const CommandObjectDiagnostics &
  operator=(const CommandObjectDiagnostics &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTDIAGNOSTICS_H
