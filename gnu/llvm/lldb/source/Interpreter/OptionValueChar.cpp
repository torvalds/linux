//===-- OptionValueChar.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueChar.h"

#include "lldb/Interpreter/OptionArgParser.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"
#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueChar::DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                                uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());

  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    if (m_current_value != '\0')
      strm.PutChar(m_current_value);
    else
      strm.PutCString("(null)");
  }
}

Status OptionValueChar::SetValueFromString(llvm::StringRef value,
                                           VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign: {
    bool success = false;
    char char_value = OptionArgParser::ToChar(value, '\0', &success);
    if (success) {
      m_current_value = char_value;
      m_value_was_set = true;
    } else
      error.SetErrorStringWithFormat("'%s' cannot be longer than 1 character",
                                     value.str().c_str());
  } break;

  default:
    error = OptionValue::SetValueFromString(value, op);
    break;
  }
  return error;
}
