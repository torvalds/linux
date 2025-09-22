//===-- OptionValueBoolean.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONVALUEBOOLEAN_H
#define LLDB_INTERPRETER_OPTIONVALUEBOOLEAN_H

#include "lldb/Interpreter/OptionValue.h"

namespace lldb_private {

class OptionValueBoolean : public Cloneable<OptionValueBoolean, OptionValue> {
public:
  OptionValueBoolean(bool value)
      : m_current_value(value), m_default_value(value) {}
  OptionValueBoolean(bool current_value, bool default_value)
      : m_current_value(current_value), m_default_value(default_value) {}

  ~OptionValueBoolean() override = default;

  // Virtual subclass pure virtual overrides

  OptionValue::Type GetType() const override { return eTypeBoolean; }

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

  void AutoComplete(CommandInterpreter &interpreter,
                    CompletionRequest &request) override;

  // Subclass specific functions

  /// Convert to bool operator.
  ///
  /// This allows code to check a OptionValueBoolean in conditions.
  ///
  /// \code
  /// OptionValueBoolean bool_value(...);
  /// if (bool_value)
  /// { ...
  /// \endcode
  ///
  /// \return
  ///     /b True this object contains a valid namespace decl, \b
  ///     false otherwise.
  explicit operator bool() const { return m_current_value; }

  const bool &operator=(bool b) {
    m_current_value = b;
    return m_current_value;
  }

  bool GetCurrentValue() const { return m_current_value; }

  bool GetDefaultValue() const { return m_default_value; }

  void SetCurrentValue(bool value) { m_current_value = value; }

  void SetDefaultValue(bool value) { m_default_value = value; }

protected:
  bool m_current_value;
  bool m_default_value;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONVALUEBOOLEAN_H
