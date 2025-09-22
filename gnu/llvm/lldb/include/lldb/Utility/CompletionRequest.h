//===-- CompletionRequest.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_COMPLETIONREQUEST_H
#define LLDB_UTILITY_COMPLETIONREQUEST_H

#include "lldb/Utility/Args.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/StringList.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"

namespace lldb_private {
enum class CompletionMode {
  /// The current token has been completed. The client should indicate this
  /// to the user (usually this is done by adding a trailing space behind the
  /// token).
  /// Example: "command sub" -> "command subcommand " (note the trailing space).
  Normal,
  /// The current token has been partially completed. This means that we found
  /// a completion, but that the token is still incomplete. Examples
  /// for this are file paths, where we want to complete "/bi" to "/bin/", but
  /// the file path token is still incomplete after the completion. Clients
  /// should not indicate to the user that this is a full completion (e.g. by
  /// not inserting the usual trailing space after a successful completion).
  /// Example: "file /us" -> "file /usr/" (note the missing trailing space).
  Partial,
  /// The full line has been rewritten by the completion.
  /// Example: "alias name" -> "other_command full_name".
  RewriteLine,
};

class CompletionResult {
public:
  /// A single completion and all associated data.
  class Completion {

    /// The actual text that should be completed. The meaning of this text
    /// is defined by the CompletionMode.
    /// \see m_mode
    std::string m_completion;
    /// The description that should be displayed to the user alongside the
    /// completion text.
    std::string m_descripton;
    CompletionMode m_mode;

  public:
    Completion(llvm::StringRef completion, llvm::StringRef description,
               CompletionMode mode)
        : m_completion(completion.str()), m_descripton(description.str()),
          m_mode(mode) {}
    const std::string &GetCompletion() const { return m_completion; }
    const std::string &GetDescription() const { return m_descripton; }
    CompletionMode GetMode() const { return m_mode; }

    /// Generates a string that uniquely identifies this completion result.
    std::string GetUniqueKey() const;
  };

private:
  /// List of found completions.
  std::vector<Completion> m_results;

  /// A set of the unique keys of all found completions so far. Used to filter
  /// out duplicates.
  /// \see CompletionResult::Completion::GetUniqueKey
  llvm::StringSet<> m_added_values;

public:
  void AddResult(llvm::StringRef completion, llvm::StringRef description,
                 CompletionMode mode);

  llvm::ArrayRef<Completion> GetResults() const { return m_results; }

  /// Adds all collected completion matches to the given list.
  /// The list will be cleared before the results are added. The number of
  /// results here is guaranteed to be equal to GetNumberOfResults().
  void GetMatches(StringList &matches) const;

  /// Adds all collected completion descriptions to the given list.
  /// The list will be cleared before the results are added. The number of
  /// results here is guaranteed to be equal to GetNumberOfResults().
  void GetDescriptions(StringList &descriptions) const;

  std::size_t GetNumberOfResults() const { return m_results.size(); }
};

/// \class CompletionRequest CompletionRequest.h
///   "lldb/Utility/ArgCompletionRequest.h"
///
/// Contains all information necessary to complete an incomplete command
/// for the user. Will be filled with the generated completions by the different
/// completions functions.
///
class CompletionRequest {
public:
  /// Constructs a completion request.
  ///
  /// \param [in] command_line
  ///     The command line the user has typed at this point.
  ///
  /// \param [in] raw_cursor_pos
  ///     The position of the cursor in the command line string. Index 0 means
  ///     the cursor is at the start of the line. The completion starts from
  ///     this cursor position.
  ///
  /// \param [out] result
  ///     The CompletionResult that will be filled with the results after this
  ///     request has been handled.
  CompletionRequest(llvm::StringRef command_line, unsigned raw_cursor_pos,
                    CompletionResult &result);

  /// Returns the raw user input used to create this CompletionRequest cut off
  /// at the cursor position. The cursor will be at the end of the raw line.
  llvm::StringRef GetRawLine() const {
    return m_command.substr(0, GetRawCursorPos());
  }

