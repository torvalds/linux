//===-- OptionValueFileColonLine.cpp---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueFileColonLine.h"

#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

// This is an OptionValue for parsing file:line:column specifications.
// I set the completer to "source file" which isn't quite right, but we can
// only usefully complete in the file name part of it so it should be good
// enough.
OptionValueFileColonLine::OptionValueFileColonLine() = default;

OptionValueFileColonLine::OptionValueFileColonLine(llvm::StringRef input)

{
  SetValueFromString(input, eVarSetOperationAssign);
}

void OptionValueFileColonLine::DumpValue(const ExecutionContext *exe_ctx,
                                         Stream &strm, uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");

    if (m_file_spec)
      strm << '"' << m_file_spec.GetPath().c_str() << '"';
    if (m_line_number != LLDB_INVALID_LINE_NUMBER)
      strm.Printf(":%d", m_line_number);
    if (m_column_number != LLDB_INVALID_COLUMN_NUMBER)
      strm.Printf(":%d", m_column_number);
  }
}

Status OptionValueFileColonLine::SetValueFromString(llvm::StringRef value,
                                                    VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign:
    if (value.size() > 0) {
      // This is in the form filename:linenumber:column.
      // I wish we could use filename:linenumber.column, that would make the
      // parsing unambiguous and so much easier...
      // But clang & gcc both print the output with two : so we're stuck with
      // the two colons.  Practically, the only actual ambiguity this introduces
      // is with files like "foo:10", which doesn't seem terribly likely.

      // Providing the column is optional, so the input value might have one or
      // two colons.  First pick off the last colon separated piece.
      // It has to be there, since the line number is required:
      llvm::StringRef last_piece;
      llvm::StringRef left_of_last_piece;

      std::tie(left_of_last_piece, last_piece) = value.rsplit(':');
      if (last_piece.empty()) {
        error.SetErrorStringWithFormat("Line specifier must include file and "
                                       "line: '%s'",
                                       value.str().c_str());
        return error;
      }

      // Now see if there's another colon and if so pull out the middle piece:
      // Then check whether the middle piece is an integer.  If it is, then it
      // was the line number, and if it isn't we're going to assume that there
      // was a colon in the filename (see note at the beginning of the function)
      // and ignore it.
      llvm::StringRef file_name;
      llvm::StringRef middle_piece;

      std::tie(file_name, middle_piece) = left_of_last_piece.rsplit(':');
      if (middle_piece.empty() ||
          !llvm::to_integer(middle_piece, m_line_number)) {
        // The middle piece was empty or not an integer, so there were only two
        // legit pieces; our original division was right.  Reassign the file
        // name and pull out the line number:
        file_name = left_of_last_piece;
        if (!llvm::to_integer(last_piece, m_line_number)) {
          error.SetErrorStringWithFormat("Bad line number value '%s' in: '%s'",
                                         last_piece.str().c_str(),
                                         value.str().c_str());
          return error;
        }
      } else {
        // There were three pieces, and we've got the line number.  So now
        // we just need to check the column number which was the last peice.
        if (!llvm::to_integer(last_piece, m_column_number)) {
          error.SetErrorStringWithFormat("Bad column value '%s' in: '%s'",
                                         last_piece.str().c_str(),
                                         value.str().c_str());
          return error;
        }
      }

      m_value_was_set = true;
      m_file_spec.SetFile(file_name, FileSpec::Style::native);
      NotifyValueChanged();
    } else {
      error.SetErrorString("invalid value string");
    }
    break;

  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
  case eVarSetOperationRemove:
  case eVarSetOperationAppend:
  case eVarSetOperationInvalid:
    error = OptionValue::SetValueFromString(value, op);
    break;
  }
  return error;
}

void OptionValueFileColonLine::AutoComplete(CommandInterpreter &interpreter,
                                            CompletionRequest &request) {
  lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
      interpreter, m_completion_mask, request, nullptr);
}
