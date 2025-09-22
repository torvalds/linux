//===-- ValueObjectDynamicValue.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTDYNAMICVALUE_H
#define LLDB_CORE_VALUEOBJECTDYNAMICVALUE_H

#include "lldb/Core/Address.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lldb_private {
class DataExtractor;
class Declaration;
class Status;

/// A ValueObject that represents memory at a given address, viewed as some
/// set lldb type.
class ValueObjectDynamicValue : public ValueObject {
public:
  ~ValueObjectDynamicValue() override = default;

  std::optional<uint64_t> GetByteSize() override;

  ConstString GetTypeName() override;

  ConstString GetQualifiedTypeName() override;

  ConstString GetDisplayTypeName() override;

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

  lldb::ValueType GetValueType() const override;

  bool IsInScope() override;

  bool IsDynamic() override { return true; }

  bool IsBaseClass() override {
    if (m_parent)
      return m_parent->IsBaseClass();
    return false;
  }

  bool GetIsConstant() const override { return false; }

  ValueObject *GetParent() override {
    return ((m_parent != nullptr) ? m_parent->GetParent() : nullptr);
  }

  const ValueObject *GetParent() const override {
    return ((m_parent != nullptr) ? m_parent->GetParent() : nullptr);
  }

  lldb::ValueObjectSP GetStaticValue() override { return m_parent->GetSP(); }

  bool SetValueFromCString(const char *value_str, Status &error) override;

  bool SetData(DataExtractor &data, Status &error) override;

  TypeImpl GetTypeImpl() override;

  lldb::VariableSP GetVariable() override {
    return m_parent ? m_parent->GetVariable() : nullptr;
  }

  lldb::LanguageType GetPreferredDisplayLanguage() override;

  void SetPreferredDisplayLanguage(lldb::LanguageType);

  bool IsSyntheticChildrenGenerated() override;

  void SetSyntheticChildrenGenerated(bool b) override;

  bool GetDeclaration(Declaration &decl) override;

  uint64_t GetLanguageFlags() override;

  void SetLanguageFlags(uint64_t flags) override;

protected:
  bool UpdateValue() override;

  LazyBool CanUpdateWithInvalidExecutionContext() override {
    return eLazyBoolYes;
  }

  lldb::DynamicValueType GetDynamicValueTypeImpl() override {
    return m_use_dynamic;
  }

  bool HasDynamicValueTypeInfo() override { return true; }

  CompilerType GetCompilerTypeImpl() override;

  Address m_address; ///< The variable that this value object is based upon
  TypeAndOrName m_dynamic_type_info; // We can have a type_sp or just a name
  lldb::DynamicValueType m_use_dynamic;
  TypeImpl m_type_impl;

private:
  friend class ValueObject;
  friend class ValueObjectConstResult;
  ValueObjectDynamicValue(ValueObject &parent,
                          lldb::DynamicValueType use_dynamic);

  ValueObjectDynamicValue(const ValueObjectDynamicValue &) = delete;
  const ValueObjectDynamicValue &
  operator=(const ValueObjectDynamicValue &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTDYNAMICVALUE_H
