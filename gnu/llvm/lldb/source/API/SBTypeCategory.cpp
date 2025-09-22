//===-- SBTypeCategory.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTypeCategory.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBStream.h"
#include "lldb/API/SBTypeFilter.h"
#include "lldb/API/SBTypeFormat.h"
#include "lldb/API/SBTypeNameSpecifier.h"
#include "lldb/API/SBTypeSummary.h"
#include "lldb/API/SBTypeSynthetic.h"

#include "lldb/Core/Debugger.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"

using namespace lldb;
using namespace lldb_private;

typedef std::pair<lldb::TypeCategoryImplSP, user_id_t> ImplType;

SBTypeCategory::SBTypeCategory() { LLDB_INSTRUMENT_VA(this); }

SBTypeCategory::SBTypeCategory(const char *name) {
  DataVisualization::Categories::GetCategory(ConstString(name), m_opaque_sp);
}

SBTypeCategory::SBTypeCategory(const lldb::SBTypeCategory &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBTypeCategory::~SBTypeCategory() = default;

bool SBTypeCategory::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBTypeCategory::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_sp.get() != nullptr);
}

bool SBTypeCategory::GetEnabled() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return m_opaque_sp->IsEnabled();
}

void SBTypeCategory::SetEnabled(bool enabled) {
  LLDB_INSTRUMENT_VA(this, enabled);

  if (!IsValid())
    return;
  if (enabled)
    DataVisualization::Categories::Enable(m_opaque_sp);
  else
    DataVisualization::Categories::Disable(m_opaque_sp);
}

const char *SBTypeCategory::GetName() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return nullptr;
  return ConstString(m_opaque_sp->GetName()).GetCString();
}

lldb::LanguageType SBTypeCategory::GetLanguageAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  if (IsValid())
    return m_opaque_sp->GetLanguageAtIndex(idx);
  return lldb::eLanguageTypeUnknown;
}

uint32_t SBTypeCategory::GetNumLanguages() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_sp->GetNumLanguages();
  return 0;
}

void SBTypeCategory::AddLanguage(lldb::LanguageType language) {
  LLDB_INSTRUMENT_VA(this, language);

  if (IsValid())
    m_opaque_sp->AddLanguage(language);
}

uint32_t SBTypeCategory::GetNumFormats() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return 0;

  return m_opaque_sp->GetNumFormats();
}

uint32_t SBTypeCategory::GetNumSummaries() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return 0;
  return m_opaque_sp->GetNumSummaries();
}

uint32_t SBTypeCategory::GetNumFilters() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return 0;
  return m_opaque_sp->GetNumFilters();
}

uint32_t SBTypeCategory::GetNumSynthetics() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return 0;
  return m_opaque_sp->GetNumSynthetics();
}

lldb::SBTypeNameSpecifier
SBTypeCategory::GetTypeNameSpecifierForFilterAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  if (!IsValid())
    return SBTypeNameSpecifier();
  return SBTypeNameSpecifier(
      m_opaque_sp->GetTypeNameSpecifierForFilterAtIndex(index));
}

lldb::SBTypeNameSpecifier
SBTypeCategory::GetTypeNameSpecifierForFormatAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  if (!IsValid())
    return SBTypeNameSpecifier();
  return SBTypeNameSpecifier(
      m_opaque_sp->GetTypeNameSpecifierForFormatAtIndex(index));
}

lldb::SBTypeNameSpecifier
SBTypeCategory::GetTypeNameSpecifierForSummaryAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  if (!IsValid())
    return SBTypeNameSpecifier();
  return SBTypeNameSpecifier(
      m_opaque_sp->GetTypeNameSpecifierForSummaryAtIndex(index));
}

lldb::SBTypeNameSpecifier
SBTypeCategory::GetTypeNameSpecifierForSyntheticAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  if (!IsValid())
    return SBTypeNameSpecifier();
  return SBTypeNameSpecifier(
      m_opaque_sp->GetTypeNameSpecifierForSyntheticAtIndex(index));
}

SBTypeFilter SBTypeCategory::GetFilterForType(SBTypeNameSpecifier spec) {
  LLDB_INSTRUMENT_VA(this, spec);

  if (!IsValid())
    return SBTypeFilter();

  if (!spec.IsValid())
    return SBTypeFilter();

  lldb::TypeFilterImplSP children_sp =
      m_opaque_sp->GetFilterForType(spec.GetSP());

  if (!children_sp)
    return lldb::SBTypeFilter();

  TypeFilterImplSP filter_sp =
      std::static_pointer_cast<TypeFilterImpl>(children_sp);

  return lldb::SBTypeFilter(filter_sp);
}
SBTypeFormat SBTypeCategory::GetFormatForType(SBTypeNameSpecifier spec) {
  LLDB_INSTRUMENT_VA(this, spec);

  if (!IsValid())
    return SBTypeFormat();

  if (!spec.IsValid())
    return SBTypeFormat();

  lldb::TypeFormatImplSP format_sp =
      m_opaque_sp->GetFormatForType(spec.GetSP());

  if (!format_sp)
    return lldb::SBTypeFormat();

  return lldb::SBTypeFormat(format_sp);
}

