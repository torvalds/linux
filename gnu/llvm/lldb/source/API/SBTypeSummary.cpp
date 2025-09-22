//===-- SBTypeSummary.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTypeSummary.h"
#include "Utils.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBValue.h"
#include "lldb/DataFormatters/DataVisualization.h"
#include "lldb/Utility/Instrumentation.h"

#include "llvm/Support/Casting.h"

using namespace lldb;
using namespace lldb_private;

SBTypeSummaryOptions::SBTypeSummaryOptions() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_up = std::make_unique<TypeSummaryOptions>();
}

SBTypeSummaryOptions::SBTypeSummaryOptions(
    const lldb::SBTypeSummaryOptions &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBTypeSummaryOptions::~SBTypeSummaryOptions() = default;

bool SBTypeSummaryOptions::IsValid() {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBTypeSummaryOptions::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up.get();
}

lldb::LanguageType SBTypeSummaryOptions::GetLanguage() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_up->GetLanguage();
  return lldb::eLanguageTypeUnknown;
}

lldb::TypeSummaryCapping SBTypeSummaryOptions::GetCapping() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_up->GetCapping();
  return eTypeSummaryCapped;
}

void SBTypeSummaryOptions::SetLanguage(lldb::LanguageType l) {
  LLDB_INSTRUMENT_VA(this, l);

  if (IsValid())
    m_opaque_up->SetLanguage(l);
}

void SBTypeSummaryOptions::SetCapping(lldb::TypeSummaryCapping c) {
  LLDB_INSTRUMENT_VA(this, c);

  if (IsValid())
    m_opaque_up->SetCapping(c);
}

lldb_private::TypeSummaryOptions *SBTypeSummaryOptions::operator->() {
  return m_opaque_up.get();
}

const lldb_private::TypeSummaryOptions *SBTypeSummaryOptions::
operator->() const {
  return m_opaque_up.get();
}

lldb_private::TypeSummaryOptions *SBTypeSummaryOptions::get() {
  return m_opaque_up.get();
}

lldb_private::TypeSummaryOptions &SBTypeSummaryOptions::ref() {
  return *m_opaque_up;
}

const lldb_private::TypeSummaryOptions &SBTypeSummaryOptions::ref() const {
  return *m_opaque_up;
}

SBTypeSummaryOptions::SBTypeSummaryOptions(
    const lldb_private::TypeSummaryOptions &lldb_object)
    : m_opaque_up(std::make_unique<TypeSummaryOptions>(lldb_object)) {
  LLDB_INSTRUMENT_VA(this, lldb_object);
}

SBTypeSummary::SBTypeSummary() { LLDB_INSTRUMENT_VA(this); }

SBTypeSummary SBTypeSummary::CreateWithSummaryString(const char *data,
                                                     uint32_t options) {
  LLDB_INSTRUMENT_VA(data, options);

  if (!data || data[0] == 0)
    return SBTypeSummary();

  return SBTypeSummary(
      TypeSummaryImplSP(new StringSummaryFormat(options, data)));
}

SBTypeSummary SBTypeSummary::CreateWithFunctionName(const char *data,
                                                    uint32_t options) {
  LLDB_INSTRUMENT_VA(data, options);

  if (!data || data[0] == 0)
    return SBTypeSummary();

  return SBTypeSummary(
      TypeSummaryImplSP(new ScriptSummaryFormat(options, data)));
}

SBTypeSummary SBTypeSummary::CreateWithScriptCode(const char *data,
                                                  uint32_t options) {
  LLDB_INSTRUMENT_VA(data, options);

  if (!data || data[0] == 0)
    return SBTypeSummary();

  return SBTypeSummary(
      TypeSummaryImplSP(new ScriptSummaryFormat(options, "", data)));
}

SBTypeSummary SBTypeSummary::CreateWithCallback(FormatCallback cb,
                                                uint32_t options,
                                                const char *description) {
  LLDB_INSTRUMENT_VA(cb, options, description);

  SBTypeSummary retval;
  if (cb) {
    retval.SetSP(TypeSummaryImplSP(new CXXFunctionSummaryFormat(
        options,
        [cb](ValueObject &valobj, Stream &stm,
             const TypeSummaryOptions &opt) -> bool {
          SBStream stream;
          SBValue sb_value(valobj.GetSP());
          SBTypeSummaryOptions options(opt);
          if (!cb(sb_value, options, stream))
            return false;
          stm.Write(stream.GetData(), stream.GetSize());
          return true;
        },
        description ? description : "callback summary formatter")));
  }

  return retval;
}

