//===-- OptionValueSInt64.h --------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONVALUESINT64_H
#define LLDB_INTERPRETER_OPTIONVALUESINT64_H

#include "lldb/Interpreter/OptionValue.h"

namespace lldb_private {

class OptionValueSInt64 : public Cloneable<OptionValueSInt64, OptionValue> {
public:
  OptionValueSInt64() = default;

  OptionValueSInt64(int64_t value)
      : m_current_value(value), m_default_value(value) {}

  OptionValueSInt64(int64_t current_value, int64_t default_value)
      : m_current_value(current_value), m_default_value(default_value) {}

  OptionValueSInt64(const OptionValueSInt64 &rhs) = default;

  ~OptionValueSInt64() override = default;

  // Virtual subclass pure virtual overrides

  OptionValue::Type GetType() const override { return eTypeSInt64; }

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

  const int64_t &operator=(int64_t value) {
    m_current_value = value;
    return m_current_value;
  }

  int64_t GetCurrentValue() const { return m_current_value; }

  int64_t GetDefaultValue() const { return m_default_value; }

  bool SetCurrentValue(int64_t value) {
    if (value >= m_min_value && value <= m_max_value) {
      m_current_value = value;
      return true;
    }
    return false;
  }

  bool SetDefaultValue(int64_t value) {
    if (value >= m_min_value && value <= m_max_value) {
      m_default_value = value;
      return true;
    }
    return false;
  }

  void SetMinimumValue(int64_t v) { m_min_value = v; }

  int64_t GetMinimumValue() const { return m_min_value; }

  void SetMaximumValue(int64_t v) { m_max_value = v; }

  int64_t GetMaximumValue() const { return m_max_value; }

protected:
  int64_t m_current_value = 0;
  int64_t m_default_value = 0;
  int64_t m_min_value = std::numeric_limits<int64_t>::min();
  int64_t m_max_value = std::numeric_limits<int64_t>::max();
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONVALUESINT64_H