SBTypeSummary SBTypeCategory::GetSummaryForType(SBTypeNameSpecifier spec) {
  LLDB_INSTRUMENT_VA(this, spec);

  if (!IsValid())
    return SBTypeSummary();

  if (!spec.IsValid())
    return SBTypeSummary();

  lldb::TypeSummaryImplSP summary_sp =
      m_opaque_sp->GetSummaryForType(spec.GetSP());

  if (!summary_sp)
    return lldb::SBTypeSummary();

  return lldb::SBTypeSummary(summary_sp);
}

SBTypeSynthetic SBTypeCategory::GetSyntheticForType(SBTypeNameSpecifier spec) {
  LLDB_INSTRUMENT_VA(this, spec);

  if (!IsValid())
    return SBTypeSynthetic();

  if (!spec.IsValid())
    return SBTypeSynthetic();

  lldb::SyntheticChildrenSP children_sp =
      m_opaque_sp->GetSyntheticForType(spec.GetSP());

  if (!children_sp)
    return lldb::SBTypeSynthetic();

  ScriptedSyntheticChildrenSP synth_sp =
      std::static_pointer_cast<ScriptedSyntheticChildren>(children_sp);

  return lldb::SBTypeSynthetic(synth_sp);
}

SBTypeFilter SBTypeCategory::GetFilterAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  if (!IsValid())
    return SBTypeFilter();
  lldb::SyntheticChildrenSP children_sp =
      m_opaque_sp->GetSyntheticAtIndex((index));

  if (!children_sp.get())
    return lldb::SBTypeFilter();

  TypeFilterImplSP filter_sp =
      std::static_pointer_cast<TypeFilterImpl>(children_sp);

  return lldb::SBTypeFilter(filter_sp);
}

SBTypeFormat SBTypeCategory::GetFormatAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  if (!IsValid())
    return SBTypeFormat();
  return SBTypeFormat(m_opaque_sp->GetFormatAtIndex((index)));
}

SBTypeSummary SBTypeCategory::GetSummaryAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  if (!IsValid())
    return SBTypeSummary();
  return SBTypeSummary(m_opaque_sp->GetSummaryAtIndex((index)));
}

SBTypeSynthetic SBTypeCategory::GetSyntheticAtIndex(uint32_t index) {
  LLDB_INSTRUMENT_VA(this, index);

  if (!IsValid())
    return SBTypeSynthetic();
  lldb::SyntheticChildrenSP children_sp =
      m_opaque_sp->GetSyntheticAtIndex((index));

  if (!children_sp.get())
    return lldb::SBTypeSynthetic();

  ScriptedSyntheticChildrenSP synth_sp =
      std::static_pointer_cast<ScriptedSyntheticChildren>(children_sp);

  return lldb::SBTypeSynthetic(synth_sp);
}

bool SBTypeCategory::AddTypeFormat(SBTypeNameSpecifier type_name,
                                   SBTypeFormat format) {
  LLDB_INSTRUMENT_VA(this, type_name, format);

  if (!IsValid())
    return false;

  if (!type_name.IsValid())
    return false;

  if (!format.IsValid())
    return false;

  m_opaque_sp->AddTypeFormat(type_name.GetSP(), format.GetSP());
  return true;
}

bool SBTypeCategory::DeleteTypeFormat(SBTypeNameSpecifier type_name) {
  LLDB_INSTRUMENT_VA(this, type_name);

  if (!IsValid())
    return false;

  if (!type_name.IsValid())
    return false;

  return m_opaque_sp->DeleteTypeFormat(type_name.GetSP());
}

bool SBTypeCategory::AddTypeSummary(SBTypeNameSpecifier type_name,
                                    SBTypeSummary summary) {
  LLDB_INSTRUMENT_VA(this, type_name, summary);

  if (!IsValid())
    return false;

  if (!type_name.IsValid())
    return false;

  if (!summary.IsValid())
    return false;

  // FIXME: we need to iterate over all the Debugger objects and have each of
  // them contain a copy of the function
  // since we currently have formatters live in a global space, while Python
  // code lives in a specific Debugger-related environment this should
  // eventually be fixed by deciding a final location in the LLDB object space
  // for formatters
  if (summary.IsFunctionCode()) {
    const void *name_token =
        (const void *)ConstString(type_name.GetName()).GetCString();
    const char *script = summary.GetData();
    StringList input;
    input.SplitIntoLines(script, strlen(script));
    uint32_t num_debuggers = lldb_private::Debugger::GetNumDebuggers();
    bool need_set = true;
    for (uint32_t j = 0; j < num_debuggers; j++) {
      DebuggerSP debugger_sp = lldb_private::Debugger::GetDebuggerAtIndex(j);
      if (debugger_sp) {
        ScriptInterpreter *interpreter_ptr =
            debugger_sp->GetScriptInterpreter();
        if (interpreter_ptr) {
          std::string output;
          if (interpreter_ptr->GenerateTypeScriptFunction(input, output,
                                                          name_token) &&
              !output.empty()) {
            if (need_set) {
              need_set = false;
              summary.SetFunctionName(output.c_str());
            }
          }
        }
      }
    }
  }

  m_opaque_sp->AddTypeSummary(type_name.GetSP(), summary.GetSP());
  return true;
}

