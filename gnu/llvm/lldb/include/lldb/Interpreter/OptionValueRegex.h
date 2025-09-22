//===-- OptionValueRegex.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONVALUEREGEX_H
#define LLDB_INTERPRETER_OPTIONVALUEREGEX_H

#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Utility/RegularExpression.h"

namespace lldb_private {

class OptionValueRegex : public Cloneable<OptionValueRegex, OptionValue> {
public:
  OptionValueRegex(const char *value = nullptr)
      : m_regex(value), m_default_regex_str(value) {}

  ~OptionValueRegex() override = default;

  // Virtual subclass pure virtual overrides

  OptionValue::Type GetType() const override { return eTypeRegex; }

  void DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                 uint32_t dump_mask) override;

  llvm::json::Value ToJSON(const ExecutionContext *exe_ctx) override {
    return m_regex.GetText();
  }

  Status
  SetValueFromString(llvm::StringRef value,
                     VarSetOperationType op = eVarSetOperationAssign) override;

  void Clear() override {
    m_regex = RegularExpression(m_default_regex_str);
    m_value_was_set = false;
  }

  // Subclass specific functions
  const RegularExpression *GetCurrentValue() const {
    return (m_regex.IsValid() ? &m_regex : nullptr);
  }

  void SetCurrentValue(const char *value) {
    if (value && value[0])
      m_regex = RegularExpression(llvm::StringRef(value));
    else
      m_regex = RegularExpression();
  }

  bool IsValid() const { return m_regex.IsValid(); }

protected:
  RegularExpression m_regex;
  std::string m_default_regex_str;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONVALUEREGEX_H
