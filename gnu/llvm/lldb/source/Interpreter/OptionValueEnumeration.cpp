//===-- OptionValueEnumeration.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueEnumeration.h"

#include "lldb/Utility/StringList.h"

using namespace lldb;
using namespace lldb_private;

OptionValueEnumeration::OptionValueEnumeration(
    const OptionEnumValues &enumerators, enum_type value)
    : m_current_value(value), m_default_value(value) {
  SetEnumerations(enumerators);
}

void OptionValueEnumeration::DumpValue(const ExecutionContext *exe_ctx,
                                       Stream &strm, uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    const size_t count = m_enumerations.GetSize();
    for (size_t i = 0; i < count; ++i) {
      if (m_enumerations.GetValueAtIndexUnchecked(i).value == m_current_value) {
        strm.PutCString(m_enumerations.GetCStringAtIndex(i).GetStringRef());
        return;
      }
    }
    strm.Printf("%" PRIu64, (uint64_t)m_current_value);
  }
}

Status OptionValueEnumeration::SetValueFromString(llvm::StringRef value,
                                                  VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    NotifyValueChanged();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign: {
    ConstString const_enumerator_name(value.trim());
    const EnumerationMapEntry *enumerator_entry =
        m_enumerations.FindFirstValueForName(const_enumerator_name);
    if (enumerator_entry) {
      m_current_value = enumerator_entry->value.value;
      NotifyValueChanged();
    } else {
      StreamString error_strm;
      error_strm.Printf("invalid enumeration value '%s'", value.str().c_str());
      const size_t count = m_enumerations.GetSize();
      if (count) {
        error_strm.Printf(", valid values are: %s",
                          m_enumerations.GetCStringAtIndex(0).GetCString());
        for (size_t i = 1; i < count; ++i) {
          error_strm.Printf(", %s",
                            m_enumerations.GetCStringAtIndex(i).GetCString());
        }
      }
      error.SetErrorString(error_strm.GetString());
    }
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

void OptionValueEnumeration::SetEnumerations(
    const OptionEnumValues &enumerators) {
  m_enumerations.Clear();

  for (const auto &enumerator : enumerators) {
    ConstString const_enumerator_name(enumerator.string_value);
    EnumeratorInfo enumerator_info = {enumerator.value, enumerator.usage};
    m_enumerations.Append(const_enumerator_name, enumerator_info);
  }

  m_enumerations.Sort();
}

void OptionValueEnumeration::AutoComplete(CommandInterpreter &interpreter,
                                          CompletionRequest &request) {
  const uint32_t num_enumerators = m_enumerations.GetSize();
  if (!request.GetCursorArgumentPrefix().empty()) {
    for (size_t i = 0; i < num_enumerators; ++i) {
      llvm::StringRef name = m_enumerations.GetCStringAtIndex(i).GetStringRef();
      request.TryCompleteCurrentArg(name);
    }
    return;
  }
    for (size_t i = 0; i < num_enumerators; ++i)
      request.AddCompletion(m_enumerations.GetCStringAtIndex(i).GetStringRef());
}
