//===-- SBTypeSynthetic.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTypeSynthetic.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBStream.h"

#include "lldb/DataFormatters/DataVisualization.h"

using namespace lldb;
using namespace lldb_private;

SBTypeSynthetic::SBTypeSynthetic() { LLDB_INSTRUMENT_VA(this); }

SBTypeSynthetic SBTypeSynthetic::CreateWithClassName(const char *data,
                                                     uint32_t options) {
  LLDB_INSTRUMENT_VA(data, options);

  if (!data || data[0] == 0)
    return SBTypeSynthetic();
  return SBTypeSynthetic(ScriptedSyntheticChildrenSP(
      new ScriptedSyntheticChildren(options, data, "")));
}

SBTypeSynthetic SBTypeSynthetic::CreateWithScriptCode(const char *data,
                                                      uint32_t options) {
  LLDB_INSTRUMENT_VA(data, options);

  if (!data || data[0] == 0)
    return SBTypeSynthetic();
  return SBTypeSynthetic(ScriptedSyntheticChildrenSP(
      new ScriptedSyntheticChildren(options, "", data)));
}

SBTypeSynthetic::SBTypeSynthetic(const lldb::SBTypeSynthetic &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBTypeSynthetic::~SBTypeSynthetic() = default;

bool SBTypeSynthetic::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBTypeSynthetic::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get() != nullptr;
}

bool SBTypeSynthetic::IsClassCode() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  const char *code = m_opaque_sp->GetPythonCode();
  return (code && *code);
}

bool SBTypeSynthetic::IsClassName() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;
  return !IsClassCode();
}

const char *SBTypeSynthetic::GetData() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return nullptr;
  if (IsClassCode())
    return ConstString(m_opaque_sp->GetPythonCode()).GetCString();

  return ConstString(m_opaque_sp->GetPythonClassName()).GetCString();
}

void SBTypeSynthetic::SetClassName(const char *data) {
  LLDB_INSTRUMENT_VA(this, data);

  if (IsValid() && data && *data)
    m_opaque_sp->SetPythonClassName(data);
}

void SBTypeSynthetic::SetClassCode(const char *data) {
  LLDB_INSTRUMENT_VA(this, data);

  if (IsValid() && data && *data)
    m_opaque_sp->SetPythonCode(data);
}

uint32_t SBTypeSynthetic::GetOptions() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return lldb::eTypeOptionNone;
  return m_opaque_sp->GetOptions();
}

void SBTypeSynthetic::SetOptions(uint32_t value) {
  LLDB_INSTRUMENT_VA(this, value);

  if (!CopyOnWrite_Impl())
    return;
  m_opaque_sp->SetOptions(value);
}

bool SBTypeSynthetic::GetDescription(lldb::SBStream &description,
                                     lldb::DescriptionLevel description_level) {
  LLDB_INSTRUMENT_VA(this, description, description_level);

  if (m_opaque_sp) {
    description.Printf("%s\n", m_opaque_sp->GetDescription().c_str());
    return true;
  }
  return false;
}

lldb::SBTypeSynthetic &SBTypeSynthetic::
operator=(const lldb::SBTypeSynthetic &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

bool SBTypeSynthetic::operator==(lldb::SBTypeSynthetic &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp == rhs.m_opaque_sp;
}

bool SBTypeSynthetic::IsEqualTo(lldb::SBTypeSynthetic &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();

  if (m_opaque_sp->IsScripted() != rhs.m_opaque_sp->IsScripted())
    return false;

  if (IsClassCode() != rhs.IsClassCode())
    return false;

  if (strcmp(GetData(), rhs.GetData()))
    return false;

  return GetOptions() == rhs.GetOptions();
}

bool SBTypeSynthetic::operator!=(lldb::SBTypeSynthetic &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp != rhs.m_opaque_sp;
}

lldb::ScriptedSyntheticChildrenSP SBTypeSynthetic::GetSP() {
  return m_opaque_sp;
}

void SBTypeSynthetic::SetSP(
    const lldb::ScriptedSyntheticChildrenSP &TypeSynthetic_impl_sp) {
  m_opaque_sp = TypeSynthetic_impl_sp;
}

SBTypeSynthetic::SBTypeSynthetic(
    const lldb::ScriptedSyntheticChildrenSP &TypeSynthetic_impl_sp)
    : m_opaque_sp(TypeSynthetic_impl_sp) {}

bool SBTypeSynthetic::CopyOnWrite_Impl() {
  if (!IsValid())
    return false;
  if (m_opaque_sp.use_count() == 1)
    return true;

  ScriptedSyntheticChildrenSP new_sp(new ScriptedSyntheticChildren(
      m_opaque_sp->GetOptions(), m_opaque_sp->GetPythonClassName(),
      m_opaque_sp->GetPythonCode()));

  SetSP(new_sp);

  return true;
}
