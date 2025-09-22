//===-- OptionValuePathMappings.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONVALUEPATHMAPPINGS_H
#define LLDB_INTERPRETER_OPTIONVALUEPATHMAPPINGS_H

#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Target/PathMappingList.h"

namespace lldb_private {

class OptionValuePathMappings
    : public Cloneable<OptionValuePathMappings, OptionValue> {
public:
  OptionValuePathMappings(bool notify_changes)
      : m_notify_changes(notify_changes) {}

  ~OptionValuePathMappings() override = default;

  // Virtual subclass pure virtual overrides

  OptionValue::Type GetType() const override { return eTypePathMap; }

  void DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                 uint32_t dump_mask) override;

  llvm::json::Value ToJSON(const ExecutionContext *exe_ctx) override;

  Status
  SetValueFromString(llvm::StringRef value,
                     VarSetOperationType op = eVarSetOperationAssign) override;

  void Clear() override {
    m_path_mappings.Clear(m_notify_changes);
    m_value_was_set = false;
  }

  bool IsAggregateValue() const override { return true; }

  // Subclass specific functions

  PathMappingList &GetCurrentValue() { return m_path_mappings; }

  const PathMappingList &GetCurrentValue() const { return m_path_mappings; }

protected:
  PathMappingList m_path_mappings;
  bool m_notify_changes;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONVALUEPATHMAPPINGS_H
