//===-- OptionValueChar.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONVALUECHAR_H
#define LLDB_INTERPRETER_OPTIONVALUECHAR_H

#include "lldb/Interpreter/OptionValue.h"

namespace lldb_private {

class OptionValueChar : public Cloneable<OptionValueChar, OptionValue> {
public:
  OptionValueChar(char value)
      : m_current_value(value), m_default_value(value) {}

  OptionValueChar(char current_value, char default_value)
      : m_current_value(current_value), m_default_value(default_value) {}

  ~OptionValueChar() override = default;

  // Virtual subclass pure virtual overrides

  OptionValue::Type GetType() const override { return eTypeChar; }

  void DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                 uint32_t dump_mask) override;

  llvm::json::Value ToJSON(const ExecutionContext *exe_ctx) override {
    return m_current_value;
  }

  Status
  SetValueFromString(llvm::StringRef value,
                     VarSetOperationType op = eVarSetOperationAssign) override;

  void Clear() override {
    m_current_value = m_default_value;
    m_value_was_set = false;
  }

  // Subclass specific functions

  const char &operator=(char c) {
    m_current_value = c;
    return m_current_value;
  }

  char GetCurrentValue() const { return m_current_value; }

  char GetDefaultValue() const { return m_default_value; }

  void SetCurrentValue(char value) { m_current_value = value; }

  void SetDefaultValue(char value) { m_default_value = value; }

protected:
  char m_current_value;
  char m_default_value;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONVALUECHAR_H
