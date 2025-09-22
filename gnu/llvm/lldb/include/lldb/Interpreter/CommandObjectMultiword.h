//===-- CommandObjectMultiword.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_COMMANDOBJECTMULTIWORD_H
#define LLDB_INTERPRETER_COMMANDOBJECTMULTIWORD_H

#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Utility/CompletionRequest.h"
#include <optional>

namespace lldb_private {

// CommandObjectMultiword

class CommandObjectMultiword : public CommandObject {
  // These two want to iterate over the subcommand dictionary.
  friend class CommandInterpreter;
  friend class CommandObjectSyntax;

public:
  CommandObjectMultiword(CommandInterpreter &interpreter, const char *name,
                         const char *help = nullptr,
                         const char *syntax = nullptr, uint32_t flags = 0);

  ~CommandObjectMultiword() override;

  bool IsMultiwordObject() override { return true; }

  CommandObjectMultiword *GetAsMultiwordCommand() override { return this; }

  bool LoadSubCommand(llvm::StringRef cmd_name,
                      const lldb::CommandObjectSP &command_obj) override;

  llvm::Error LoadUserSubcommand(llvm::StringRef cmd_name,
                                 const lldb::CommandObjectSP &command_obj,
                                 bool can_replace) override;

  llvm::Error RemoveUserSubcommand(llvm::StringRef cmd_name, bool multiword_okay);

  void GenerateHelpText(Stream &output_stream) override;

  lldb::CommandObjectSP GetSubcommandSP(llvm::StringRef sub_cmd,
                                        StringList *matches = nullptr) override;

  lldb::CommandObjectSP GetSubcommandSPExact(llvm::StringRef sub_cmd) override;

  CommandObject *GetSubcommandObject(llvm::StringRef sub_cmd,
                                     StringList *matches = nullptr) override;

  bool WantsRawCommandString() override { return false; }

  void HandleCompletion(CompletionRequest &request) override;

  std::optional<std::string> GetRepeatCommand(Args &current_command_args,
                                              uint32_t index) override;

  void Execute(const char *args_string, CommandReturnObject &result) override;

  bool IsRemovable() const override { return m_can_be_removed; }

  void SetRemovable(bool removable) { m_can_be_removed = removable; }

protected:
  CommandObject::CommandMap &GetSubcommandDictionary() {
    return m_subcommand_dict;
  }

  CommandObject::CommandMap m_subcommand_dict;
  bool m_can_be_removed;
};

class CommandObjectProxy : public CommandObject {
public:
  CommandObjectProxy(CommandInterpreter &interpreter, const char *name,
                     const char *help = nullptr, const char *syntax = nullptr,
                     uint32_t flags = 0);

  ~CommandObjectProxy() override;

  // Subclasses must provide a command object that will be transparently used
  // for this object.
  virtual CommandObject *GetProxyCommandObject() = 0;

  llvm::StringRef GetSyntax() override;

  llvm::StringRef GetHelp() override;

  llvm::StringRef GetHelpLong() override;

  bool IsRemovable() const override;

  bool IsMultiwordObject() override;

  CommandObjectMultiword *GetAsMultiwordCommand() override;

  void GenerateHelpText(Stream &result) override;

  lldb::CommandObjectSP GetSubcommandSP(llvm::StringRef sub_cmd,
                                        StringList *matches = nullptr) override;

  CommandObject *GetSubcommandObject(llvm::StringRef sub_cmd,
                                     StringList *matches = nullptr) override;

  bool LoadSubCommand(llvm::StringRef cmd_name,
                      const lldb::CommandObjectSP &command_obj) override;

  bool WantsRawCommandString() override;

  bool WantsCompletion() override;

  Options *GetOptions() override;

  void HandleCompletion(CompletionRequest &request) override;

  void
  HandleArgumentCompletion(CompletionRequest &request,
                           OptionElementVector &opt_element_vector) override;

  std::optional<std::string> GetRepeatCommand(Args &current_command_args,
                                              uint32_t index) override;

  /// \return
  ///     An error message to be displayed when the command is executed (i.e.
  ///     Execute is called) and \a GetProxyCommandObject returned null.
  virtual llvm::StringRef GetUnsupportedError();

  void Execute(const char *args_string, CommandReturnObject &result) override;

protected:
  // These two want to iterate over the subcommand dictionary.
  friend class CommandInterpreter;
  friend class CommandObjectSyntax;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_COMMANDOBJECTMULTIWORD_H
