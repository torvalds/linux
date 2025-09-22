//===-- OptionValueFormat.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueFormat.h"

#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueFormat::DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                                  uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    strm.PutCString(FormatManager::GetFormatAsCString(m_current_value));
  }
}

llvm::json::Value OptionValueFormat::ToJSON(const ExecutionContext *exe_ctx) {
  return FormatManager::GetFormatAsCString(m_current_value);
}

Status OptionValueFormat::SetValueFromString(llvm::StringRef value,
                                             VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign: {
    Format new_format;
    error = OptionArgParser::ToFormat(value.str().c_str(), new_format, nullptr);
    if (error.Success()) {
      m_value_was_set = true;
      m_current_value = new_format;
      NotifyValueChanged();
    }
  } break;

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
