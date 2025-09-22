//===-- SBTypeNameSpecifier.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTypeNameSpecifier.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBStream.h"
#include "lldb/API/SBType.h"

#include "lldb/DataFormatters/DataVisualization.h"

using namespace lldb;
using namespace lldb_private;

SBTypeNameSpecifier::SBTypeNameSpecifier() { LLDB_INSTRUMENT_VA(this); }

SBTypeNameSpecifier::SBTypeNameSpecifier(const char *name, bool is_regex)
    : SBTypeNameSpecifier(name, is_regex ? eFormatterMatchRegex
                                         : eFormatterMatchExact) {
  LLDB_INSTRUMENT_VA(this, name, is_regex);
}

SBTypeNameSpecifier::SBTypeNameSpecifier(const char *name,
                                         FormatterMatchType match_type)
    : m_opaque_sp(new TypeNameSpecifierImpl(name, match_type)) {
  LLDB_INSTRUMENT_VA(this, name, match_type);

  if (name == nullptr || (*name) == 0)
    m_opaque_sp.reset();
}

SBTypeNameSpecifier::SBTypeNameSpecifier(SBType type) {
  LLDB_INSTRUMENT_VA(this, type);

  if (type.IsValid())
    m_opaque_sp = TypeNameSpecifierImplSP(
        new TypeNameSpecifierImpl(type.m_opaque_sp->GetCompilerType(true)));
}

SBTypeNameSpecifier::SBTypeNameSpecifier(const lldb::SBTypeNameSpecifier &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBTypeNameSpecifier::~SBTypeNameSpecifier() = default;

bool SBTypeNameSpecifier::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBTypeNameSpecifier::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get() != nullptr;
}

const char *SBTypeNameSpecifier::GetName() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return nullptr;

  return ConstString(m_opaque_sp->GetName()).GetCString();
}

SBType SBTypeNameSpecifier::GetType() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return SBType();
  lldb_private::CompilerType c_type = m_opaque_sp->GetCompilerType();
  if (c_type.IsValid())
    return SBType(c_type);
  return SBType();
}

FormatterMatchType SBTypeNameSpecifier::GetMatchType() {
  LLDB_INSTRUMENT_VA(this);
  if (!IsValid())
    return eFormatterMatchExact;
  return m_opaque_sp->GetMatchType();
}

bool SBTypeNameSpecifier::IsRegex() {
  LLDB_INSTRUMENT_VA(this);

  if (!IsValid())
    return false;

  return m_opaque_sp->GetMatchType() == eFormatterMatchRegex;
}

bool SBTypeNameSpecifier::GetDescription(
    lldb::SBStream &description, lldb::DescriptionLevel description_level) {
  LLDB_INSTRUMENT_VA(this, description, description_level);

  lldb::FormatterMatchType match_type = GetMatchType();
  const char *match_type_str =
      (match_type == eFormatterMatchExact   ? "plain"
       : match_type == eFormatterMatchRegex ? "regex"
                                            : "callback");
  if (!IsValid())
    return false;
  description.Printf("SBTypeNameSpecifier(%s,%s)", GetName(), match_type_str);
  return true;
}

lldb::SBTypeNameSpecifier &SBTypeNameSpecifier::
operator=(const lldb::SBTypeNameSpecifier &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

bool SBTypeNameSpecifier::operator==(lldb::SBTypeNameSpecifier &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp == rhs.m_opaque_sp;
}

bool SBTypeNameSpecifier::IsEqualTo(lldb::SBTypeNameSpecifier &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();

  if (GetMatchType() != rhs.GetMatchType())
    return false;
  if (GetName() == nullptr || rhs.GetName() == nullptr)
    return false;

  return (strcmp(GetName(), rhs.GetName()) == 0);
}

bool SBTypeNameSpecifier::operator!=(lldb::SBTypeNameSpecifier &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp != rhs.m_opaque_sp;
}

lldb::TypeNameSpecifierImplSP SBTypeNameSpecifier::GetSP() {
  return m_opaque_sp;
}

void SBTypeNameSpecifier::SetSP(
    const lldb::TypeNameSpecifierImplSP &type_namespec_sp) {
  m_opaque_sp = type_namespec_sp;
}

SBTypeNameSpecifier::SBTypeNameSpecifier(
    const lldb::TypeNameSpecifierImplSP &type_namespec_sp)
    : m_opaque_sp(type_namespec_sp) {}
