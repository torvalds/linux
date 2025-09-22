//===-- OptionValueArch.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueArch.h"

#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Interpreter/CommandCompletions.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/State.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueArch::DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                                uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");

    if (m_current_value.IsValid()) {
      const char *arch_name = m_current_value.GetArchitectureName();
      if (arch_name)
        strm.PutCString(arch_name);
    }
  }
}

Status OptionValueArch::SetValueFromString(llvm::StringRef value,
                                           VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign: {
    std::string value_str = value.trim().str();
    if (m_current_value.SetTriple(value_str.c_str())) {
      m_value_was_set = true;
      NotifyValueChanged();
    } else
      error.SetErrorStringWithFormat("unsupported architecture '%s'",
                                     value_str.c_str());
    break;
  }
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

void OptionValueArch::AutoComplete(CommandInterpreter &interpreter,
                                   CompletionRequest &request) {
  lldb_private::CommandCompletions::InvokeCommonCompletionCallbacks(
      interpreter, lldb::eArchitectureCompletion, request, nullptr);
}
