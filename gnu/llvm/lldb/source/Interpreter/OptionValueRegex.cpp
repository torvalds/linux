//===-- OptionValueRegex.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueRegex.h"

#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueRegex::DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                                 uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    if (m_regex.IsValid()) {
      llvm::StringRef regex_text = m_regex.GetText();
      strm.Printf("%s", regex_text.str().c_str());
    }
  }
}

Status OptionValueRegex::SetValueFromString(llvm::StringRef value,
                                            VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationInvalid:
  case eVarSetOperationInsertBefore:
  case eVarSetOperationInsertAfter:
  case eVarSetOperationRemove:
  case eVarSetOperationAppend:
    error = OptionValue::SetValueFromString(value, op);
    break;

  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign:
    m_regex = RegularExpression(value);
    if (m_regex.IsValid()) {
      m_value_was_set = true;
      NotifyValueChanged();
    } else if (llvm::Error err = m_regex.GetError()) {
      error.SetErrorString(llvm::toString(std::move(err)));
    } else {
      error.SetErrorString("regex error");
    }
    break;
  }
  return error;
}
