//===-- CommandObjectRegexCommand.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectRegexCommand.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

using namespace lldb;
using namespace lldb_private;

// CommandObjectRegexCommand constructor
CommandObjectRegexCommand::CommandObjectRegexCommand(
    CommandInterpreter &interpreter, llvm::StringRef name, llvm::StringRef help,
    llvm::StringRef syntax, uint32_t completion_type_mask, bool is_removable)
    : CommandObjectRaw(interpreter, name, help, syntax),
      m_completion_type_mask(completion_type_mask),
      m_is_removable(is_removable) {}

// Destructor
CommandObjectRegexCommand::~CommandObjectRegexCommand() = default;

llvm::Expected<std::string> CommandObjectRegexCommand::SubstituteVariables(
    llvm::StringRef input,
    const llvm::SmallVectorImpl<llvm::StringRef> &replacements) {
  std::string buffer;
  llvm::raw_string_ostream output(buffer);

  llvm::SmallVector<llvm::StringRef, 4> parts;
  input.split(parts, '%');

  output << parts[0];
  for (llvm::StringRef part : drop_begin(parts)) {
    size_t idx = 0;
    if (part.consumeInteger(10, idx))
      output << '%';
    else if (idx < replacements.size())
      output << replacements[idx];
    else
      return llvm::make_error<llvm::StringError>(
          llvm::formatv("%{0} is out of range: not enough arguments specified",
                        idx),
          llvm::errc::invalid_argument);
    output << part;
  }

  return output.str();
}

void CommandObjectRegexCommand::DoExecute(llvm::StringRef command,
                                          CommandReturnObject &result) {
  EntryCollection::const_iterator pos, end = m_entries.end();
  for (pos = m_entries.begin(); pos != end; ++pos) {
    llvm::SmallVector<llvm::StringRef, 4> matches;
    if (pos->regex.Execute(command, &matches)) {
      llvm::Expected<std::string> new_command =
          SubstituteVariables(pos->command, matches);
      if (!new_command) {
        result.SetError(new_command.takeError());
        return;
      }

      // Interpret the new command and return this as the result!
      if (m_interpreter.GetExpandRegexAliases())
        result.GetOutputStream().Printf("%s\n", new_command->c_str());
      // We don't have to pass an override_context here, as the command that 
      // called us should have set up the context appropriately.
      bool force_repeat_command = true;
      m_interpreter.HandleCommand(new_command->c_str(), eLazyBoolNo, result,
                                  force_repeat_command);
      return;
    }
  }
  result.SetStatus(eReturnStatusFailed);
  if (!GetSyntax().empty())
    result.AppendError(GetSyntax());
  else
    result.GetErrorStream() << "Command contents '" << command
                            << "' failed to match any "
                               "regular expression in the '"
                            << m_cmd_name << "' regex ";
}

bool CommandObjectRegexCommand::AddRegexCommand(llvm::StringRef re_cstr,
                                                llvm::StringRef command_cstr) {
  m_entries.resize(m_entries.size() + 1);
  // Only add the regular expression if it compiles
  m_entries.back().regex = RegularExpression(re_cstr);
  if (m_entries.back().regex.IsValid()) {
    m_entries.back().command = command_cstr.str();
    return true;
  }
  // The regex didn't compile...
  m_entries.pop_back();
  return false;
}

void CommandObjectRegexCommand::HandleCompletion(CompletionRequest &request) {
  if (m_completion_type_mask) {
    lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
        GetCommandInterpreter(), m_completion_type_mask, request, nullptr);
  }
}
