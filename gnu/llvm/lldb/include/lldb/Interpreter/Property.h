//===-- Property.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_PROPERTY_H
#define LLDB_INTERPRETER_PROPERTY_H

#include "lldb/Interpreter/OptionValue.h"
#include "lldb/Utility/Flags.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-private-types.h"

#include <string>

namespace lldb_private {

// A structure that can be used to create a global table for all properties.
// Property class instances can be constructed using one of these.
struct PropertyDefinition {
  const char *name;
  OptionValue::Type type;
  bool global; // false == this setting is a global setting by default
  uintptr_t default_uint_value;
  const char *default_cstr_value;
  OptionEnumValues enum_values;
  const char *description;
};

using PropertyDefinitions = llvm::ArrayRef<PropertyDefinition>;

class Property {
public:
  Property(const PropertyDefinition &definition);

  Property(llvm::StringRef name, llvm::StringRef desc, bool is_global,
           const lldb::OptionValueSP &value_sp);

  llvm::StringRef GetName() const { return m_name; }
  llvm::StringRef GetDescription() const { return m_description; }

  const lldb::OptionValueSP &GetValue() const { return m_value_sp; }

  void SetOptionValue(const lldb::OptionValueSP &value_sp) {
    m_value_sp = value_sp;
  }

  bool IsValid() const { return (bool)m_value_sp; }

  bool IsGlobal() const { return m_is_global; }

  void Dump(const ExecutionContext *exe_ctx, Stream &strm,
            uint32_t dump_mask) const;

  bool DumpQualifiedName(Stream &strm) const;

  void DumpDescription(CommandInterpreter &interpreter, Stream &strm,
                       uint32_t output_width,
                       bool display_qualified_name) const;

  void SetValueChangedCallback(std::function<void()> callback);

protected:
  std::string m_name;
  std::string m_description;
  lldb::OptionValueSP m_value_sp;
  bool m_is_global;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_PROPERTY_H
