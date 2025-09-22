//===-- ValueObjectSyntheticFilter.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectSyntheticFilter.h"

#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"

#include "llvm/ADT/STLExtras.h"
#include <optional>

namespace lldb_private {
class Declaration;
}

using namespace lldb_private;

class DummySyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  DummySyntheticFrontEnd(ValueObject &backend)
      : SyntheticChildrenFrontEnd(backend) {}

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    return m_backend.GetNumChildren();
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    return m_backend.GetChildAtIndex(idx);
  }

  size_t GetIndexOfChildWithName(ConstString name) override {
    return m_backend.GetIndexOfChildWithName(name);
  }

  bool MightHaveChildren() override { return m_backend.MightHaveChildren(); }

  lldb::ChildCacheState Update() override {
    return lldb::ChildCacheState::eRefetch;
  }
};

ValueObjectSynthetic::ValueObjectSynthetic(ValueObject &parent,
                                           lldb::SyntheticChildrenSP filter)
    : ValueObject(parent), m_synth_sp(std::move(filter)), m_children_byindex(),
      m_name_toindex(), m_synthetic_children_cache(),
      m_synthetic_children_count(UINT32_MAX),
      m_parent_type_name(parent.GetTypeName()),
      m_might_have_children(eLazyBoolCalculate),
      m_provides_value(eLazyBoolCalculate) {
  SetName(parent.GetName());
  // Copying the data of an incomplete type won't work as it has no byte size.
  if (m_parent->GetCompilerType().IsCompleteType())
    CopyValueData(m_parent);
  CreateSynthFilter();
}

ValueObjectSynthetic::~ValueObjectSynthetic() = default;

CompilerType ValueObjectSynthetic::GetCompilerTypeImpl() {
  return m_parent->GetCompilerType();
}

ConstString ValueObjectSynthetic::GetTypeName() {
  return m_parent->GetTypeName();
}

ConstString ValueObjectSynthetic::GetQualifiedTypeName() {
  return m_parent->GetQualifiedTypeName();
}

ConstString ValueObjectSynthetic::GetDisplayTypeName() {
  if (ConstString synth_name = m_synth_filter_up->GetSyntheticTypeName())
    return synth_name;

  return m_parent->GetDisplayTypeName();
}

llvm::Expected<uint32_t>
ValueObjectSynthetic::CalculateNumChildren(uint32_t max) {
  Log *log = GetLog(LLDBLog::DataFormatters);

  UpdateValueIfNeeded();
  if (m_synthetic_children_count < UINT32_MAX)
    return m_synthetic_children_count <= max ? m_synthetic_children_count : max;

  if (max < UINT32_MAX) {
    auto num_children = m_synth_filter_up->CalculateNumChildren(max);
    LLDB_LOGF(log,
              "[ValueObjectSynthetic::CalculateNumChildren] for VO of name "
              "%s and type %s, the filter returned %u child values",
              GetName().AsCString(), GetTypeName().AsCString(),
              num_children ? *num_children : 0);
    return num_children;
  } else {
    auto num_children_or_err = m_synth_filter_up->CalculateNumChildren(max);
    if (!num_children_or_err) {
      m_synthetic_children_count = 0;
      return num_children_or_err;
    }
    auto num_children = (m_synthetic_children_count = *num_children_or_err);
    LLDB_LOGF(log,
              "[ValueObjectSynthetic::CalculateNumChildren] for VO of name "
              "%s and type %s, the filter returned %u child values",
              GetName().AsCString(), GetTypeName().AsCString(), num_children);
    return num_children;
  }
}

lldb::ValueObjectSP
ValueObjectSynthetic::GetDynamicValue(lldb::DynamicValueType valueType) {
  if (!m_parent)
    return lldb::ValueObjectSP();
  if (IsDynamic() && GetDynamicValueType() == valueType)
    return GetSP();
  return m_parent->GetDynamicValue(valueType);
}

bool ValueObjectSynthetic::MightHaveChildren() {
  if (m_might_have_children == eLazyBoolCalculate)
    m_might_have_children =
        (m_synth_filter_up->MightHaveChildren() ? eLazyBoolYes : eLazyBoolNo);
  return (m_might_have_children != eLazyBoolNo);
}

std::optional<uint64_t> ValueObjectSynthetic::GetByteSize() {
  return m_parent->GetByteSize();
}

lldb::ValueType ValueObjectSynthetic::GetValueType() const {
  return m_parent->GetValueType();
}

void ValueObjectSynthetic::CreateSynthFilter() {
  ValueObject *valobj_for_frontend = m_parent;
  if (m_synth_sp->WantsDereference())
  {
    CompilerType type = m_parent->GetCompilerType();
    if (type.IsValid() && type.IsPointerOrReferenceType())
    {
      Status error;
      lldb::ValueObjectSP deref_sp = m_parent->Dereference(error);
      if (error.Success())
        valobj_for_frontend = deref_sp.get();
    }
  }
  m_synth_filter_up = (m_synth_sp->GetFrontEnd(*valobj_for_frontend));
  if (!m_synth_filter_up)
    m_synth_filter_up = std::make_unique<DummySyntheticFrontEnd>(*m_parent);
}

