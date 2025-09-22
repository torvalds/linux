//===-- CommandAlias.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_COMMANDALIAS_H
#define LLDB_INTERPRETER_COMMANDALIAS_H

#include <memory>

#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/CompletionRequest.h"
#include "lldb/lldb-forward.h"

namespace lldb_private {
class CommandAlias : public CommandObject {
public:
  typedef std::unique_ptr<CommandAlias> UniquePointer;

  CommandAlias(CommandInterpreter &interpreter, lldb::CommandObjectSP cmd_sp,
               llvm::StringRef options_args, llvm::StringRef name,
               llvm::StringRef help = llvm::StringRef(),
               llvm::StringRef syntax = llvm::StringRef(), uint32_t flags = 0);

  void GetAliasExpansion(StreamString &help_string) const;

  bool IsValid() const { return m_underlying_command_sp && m_option_args_sp; }

  explicit operator bool() const { return IsValid(); }

  bool WantsRawCommandString() override;

  bool WantsCompletion() override;

  void HandleCompletion(CompletionRequest &request) override;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override;

  Options *GetOptions() override;

  bool IsAlias() override { return true; }

  bool IsDashDashCommand() override;

  llvm::StringRef GetHelp() override;

  llvm::StringRef GetHelpLong() override;

  void SetHelp(llvm::StringRef str) override;

  void SetHelpLong(llvm::StringRef str) override;

  void Execute(const char *args_string, CommandReturnObject &result) override;

  lldb::CommandObjectSP GetUnderlyingCommand() {
    return m_underlying_command_sp;
  }
  OptionArgVectorSP GetOptionArguments() const { return m_option_args_sp; }
  const char *GetOptionString() { return m_option_string.c_str(); }

  // this takes an alias - potentially nested (i.e. an alias to an alias) and
  // expands it all the way to a non-alias command
  std::pair<lldb::CommandObjectSP, OptionArgVectorSP> Desugar();

protected:
  bool IsNestedAlias();

private:
  lldb::CommandObjectSP m_underlying_command_sp;
  std::string m_option_string;
  OptionArgVectorSP m_option_args_sp;
  LazyBool m_is_dashdash_alias;
  bool m_did_set_help : 1;
  bool m_did_set_help_long : 1;
};
} // namespace lldb_private

#endif // LLDB_INTERPRETER_COMMANDALIAS_H