SBTypeSummary::SBTypeSummary(const lldb::SBTypeSummary &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBTypeSummary::~SBTypeSummary() = default;

bool SBTypeSummary::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBTypeSummary::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get() != nullptr;
}

bool SBTypeSummary::IsFunctionCode() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  if (ScriptSummaryFormat *script_summary_ptr =
          llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get())) {
    const char *ftext = script_summary_ptr->GetPythonScript();
    return (ftext && *ftext != 0);
  }
  return false;
}

bool SBTypeSummary::IsFunctionName() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  if (ScriptSummaryFormat *script_summary_ptr =
          llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get())) {
    const char *ftext = script_summary_ptr->GetPythonScript();
    return (!ftext || *ftext == 0);
  }
  return false;
}

bool SBTypeSummary::IsSummaryString() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;

  return m_opaque_sp->GetKind() == TypeSummaryImpl::Kind::eSummaryString;
}

const char *SBTypeSummary::GetData() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return nullptr;
  if (ScriptSummaryFormat *script_summary_ptr =
          llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get())) {
    const char *fname = script_summary_ptr->GetFunctionName();
    const char *ftext = script_summary_ptr->GetPythonScript();
    if (ftext && *ftext)
      return ConstString(ftext).GetCString();
    return ConstString(fname).GetCString();
  } else if (StringSummaryFormat *string_summary_ptr =
                 llvm::dyn_cast<StringSummaryFormat>(m_opaque_sp.get()))
    return ConstString(string_summary_ptr->GetSummaryString()).GetCString();
  return nullptr;
}

uint32_t SBTypeSummary::GetOptions() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return lldb::eTypeOptionNone;
  return m_opaque_sp->GetOptions();
}

void SBTypeSummary::SetOptions(uint32_t value) {
  LLDB_INSTRUMENT_VA(this, value);

  if (!CopyOnWrite_Impl())
    return;
  m_opaque_sp->SetOptions(value);
}

void SBTypeSummary::SetSummaryString(const char *data) {
  LLDB_INSTRUMENT_VA(this, data);

  if (!IsValid())
    return;
  if (!llvm::isa<StringSummaryFormat>(m_opaque_sp.get()))
    ChangeSummaryType(false);
  if (StringSummaryFormat *string_summary_ptr =
          llvm::dyn_cast<StringSummaryFormat>(m_opaque_sp.get()))
    string_summary_ptr->SetSummaryString(data);
}

void SBTypeSummary::SetFunctionName(const char *data) {
  LLDB_INSTRUMENT_VA(this, data);

  if (!IsValid())
    return;
  if (!llvm::isa<ScriptSummaryFormat>(m_opaque_sp.get()))
    ChangeSummaryType(true);
  if (ScriptSummaryFormat *script_summary_ptr =
          llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get()))
    script_summary_ptr->SetFunctionName(data);
}

void SBTypeSummary::SetFunctionCode(const char *data) {
  LLDB_INSTRUMENT_VA(this, data);

  if (!IsValid())
    return;
  if (!llvm::isa<ScriptSummaryFormat>(m_opaque_sp.get()))
    ChangeSummaryType(true);
  if (ScriptSummaryFormat *script_summary_ptr =
          llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get()))
    script_summary_ptr->SetPythonScript(data);
}

bool SBTypeSummary::GetDescription(lldb::SBStream &description,
                                   lldb::DescriptionLevel description_level) {
  LLDB_INSTRUMENT_VA(this, description, description_level);

  if (!CopyOnWrite_Impl())
    return false;
  else {
    description.Printf("%s\n", m_opaque_sp->GetDescription().c_str());
    return true;
  }
}

bool SBTypeSummary::DoesPrintValue(lldb::SBValue value) {
  LLDB_INSTRUMENT_VA(this, value);

  if (!IsValid())
    return false;
  lldb::ValueObjectSP value_sp = value.GetSP();
  return m_opaque_sp->DoesPrintValue(value_sp.get());
}