bool SBTypeCategory::DeleteTypeSummary(SBTypeNameSpecifier type_name) {
  LLDB_INSTRUMENT_VA(this, type_name);

  if (!IsValid())
    return false;

  if (!type_name.IsValid())
    return false;

  return m_opaque_sp->DeleteTypeSummary(type_name.GetSP());
}

bool SBTypeCategory::AddTypeFilter(SBTypeNameSpecifier type_name,
                                   SBTypeFilter filter) {
  LLDB_INSTRUMENT_VA(this, type_name, filter);

  if (!IsValid())
    return false;

  if (!type_name.IsValid())
    return false;

  if (!filter.IsValid())
    return false;

  m_opaque_sp->AddTypeFilter(type_name.GetSP(), filter.GetSP());
  return true;
}

bool SBTypeCategory::DeleteTypeFilter(SBTypeNameSpecifier type_name) {
  LLDB_INSTRUMENT_VA(this, type_name);

  if (!IsValid())
    return false;

  if (!type_name.IsValid())
    return false;

  return m_opaque_sp->DeleteTypeFilter(type_name.GetSP());
}

bool SBTypeCategory::AddTypeSynthetic(SBTypeNameSpecifier type_name,
                                      SBTypeSynthetic synth) {
  LLDB_INSTRUMENT_VA(this, type_name, synth);

  if (!IsValid())
    return false;

  if (!type_name.IsValid())
    return false;

  if (!synth.IsValid())
    return false;

  // FIXME: we need to iterate over all the Debugger objects and have each of
  // them contain a copy of the function
  // since we currently have formatters live in a global space, while Python
  // code lives in a specific Debugger-related environment this should
  // eventually be fixed by deciding a final location in the LLDB object space
  // for formatters
  if (synth.IsClassCode()) {
    const void *name_token =
        (const void *)ConstString(type_name.GetName()).GetCString();
    const char *script = synth.GetData();
    StringList input;
    input.SplitIntoLines(script, strlen(script));
    uint32_t num_debuggers = lldb_private::Debugger::GetNumDebuggers();
    bool need_set = true;
    for (uint32_t j = 0; j < num_debuggers; j++) {
      DebuggerSP debugger_sp = lldb_private::Debugger::GetDebuggerAtIndex(j);
      if (debugger_sp) {
        ScriptInterpreter *interpreter_ptr =
            debugger_sp->GetScriptInterpreter();
        if (interpreter_ptr) {
          std::string output;
          if (interpreter_ptr->GenerateTypeSynthClass(input, output,
                                                      name_token) &&
              !output.empty()) {
            if (need_set) {
              need_set = false;
              synth.SetClassName(output.c_str());
            }
          }
        }
      }
    }
  }

  m_opaque_sp->AddTypeSynthetic(type_name.GetSP(), synth.GetSP());
  return true;
}

bool SBTypeCategory::DeleteTypeSynthetic(SBTypeNameSpecifier type_name) {
  LLDB_INSTRUMENT_VA(this, type_name);

  if (!IsValid())
    return false;

  if (!type_name.IsValid())
    return false;

  return m_opaque_sp->DeleteTypeSynthetic(type_name.GetSP());
}

bool SBTypeCategory::GetDescription(lldb::SBStream &description,
                                    lldb::DescriptionLevel description_level) {
  LLDB_INSTRUMENT_VA(this, description, description_level);

  if (!IsValid())
    return false;
  description.Printf("Category name: %s\n", GetName());
  return true;
}

lldb::SBTypeCategory &SBTypeCategory::
operator=(const lldb::SBTypeCategory &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

bool SBTypeCategory::operator==(lldb::SBTypeCategory &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();

  return m_opaque_sp.get() == rhs.m_opaque_sp.get();
}

bool SBTypeCategory::operator!=(lldb::SBTypeCategory &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return rhs.IsValid();

  return m_opaque_sp.get() != rhs.m_opaque_sp.get();
}

lldb::TypeCategoryImplSP SBTypeCategory::GetSP() {
  if (!IsValid())
    return lldb::TypeCategoryImplSP();
  return m_opaque_sp;
}

void SBTypeCategory::SetSP(
    const lldb::TypeCategoryImplSP &typecategory_impl_sp) {
  m_opaque_sp = typecategory_impl_sp;
}

SBTypeCategory::SBTypeCategory(
    const lldb::TypeCategoryImplSP &typecategory_impl_sp)
    : m_opaque_sp(typecategory_impl_sp) {}

bool SBTypeCategory::IsDefaultCategory() {
  if (!IsValid())
    return false;

  return (strcmp(m_opaque_sp->GetName(), "default") == 0);
}
