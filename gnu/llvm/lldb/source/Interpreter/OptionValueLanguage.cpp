//===-- OptionValueLanguage.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Interpreter/OptionValueLanguage.h"

#include "lldb/DataFormatters/FormatManager.h"
#include "lldb/Target/Language.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

void OptionValueLanguage::DumpValue(const ExecutionContext *exe_ctx,
                                    Stream &strm, uint32_t dump_mask) {
  if (dump_mask & eDumpOptionType)
    strm.Printf("(%s)", GetTypeAsCString());
  if (dump_mask & eDumpOptionValue) {
    if (dump_mask & eDumpOptionType)
      strm.PutCString(" = ");
    if (m_current_value != eLanguageTypeUnknown)
      strm.PutCString(Language::GetNameForLanguageType(m_current_value));
  }
}

llvm::json::Value OptionValueLanguage::ToJSON(const ExecutionContext *exe_ctx) {
  return Language::GetNameForLanguageType(m_current_value);
}

Status OptionValueLanguage::SetValueFromString(llvm::StringRef value,
                                               VarSetOperationType op) {
  Status error;
  switch (op) {
  case eVarSetOperationClear:
    Clear();
    break;

  case eVarSetOperationReplace:
  case eVarSetOperationAssign: {
    LanguageSet languages_for_types = Language::GetLanguagesSupportingTypeSystems();
    LanguageType new_type = Language::GetLanguageTypeFromString(value.trim());
    if (new_type && languages_for_types[new_type]) {
      m_value_was_set = true;
      m_current_value = new_type;
    } else {
      StreamString error_strm;
      error_strm.Printf("invalid language type '%s', ", value.str().c_str());
      error_strm.Printf("valid values are:\n");
      for (int bit : languages_for_types.bitvector.set_bits()) {
        auto language = (LanguageType)bit;
        error_strm.Printf("    %s\n",
                          Language::GetNameForLanguageType(language));
      }
      error.SetErrorString(error_strm.GetString());
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