  /// Returns the full raw user input used to create this CompletionRequest.
  /// This string is not cut off at the cursor position and will include
  /// characters behind the cursor position.
  ///
  /// You should most likely *not* use this function unless the characters
  /// behind the cursor position influence the completion.
  llvm::StringRef GetRawLineWithUnusedSuffix() const { return m_command; }

  unsigned GetRawCursorPos() const { return m_raw_cursor_pos; }

  const Args &GetParsedLine() const { return m_parsed_line; }

  Args &GetParsedLine() { return m_parsed_line; }

  const Args::ArgEntry &GetParsedArg() {
    return GetParsedLine()[GetCursorIndex()];
  }

  /// Drops the first argument from the argument list.
  void ShiftArguments() {
    m_cursor_index--;
    m_parsed_line.Shift();
  }

  /// Adds an empty argument at the end of the argument list and moves
  /// the cursor to this new argument.
  void AppendEmptyArgument() {
    m_parsed_line.AppendArgument(llvm::StringRef());
    m_cursor_index++;
    m_cursor_char_position = 0;
  }

  size_t GetCursorIndex() const { return m_cursor_index; }

  /// Adds a possible completion string. If the completion was already
  /// suggested before, it will not be added to the list of results. A copy of
  /// the suggested completion is stored, so the given string can be free'd
  /// afterwards.
  ///
  /// \param completion The suggested completion.
  /// \param description An optional description of the completion string. The
  ///     description will be displayed to the user alongside the completion.
  /// \param mode The CompletionMode for this completion.
  void AddCompletion(llvm::StringRef completion,
                     llvm::StringRef description = "",
                     CompletionMode mode = CompletionMode::Normal) {
    m_result.AddResult(completion, description, mode);
  }

  /// Adds a possible completion string if the completion would complete the
  /// current argument.
  ///
  /// \param completion The suggested completion.
  /// \param description An optional description of the completion string. The
  ///     description will be displayed to the user alongside the completion.
  template <CompletionMode M = CompletionMode::Normal>
  void TryCompleteCurrentArg(llvm::StringRef completion,
                             llvm::StringRef description = "") {
    // Trying to rewrite the whole line while checking for the current
    // argument never makes sense. Completion modes are always hardcoded, so
    // this can be a static_assert.
    static_assert(M != CompletionMode::RewriteLine,
                  "Shouldn't rewrite line with this function");
    if (completion.starts_with(GetCursorArgumentPrefix()))
      AddCompletion(completion, description, M);
  }

  /// Adds multiple possible completion strings.
  ///
  /// \param completions The list of completions.
  ///
  /// \see AddCompletion
  void AddCompletions(const StringList &completions) {
    for (const std::string &completion : completions)
      AddCompletion(completion);
  }

  /// Adds multiple possible completion strings alongside their descriptions.
  ///
  /// The number of completions and descriptions must be identical.
  ///
  /// \param completions The list of completions.
  /// \param descriptions The list of descriptions.
  ///
  /// \see AddCompletion
  void AddCompletions(const StringList &completions,
                      const StringList &descriptions) {
    lldbassert(completions.GetSize() == descriptions.GetSize());
    for (std::size_t i = 0; i < completions.GetSize(); ++i)
      AddCompletion(completions.GetStringAtIndex(i),
                    descriptions.GetStringAtIndex(i));
  }

  llvm::StringRef GetCursorArgumentPrefix() const {
    return GetParsedLine().GetArgumentAtIndex(GetCursorIndex());
  }

private:
  /// The raw command line we are supposed to complete.
  llvm::StringRef m_command;
  /// The cursor position in m_command.
  unsigned m_raw_cursor_pos;
  /// The command line parsed as arguments.
  Args m_parsed_line;
  /// The index of the argument in which the completion cursor is.
  size_t m_cursor_index;
  /// The cursor position in the argument indexed by m_cursor_index.
  size_t m_cursor_char_position;

  /// The result this request is supposed to fill out.
  /// We keep this object private to ensure that no backend can in any way
  /// depend on already calculated completions (which would make debugging and
  /// testing them much more complicated).
  CompletionResult &m_result;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_COMPLETIONREQUEST_H
