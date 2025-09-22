//===-- CommandObjectDWIMPrint.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTDWIMPRINT_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTDWIMPRINT_H

#include "CommandObjectExpression.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/OptionGroupFormat.h"
#include "lldb/Interpreter/OptionGroupValueObjectDisplay.h"
#include "lldb/Interpreter/OptionValueFormat.h"

namespace lldb_private {

/// Implements `dwim-print`, a printing command that chooses the most direct,
/// efficient, and resilient means of printing a given expression.
///
/// DWIM is an acronym for Do What I Mean. From Wikipedia, DWIM is described as:
///
///   > attempt to anticipate what users intend to do, correcting trivial errors
///   > automatically rather than blindly executing users' explicit but
///   > potentially incorrect input
///
/// The `dwim-print` command serves as a single print command for users who
/// don't yet know, or perfer not to know, the various lldb commands that can be
/// used to print, and when to use them.
class CommandObjectDWIMPrint : public CommandObjectRaw {
public:
  CommandObjectDWIMPrint(CommandInterpreter &interpreter);

  ~CommandObjectDWIMPrint() override = default;

  Options *GetOptions() override;

  bool WantsCompletion() override { return true; }

private:
  void DoExecute(llvm::StringRef command, CommandReturnObject &result) override;

  OptionGroupOptions m_option_group;
  OptionGroupFormat m_format_options = lldb::eFormatDefault;
  OptionGroupValueObjectDisplay m_varobj_options;
  CommandObjectExpression::CommandOptions m_expr_options;
};

} // namespace lldb_private

#endif