lldb::SBTypeSummary &SBTypeSummary::operator=(const lldb::SBTypeSummary &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

bool SBTypeSummary::operator==(lldb::SBTypeSummary &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp == rhs.m_opaque_sp;
}

bool SBTypeSummary::IsEqualTo(lldb::SBTypeSummary &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (IsValid()) {
    // valid and invalid are different
    if (!rhs.IsValid())
      return false;
  } else {
    // invalid and valid are different
    if (rhs.IsValid())
      return false;
    else
      // both invalid are the same
      return true;
  }

  if (m_opaque_sp->GetKind() != rhs.m_opaque_sp->GetKind())
    return false;

  switch (m_opaque_sp->GetKind()) {
  case TypeSummaryImpl::Kind::eCallback:
    return llvm::dyn_cast<CXXFunctionSummaryFormat>(m_opaque_sp.get()) ==
           llvm::dyn_cast<CXXFunctionSummaryFormat>(rhs.m_opaque_sp.get());
  case TypeSummaryImpl::Kind::eScript:
    if (IsFunctionCode() != rhs.IsFunctionCode())
      return false;
    if (IsFunctionName() != rhs.IsFunctionName())
      return false;
    return GetOptions() == rhs.GetOptions();
  case TypeSummaryImpl::Kind::eSummaryString:
    if (IsSummaryString() != rhs.IsSummaryString())
      return false;
    return GetOptions() == rhs.GetOptions();
  case TypeSummaryImpl::Kind::eInternal:
    return (m_opaque_sp.get() == rhs.m_opaque_sp.get());
  }

  return false;
}

bool SBTypeSummary::operator!=(lldb::SBTypeSummary &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp != rhs.m_opaque_sp;
}

lldb::TypeSummaryImplSP SBTypeSummary::GetSP() { return m_opaque_sp; }

void SBTypeSummary::SetSP(const lldb::TypeSummaryImplSP &typesummary_impl_sp) {
  m_opaque_sp = typesummary_impl_sp;
}

SBTypeSummary::SBTypeSummary(const lldb::TypeSummaryImplSP &typesummary_impl_sp)
    : m_opaque_sp(typesummary_impl_sp) {}

bool SBTypeSummary::CopyOnWrite_Impl() {
  if (!IsValid())
    return false;

  if (m_opaque_sp.use_count() == 1)
    return true;

  TypeSummaryImplSP new_sp;

  if (CXXFunctionSummaryFormat *current_summary_ptr =
          llvm::dyn_cast<CXXFunctionSummaryFormat>(m_opaque_sp.get())) {
    new_sp = TypeSummaryImplSP(new CXXFunctionSummaryFormat(
        GetOptions(), current_summary_ptr->m_impl,
        current_summary_ptr->m_description.c_str()));
  } else if (ScriptSummaryFormat *current_summary_ptr =
                 llvm::dyn_cast<ScriptSummaryFormat>(m_opaque_sp.get())) {
    new_sp = TypeSummaryImplSP(new ScriptSummaryFormat(
        GetOptions(), current_summary_ptr->GetFunctionName(),
        current_summary_ptr->GetPythonScript()));
  } else if (StringSummaryFormat *current_summary_ptr =
                 llvm::dyn_cast<StringSummaryFormat>(m_opaque_sp.get())) {
    new_sp = TypeSummaryImplSP(new StringSummaryFormat(
        GetOptions(), current_summary_ptr->GetSummaryString()));
  }

  SetSP(new_sp);

  return nullptr != new_sp.get();
}

bool SBTypeSummary::ChangeSummaryType(bool want_script) {
  if (!IsValid())
    return false;

  TypeSummaryImplSP new_sp;

  if (want_script ==
      (m_opaque_sp->GetKind() == TypeSummaryImpl::Kind::eScript)) {
    if (m_opaque_sp->GetKind() ==
            lldb_private::TypeSummaryImpl::Kind::eCallback &&
        !want_script)
      new_sp = TypeSummaryImplSP(new StringSummaryFormat(GetOptions(), ""));
    else
      return CopyOnWrite_Impl();
  }

  if (!new_sp) {
    if (want_script)
      new_sp = TypeSummaryImplSP(new ScriptSummaryFormat(GetOptions(), "", ""));
    else
      new_sp = TypeSummaryImplSP(new StringSummaryFormat(GetOptions(), ""));
  }

  SetSP(new_sp);

  return true;
}
