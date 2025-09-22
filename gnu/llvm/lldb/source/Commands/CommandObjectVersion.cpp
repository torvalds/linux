//===-- CommandObjectVersion.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectVersion.h"

#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Version/Version.h"

using namespace lldb;
using namespace lldb_private;

// CommandObjectVersion

CommandObjectVersion::CommandObjectVersion(CommandInterpreter &interpreter)
    : CommandObjectParsed(interpreter, "version",
                          "Show the LLDB debugger version.", "version") {}

CommandObjectVersion::~CommandObjectVersion() = default;

void CommandObjectVersion::DoExecute(Args &args, CommandReturnObject &result) {
  result.AppendMessageWithFormat("%s\n", lldb_private::GetVersion());
  result.SetStatus(eReturnStatusSuccessFinishResult);
}
