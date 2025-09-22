//===-- ValueObjectSyntheticFilter.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTSYNTHETICFILTER_H
#define LLDB_CORE_VALUEOBJECTSYNTHETICFILTER_H

#include "lldb/Core/ValueObject.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"

#include <cstdint>
#include <memory>
#include <optional>

#include <cstddef>

namespace lldb_private {
class Declaration;
class Status;
class SyntheticChildrenFrontEnd;

/// A ValueObject that obtains its children from some source other than
/// real information.
/// This is currently used to implement Python-based children and filters but
/// you can bind it to any source of synthetic information and have it behave
/// accordingly.
class ValueObjectSynthetic : public ValueObject {
public:
  ~ValueObjectSynthetic() override;

  std::optional<uint64_t> GetByteSize() override;

  ConstString GetTypeName() override;

  ConstString GetQualifiedTypeName() override;

  ConstString GetDisplayTypeName() override;

  bool MightHaveChildren() override;

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

  lldb::ValueType GetValueType() const override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx,
                                      bool can_create = true) override;

  lldb::ValueObjectSP GetChildMemberWithName(llvm::StringRef name,
                                             bool can_create = true) override;

  size_t GetIndexOfChildWithName(llvm::StringRef name) override;

  lldb::ValueObjectSP
  GetDynamicValue(lldb::DynamicValueType valueType) override;

  bool IsInScope() override;

  bool HasSyntheticValue() override { return false; }

  bool IsSynthetic() override { return true; }

  void CalculateSyntheticValue() override {}

  bool IsDynamic() override {
    return ((m_parent != nullptr) ? m_parent->IsDynamic() : false);
  }

  lldb::ValueObjectSP GetStaticValue() override {
    return ((m_parent != nullptr) ? m_parent->GetStaticValue() : GetSP());
  }

  virtual lldb::DynamicValueType GetDynamicValueType() {
    return ((m_parent != nullptr) ? m_parent->GetDynamicValueType()
                                  : lldb::eNoDynamicValues);
  }

  lldb::VariableSP GetVariable() override {
    return m_parent != nullptr ? m_parent->GetVariable() : nullptr;
  }

  ValueObject *GetParent() override {
    return ((m_parent != nullptr) ? m_parent->GetParent() : nullptr);
  }

  const ValueObject *GetParent() const override {
    return ((m_parent != nullptr) ? m_parent->GetParent() : nullptr);
  }

  lldb::ValueObjectSP GetNonSyntheticValue() override;

  bool CanProvideValue() override;

  bool DoesProvideSyntheticValue() override {
    return (UpdateValueIfNeeded(), m_provides_value == eLazyBoolYes);
  }

  bool GetIsConstant() const override { return false; }

  bool SetValueFromCString(const char *value_str, Status &error) override;

  void SetFormat(lldb::Format format) override;

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

  CompilerType GetCompilerTypeImpl() override;

  virtual void CreateSynthFilter();

  // we need to hold on to the SyntheticChildren because someone might delete
  // the type binding while we are alive
  lldb::SyntheticChildrenSP m_synth_sp;
  std::unique_ptr<SyntheticChildrenFrontEnd> m_synth_filter_up;

  typedef std::map<uint32_t, ValueObject *> ByIndexMap;
  typedef std::map<const char *, uint32_t> NameToIndexMap;
  typedef std::vector<lldb::ValueObjectSP> SyntheticChildrenCache;

  typedef ByIndexMap::iterator ByIndexIterator;
  typedef NameToIndexMap::iterator NameToIndexIterator;

  std::mutex m_child_mutex;
  /// Guarded by m_child_mutex;
  ByIndexMap m_children_byindex;
  /// Guarded by m_child_mutex;
  NameToIndexMap m_name_toindex;
  /// Guarded by m_child_mutex;
  SyntheticChildrenCache m_synthetic_children_cache;

  // FIXME: use the ValueObject's  ChildrenManager instead of a special purpose
  // solution.
  uint32_t m_synthetic_children_count;

  ConstString m_parent_type_name;

  LazyBool m_might_have_children;

  LazyBool m_provides_value;

private:
  friend class ValueObject;
  ValueObjectSynthetic(ValueObject &parent, lldb::SyntheticChildrenSP filter);

  void CopyValueData(ValueObject *source);

  ValueObjectSynthetic(const ValueObjectSynthetic &) = delete;
  const ValueObjectSynthetic &operator=(const ValueObjectSynthetic &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTSYNTHETICFILTER_H
