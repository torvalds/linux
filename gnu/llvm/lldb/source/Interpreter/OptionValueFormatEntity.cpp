//===-- OptionValueFormatEntity.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueFormatEntity.h"

#include "lldb/Core/Module.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"
using namespace lldb;
using namespace lldb_private;

OptionValueFormatEntity::OptionValueFormatEntity(const char *default_format) {
  if (default_format && default_format[0]) {
    llvm::StringRef default_format_str(default_format);
    Status error = FormatEntity::Parse(default_format_str, m_default_entry);
    if (error.Success()) {
      m_default_format = default_format;
      m_current_format = default_format;
      m_current_entry = m_default_entry;
    }
  }
}

void OptionValueFormatEntity::Clear() {
  m_current_entry = m_default_entry;
  m_current_format = m_default_format;
  m_value_was_set = false;
}

static void EscapeBackticks(llvm::StringRef str, std::string &dst) {
  dst.clear();
  dst.reserve(str.size());

  for (size_t i = 0, e = str.size(); i != e; ++i) {
    char c = str[i];
    if (c == '`') {
      if (i == 0 || str[i - 1] != '\\')
        dst += '\\';
    }
    dst += c;
  }
}

void OptionValueFormatEntity::DumpValue(const ExecutionContext *exe_ctx,
                                        Stream &strm, uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    std::string escaped;
    EscapeBackticks(m_current_format, escaped);
    strm << '"' << escaped << '"';
  }
}

llvm::json::Value
OptionValueFormatEntity::ToJSON(const ExecutionContext *exe_ctx) {
  std::string escaped;
  EscapeBackticks(m_current_format, escaped);
  return escaped;
}

Status OptionValueFormatEntity::SetValueFromString(llvm::StringRef value_str,
                                                   VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign: {
    // Check if the string starts with a quote character after removing leading
    // and trailing spaces. If it does start with a quote character, make sure
    // it ends with the same quote character and remove the quotes before we
    // parse the format string. If the string doesn't start with a quote, leave
    // the string alone and parse as is.
    llvm::StringRef trimmed_value_str = value_str.trim();
    if (!trimmed_value_str.empty()) {
      const char first_char = trimmed_value_str[0];
      if (first_char == '"' || first_char == '\'') {
        const size_t trimmed_len = trimmed_value_str.size();
        if (trimmed_len == 1 || value_str[trimmed_len - 1] != first_char) {
          error.SetErrorString("mismatched quotes");
          return error;
        }
        value_str = trimmed_value_str.substr(1, trimmed_len - 2);
      }
    }
    FormatEntity::Entry entry;
    error = FormatEntity::Parse(value_str, entry);
    if (error.Success()) {
      m_current_entry = std::move(entry);
      m_current_format = std::string(value_str);
      m_value_was_set = true;
      NotifyValueChanged();
    }
  } break;

  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
  case eVarSetOperationRemove:
  case eVarSetOperationAppend:
  case eVarSetOperationInvalid:
    error = OptionValue::SetValueFromString(value_str, op);
    break;
  }
  return error;
}

void OptionValueFormatEntity::AutoComplete(CommandInterpreter &interpreter,
                                           CompletionRequest &request) {
  FormatEntity::AutoComplete(request);
}
