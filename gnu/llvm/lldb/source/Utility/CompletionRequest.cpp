//===-- CompletionRequest.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/CompletionRequest.h"

using namespace lldb;
using namespace lldb_private;

CompletionRequest::CompletionRequest(llvm::StringRef command_line,
                                     unsigned raw_cursor_pos,
                                     CompletionResult &result)
    : m_command(command_line), m_raw_cursor_pos(raw_cursor_pos),
      m_result(result) {
  assert(raw_cursor_pos <= command_line.size() && "Out of bounds cursor?");

  // We parse the argument up to the cursor, so the last argument in
  // parsed_line is the one containing the cursor, and the cursor is after the
  // last character.
  llvm::StringRef partial_command(command_line.substr(0, raw_cursor_pos));
  m_parsed_line = Args(partial_command);

  if (GetParsedLine().GetArgumentCount() == 0) {
    m_cursor_index = 0;
    m_cursor_char_position = 0;
  } else {
    m_cursor_index = GetParsedLine().GetArgumentCount() - 1U;
    m_cursor_char_position =
        strlen(GetParsedLine().GetArgumentAtIndex(m_cursor_index));
  }

  // The cursor is after a space but the space is not part of the argument.
  // Let's add an empty fake argument to the end to make sure the completion
  // code. Note: The space could be part of the last argument when it's quoted.
  if (partial_command.ends_with(" ") &&
      !GetCursorArgumentPrefix().ends_with(" "))
    AppendEmptyArgument();
}

std::string CompletionResult::Completion::GetUniqueKey() const {

  // We build a unique key for this pair of completion:description. We
  // prefix the key with the length of the completion string. This prevents
  // that we could get any collisions from completions pairs such as these:
  // "foo:", "bar" would be "foo:bar", but will now be: "4foo:bar"
  // "foo", ":bar" would be "foo:bar", but will now be: "3foo:bar"

  std::string result;
  result.append(std::to_string(m_completion.size()));
  result.append(m_completion);
  result.append(std::to_string(static_cast<int>(m_mode)));
  result.append(":");
  result.append(m_descripton);
  return result;
}

void CompletionResult::AddResult(llvm::StringRef completion,
                                 llvm::StringRef description,
                                 CompletionMode mode) {
  Completion r(completion, description, mode);

  // Add the completion if we haven't seen the same value before.
  if (m_added_values.insert(r.GetUniqueKey()).second)
    m_results.push_back(r);
}

void CompletionResult::GetMatches(StringList &matches) const {
  matches.Clear();
  for (const Completion &completion : m_results)
    matches.AppendString(completion.GetCompletion());
}

void CompletionResult::GetDescriptions(StringList &descriptions) const {
  descriptions.Clear();
  for (const Completion &completion : m_results)
    descriptions.AppendString(completion.GetDescription());
}