bool ValueObjectSynthetic::UpdateValue() {
  Log *log = GetLog(LLDBLog::DataFormatters);

  SetValueIsValid(false);
  m_error.Clear();

  if (!m_parent->UpdateValueIfNeeded(false)) {
    // our parent could not update.. as we are meaningless without a parent,
    // just stop
    if (m_parent->GetError().Fail())
      m_error = m_parent->GetError();
    return false;
  }

  // Regenerate the synthetic filter if our typename changes. When the (dynamic)
  // type of an object changes, so does their synthetic filter of choice.
  ConstString new_parent_type_name = m_parent->GetTypeName();
  if (new_parent_type_name != m_parent_type_name) {
    LLDB_LOGF(log,
              "[ValueObjectSynthetic::UpdateValue] name=%s, type changed "
              "from %s to %s, recomputing synthetic filter",
              GetName().AsCString(), m_parent_type_name.AsCString(),
              new_parent_type_name.AsCString());
    m_parent_type_name = new_parent_type_name;
    CreateSynthFilter();
  }

  // let our backend do its update
  if (m_synth_filter_up->Update() == lldb::ChildCacheState::eRefetch) {
    LLDB_LOGF(log,
              "[ValueObjectSynthetic::UpdateValue] name=%s, synthetic "
              "filter said caches are stale - clearing",
              GetName().AsCString());
    // filter said that cached values are stale
    {
      std::lock_guard<std::mutex> guard(m_child_mutex);
      m_children_byindex.clear();
      m_name_toindex.clear();
    }
    // usually, an object's value can change but this does not alter its
    // children count for a synthetic VO that might indeed happen, so we need
    // to tell the upper echelons that they need to come back to us asking for
    // children
    m_flags.m_children_count_valid = false;
    {
      std::lock_guard<std::mutex> guard(m_child_mutex);
      m_synthetic_children_cache.clear();
    }
    m_synthetic_children_count = UINT32_MAX;
    m_might_have_children = eLazyBoolCalculate;
  } else {
    LLDB_LOGF(log,
              "[ValueObjectSynthetic::UpdateValue] name=%s, synthetic "
              "filter said caches are still valid",
              GetName().AsCString());
  }

  m_provides_value = eLazyBoolCalculate;

  lldb::ValueObjectSP synth_val(m_synth_filter_up->GetSyntheticValue());

  if (synth_val && synth_val->CanProvideValue()) {
    LLDB_LOGF(log,
              "[ValueObjectSynthetic::UpdateValue] name=%s, synthetic "
              "filter said it can provide a value",
              GetName().AsCString());

    m_provides_value = eLazyBoolYes;
    CopyValueData(synth_val.get());
  } else {
    LLDB_LOGF(log,
              "[ValueObjectSynthetic::UpdateValue] name=%s, synthetic "
              "filter said it will not provide a value",
              GetName().AsCString());

    m_provides_value = eLazyBoolNo;
    // Copying the data of an incomplete type won't work as it has no byte size.
    if (m_parent->GetCompilerType().IsCompleteType())
      CopyValueData(m_parent);
  }

  SetValueIsValid(true);
  return true;
}

lldb::ValueObjectSP ValueObjectSynthetic::GetChildAtIndex(uint32_t idx,
                                                          bool can_create) {
  Log *log = GetLog(LLDBLog::DataFormatters);

  LLDB_LOGF(log,
            "[ValueObjectSynthetic::GetChildAtIndex] name=%s, retrieving "
            "child at index %u",
            GetName().AsCString(), idx);

  UpdateValueIfNeeded();

  ValueObject *valobj;
  bool child_is_cached;
  {
    std::lock_guard<std::mutex> guard(m_child_mutex);
    auto cached_child_it = m_children_byindex.find(idx);
    child_is_cached = cached_child_it != m_children_byindex.end();
    if (child_is_cached)
      valobj = cached_child_it->second;
  }

  if (!child_is_cached) {
    if (can_create && m_synth_filter_up != nullptr) {
      LLDB_LOGF(log,
                "[ValueObjectSynthetic::GetChildAtIndex] name=%s, child at "
                "index %u not cached and will be created",
                GetName().AsCString(), idx);

      lldb::ValueObjectSP synth_guy = m_synth_filter_up->GetChildAtIndex(idx);

      LLDB_LOGF(
          log,
          "[ValueObjectSynthetic::GetChildAtIndex] name=%s, child at index "
          "%u created as %p (is "
          "synthetic: %s)",
          GetName().AsCString(), idx, static_cast<void *>(synth_guy.get()),
          synth_guy.get()
              ? (synth_guy->IsSyntheticChildrenGenerated() ? "yes" : "no")
              : "no");

      if (!synth_guy)
        return synth_guy;

      {
        std::lock_guard<std::mutex> guard(m_child_mutex);
        if (synth_guy->IsSyntheticChildrenGenerated())
          m_synthetic_children_cache.push_back(synth_guy);
        m_children_byindex[idx] = synth_guy.get();
      }
      synth_guy->SetPreferredDisplayLanguageIfNeeded(
          GetPreferredDisplayLanguage());
      return synth_guy;
    } else {
      LLDB_LOGF(log,
                "[ValueObjectSynthetic::GetChildAtIndex] name=%s, child at "
                "index %u not cached and cannot "
                "be created (can_create = %s, synth_filter = %p)",
                GetName().AsCString(), idx, can_create ? "yes" : "no",
                static_cast<void *>(m_synth_filter_up.get()));

      return lldb::ValueObjectSP();
    }
  } else {
    LLDB_LOGF(log,
              "[ValueObjectSynthetic::GetChildAtIndex] name=%s, child at "
              "index %u cached as %p",
              GetName().AsCString(), idx, static_cast<void *>(valobj));

    return valobj->GetSP();
  }
}

