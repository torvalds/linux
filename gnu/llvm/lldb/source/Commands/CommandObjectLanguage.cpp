//===-- CommandObjectLanguage.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectLanguage.h"



#include "lldb/Target/LanguageRuntime.h"

using namespace lldb;
using namespace lldb_private;

CommandObjectLanguage::CommandObjectLanguage(CommandInterpreter &interpreter)
    : CommandObjectMultiword(
          interpreter, "language", "Commands specific to a source language.",
          "language <language-name> <subcommand> [<subcommand-options>]") {
  // Let the LanguageRuntime populates this command with subcommands
  LanguageRuntime::InitializeCommands(this);
}

CommandObjectLanguage::~CommandObjectLanguage() = default;
