//===-- OptionValueEnumeration.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_OPTIONVALUEENUMERATION_H
#define LLDB_INTERPRETER_OPTIONVALUEENUMERATION_H

#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/lldb-private-types.h"

namespace lldb_private {

class OptionValueEnumeration
    : public Cloneable<OptionValueEnumeration, OptionValue> {
public:
  typedef int64_t enum_type;
  struct EnumeratorInfo {
    enum_type value;
    const char *description;
  };
  typedef UniqueCStringMap<EnumeratorInfo> EnumerationMap;
  typedef EnumerationMap::Entry EnumerationMapEntry;

  OptionValueEnumeration(const OptionEnumValues &enumerators, enum_type value);

  ~OptionValueEnumeration() override = default;

  // Virtual subclass pure virtual overrides

  OptionValue::Type GetType() const override { return eTypeEnum; }

  void DumpValue(const ExecutionContext *exe_ctx, Stream &strm,
                 uint32_t dump_mask) override;

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

  enum_type operator=(enum_type value) {
    m_current_value = value;
    return m_current_value;
  }

  enum_type GetCurrentValue() const { return m_current_value; }

  enum_type GetDefaultValue() const { return m_default_value; }

  void SetCurrentValue(enum_type value) { m_current_value = value; }

  void SetDefaultValue(enum_type value) { m_default_value = value; }

protected:
  void SetEnumerations(const OptionEnumValues &enumerators);

  enum_type m_current_value;
  enum_type m_default_value;
  EnumerationMap m_enumerations;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_OPTIONVALUEENUMERATION_H