lldb::ValueObjectSP
ValueObjectSynthetic::GetChildMemberWithName(llvm::StringRef name,
                                             bool can_create) {
  UpdateValueIfNeeded();

  uint32_t index = GetIndexOfChildWithName(name);

  if (index == UINT32_MAX)
    return lldb::ValueObjectSP();

  return GetChildAtIndex(index, can_create);
}

size_t ValueObjectSynthetic::GetIndexOfChildWithName(llvm::StringRef name_ref) {
  UpdateValueIfNeeded();

  ConstString name(name_ref);

  uint32_t found_index = UINT32_MAX;
  bool did_find;
  {
    std::lock_guard<std::mutex> guard(m_child_mutex);
    auto name_to_index = m_name_toindex.find(name.GetCString());
    did_find = name_to_index != m_name_toindex.end();
    if (did_find)
      found_index = name_to_index->second;
  }

  if (!did_find && m_synth_filter_up != nullptr) {
    uint32_t index = m_synth_filter_up->GetIndexOfChildWithName(name);
    if (index == UINT32_MAX)
      return index;
    std::lock_guard<std::mutex> guard(m_child_mutex);
    m_name_toindex[name.GetCString()] = index;
    return index;
  } else if (!did_find && m_synth_filter_up == nullptr)
    return UINT32_MAX;
  else /*if (iter != m_name_toindex.end())*/
    return found_index;
}

bool ValueObjectSynthetic::IsInScope() { return m_parent->IsInScope(); }

lldb::ValueObjectSP ValueObjectSynthetic::GetNonSyntheticValue() {
  return m_parent->GetSP();
}

void ValueObjectSynthetic::CopyValueData(ValueObject *source) {
  m_value = (source->UpdateValueIfNeeded(), source->GetValue());
  ExecutionContext exe_ctx(GetExecutionContextRef());
  m_error = m_value.GetValueAsData(&exe_ctx, m_data, GetModule().get());
}

bool ValueObjectSynthetic::CanProvideValue() {
  if (!UpdateValueIfNeeded())
    return false;
  if (m_provides_value == eLazyBoolYes)
    return true;
  return m_parent->CanProvideValue();
}

bool ValueObjectSynthetic::SetValueFromCString(const char *value_str,
                                               Status &error) {
  return m_parent->SetValueFromCString(value_str, error);
}

void ValueObjectSynthetic::SetFormat(lldb::Format format) {
  if (m_parent) {
    m_parent->ClearUserVisibleData(eClearUserVisibleDataItemsAll);
    m_parent->SetFormat(format);
  }
  this->ValueObject::SetFormat(format);
  this->ClearUserVisibleData(eClearUserVisibleDataItemsAll);
}

void ValueObjectSynthetic::SetPreferredDisplayLanguage(
    lldb::LanguageType lang) {
  this->ValueObject::SetPreferredDisplayLanguage(lang);
  if (m_parent)
    m_parent->SetPreferredDisplayLanguage(lang);
}

lldb::LanguageType ValueObjectSynthetic::GetPreferredDisplayLanguage() {
  if (m_preferred_display_language == lldb::eLanguageTypeUnknown) {
    if (m_parent)
      return m_parent->GetPreferredDisplayLanguage();
    return lldb::eLanguageTypeUnknown;
  } else
    return m_preferred_display_language;
}

bool ValueObjectSynthetic::IsSyntheticChildrenGenerated() {
  if (m_parent)
    return m_parent->IsSyntheticChildrenGenerated();
  return false;
}

void ValueObjectSynthetic::SetSyntheticChildrenGenerated(bool b) {
  if (m_parent)
    m_parent->SetSyntheticChildrenGenerated(b);
  this->ValueObject::SetSyntheticChildrenGenerated(b);
}

bool ValueObjectSynthetic::GetDeclaration(Declaration &decl) {
  if (m_parent)
    return m_parent->GetDeclaration(decl);

  return ValueObject::GetDeclaration(decl);
}

uint64_t ValueObjectSynthetic::GetLanguageFlags() {
  if (m_parent)
    return m_parent->GetLanguageFlags();
  return this->ValueObject::GetLanguageFlags();
}

void ValueObjectSynthetic::SetLanguageFlags(uint64_t flags) {
  if (m_parent)
    m_parent->SetLanguageFlags(flags);
  else
    this->ValueObject::SetLanguageFlags(flags);
}
