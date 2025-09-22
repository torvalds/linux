//===-- OptionValueUUID.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueUUID.h"

#include "lldb/Core/Module.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueUUID::DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                                uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    m_uuid.Dump(strm);
  }
}

Status OptionValueUUID::SetValueFromString(llvm::StringRef value,
                                           VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign: {
    if (!m_uuid.SetFromStringRef(value))
      error.SetErrorStringWithFormat("invalid uuid string value '%s'",
                                     value.str().c_str());
    else {
      m_value_was_set = true;
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

void OptionValueUUID::AutoComplete(CommandInterpreter &interpreter,
                                   CompletionRequest &request) {
  ExecutionContext exe_ctx(interpreter.GetExecutionContext());
  Target *target = exe_ctx.GetTargetPtr();
  if (!target)
    return;
  auto prefix = request.GetCursorArgumentPrefix();
  llvm::SmallVector<uint8_t, 20> uuid_bytes;
  if (!UUID::DecodeUUIDBytesFromString(prefix, uuid_bytes).empty())
    return;
  const size_t num_modules = target->GetImages().GetSize();
  for (size_t i = 0; i < num_modules; ++i) {
    ModuleSP module_sp(target->GetImages().GetModuleAtIndex(i));
    if (!module_sp)
      continue;
    const UUID &module_uuid = module_sp->GetUUID();
    if (!module_uuid.IsValid())
      continue;
    request.TryCompleteCurrentArg(module_uuid.GetAsString());
  }
}
