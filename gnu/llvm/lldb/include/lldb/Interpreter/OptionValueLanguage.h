//===-- OptionValueLanguage.h -------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONVALUELANGUAGE_H
#define LLDB_INTERPRETER_OPTIONVALUELANGUAGE_H

#include "lldb/Interpreter/OptionValue.h"
#include "lldb/lldb-enumerations.h"

namespace lldb_private {

class OptionValueLanguage : public Cloneable<OptionValueLanguage, OptionValue> {
public:
  OptionValueLanguage(lldb::LanguageType value)
      : m_current_value(value), m_default_value(value) {}

  OptionValueLanguage(lldb::LanguageType current_value,
                      lldb::LanguageType default_value)
      : m_current_value(current_value), m_default_value(default_value) {}

  ~OptionValueLanguage() override = default;

  // Virtual subclass pure virtual overrides

  OptionValue::Type GetType() const override { return eTypeLanguage; }

  void DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                 uint32_t dump_mask) override;

  llvm::json::Value ToJSON(const ExecutionContext *exe_ctx) override;

  Status
  SetValueFromString(llvm::StringRef value,
                     VarSetOperationType op = eVarSetOperationAssign) override;

  void Clear() override {
    m_current_value = m_default_value;
    m_value_was_set = false;
  }

  // Subclass specific functions

  lldb::LanguageType GetCurrentValue() const { return m_current_value; }

  lldb::LanguageType GetDefaultValue() const { return m_default_value; }

  void SetCurrentValue(lldb::LanguageType value) { m_current_value = value; }

  void SetDefaultValue(lldb::LanguageType value) { m_default_value = value; }

protected:
  lldb::LanguageType m_current_value;
  lldb::LanguageType m_default_value;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONVALUELANGUAGE_H
