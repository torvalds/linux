//===-- ValueObjectVariable.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTVARIABLE_H
#define LLDB_CORE_VALUEOBJECTVARIABLE_H

#include "lldb/Core/ValueObject.h"

#include "lldb/Core/Value.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace lldb_private {
class DataExtractor;
class Declaration;
class Status;
class ExecutionContextScope;
class SymbolContextScope;

/// A ValueObject that contains a root variable that may or may not
/// have children.
class ValueObjectVariable : public ValueObject {
public:
  ~ValueObjectVariable() override;

  static lldb::ValueObjectSP Create(ExecutionContextScope *exe_scope,
                                    const lldb::VariableSP &var_sp);

  std::optional<uint64_t> GetByteSize() override;

  ConstString GetTypeName() override;

  ConstString GetQualifiedTypeName() override;

  ConstString GetDisplayTypeName() override;

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

  lldb::ValueType GetValueType() const override;

  bool IsInScope() override;

  lldb::ModuleSP GetModule() override;

  SymbolContextScope *GetSymbolContextScope() override;

  bool GetDeclaration(Declaration &decl) override;

  const char *GetLocationAsCString() override;

  bool SetValueFromCString(const char *value_str, Status &error) override;

  bool SetData(DataExtractor &data, Status &error) override;

  lldb::VariableSP GetVariable() override { return m_variable_sp; }

protected:
  bool UpdateValue() override;
  
  void DoUpdateChildrenAddressType(ValueObject &valobj) override;

  CompilerType GetCompilerTypeImpl() override;

  /// The variable that this value object is based upon.
  lldb::VariableSP m_variable_sp;
  ///< The value that DWARFExpression resolves this variable to before we patch
  ///< it up.
  Value m_resolved_value;

private:
  ValueObjectVariable(ExecutionContextScope *exe_scope,
                      ValueObjectManager &manager,
                      const lldb::VariableSP &var_sp);
  // For ValueObject only
  ValueObjectVariable(const ValueObjectVariable &) = delete;
  const ValueObjectVariable &operator=(const ValueObjectVariable &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTVARIABLE_H
