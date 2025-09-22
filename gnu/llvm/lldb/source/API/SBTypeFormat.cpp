//===-- SBTypeFormat.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTypeFormat.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBStream.h"

#include "lldb/DataFormatters/DataVisualization.h"

using namespace lldb;
using namespace lldb_private;

SBTypeFormat::SBTypeFormat() { LLDB_INSTRUMENT_VA(this); }

SBTypeFormat::SBTypeFormat(lldb::Format format, uint32_t options)
    : m_opaque_sp(
          TypeFormatImplSP(new TypeFormatImpl_Format(format, options))) {
  LLDB_INSTRUMENT_VA(this, format, options);
}

SBTypeFormat::SBTypeFormat(const char *type, uint32_t options)
    : m_opaque_sp(TypeFormatImplSP(new TypeFormatImpl_EnumType(
          ConstString(type ? type : ""), options))) {
  LLDB_INSTRUMENT_VA(this, type, options);
}

SBTypeFormat::SBTypeFormat(const lldb::SBTypeFormat &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBTypeFormat::~SBTypeFormat() = default;

bool SBTypeFormat::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBTypeFormat::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get() != nullptr;
}

lldb::Format SBTypeFormat::GetFormat() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid() && m_opaque_sp->GetType() == TypeFormatImpl::Type::eTypeFormat)
    return ((TypeFormatImpl_Format *)m_opaque_sp.get())->GetFormat();
  return lldb::eFormatInvalid;
}

const char *SBTypeFormat::GetTypeName() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid() && m_opaque_sp->GetType() == TypeFormatImpl::Type::eTypeEnum)
    return ((TypeFormatImpl_EnumType *)m_opaque_sp.get())
        ->GetTypeName()
        .AsCString("");
  return "";
}

uint32_t SBTypeFormat::GetOptions() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_sp->GetOptions();
  return 0;
}

void SBTypeFormat::SetFormat(lldb::Format fmt) {
  LLDB_INSTRUMENT_VA(this, fmt);

  if (CopyOnWrite_Impl(Type::eTypeFormat))
    ((TypeFormatImpl_Format *)m_opaque_sp.get())->SetFormat(fmt);
}

void SBTypeFormat::SetTypeName(const char *type) {
  LLDB_INSTRUMENT_VA(this, type);

  if (CopyOnWrite_Impl(Type::eTypeEnum))
    ((TypeFormatImpl_EnumType *)m_opaque_sp.get())
        ->SetTypeName(ConstString(type ? type : ""));
}

void SBTypeFormat::SetOptions(uint32_t value) {
  LLDB_INSTRUMENT_VA(this, value);

  if (CopyOnWrite_Impl(Type::eTypeKeepSame))
    m_opaque_sp->SetOptions(value);
}

bool SBTypeFormat::GetDescription(lldb::SBStream &description,
                                  lldb::DescriptionLevel description_level) {
  LLDB_INSTRUMENT_VA(this, description, description_level);

  if (!IsValid())
    return false;
  else {
    description.Printf("%s\n", m_opaque_sp->GetDescription().c_str());
    return true;
  }
}

lldb::SBTypeFormat &SBTypeFormat::operator=(const lldb::SBTypeFormat &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

bool SBTypeFormat::operator==(lldb::SBTypeFormat &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp == rhs.m_opaque_sp;
}

bool SBTypeFormat::IsEqualTo(lldb::SBTypeFormat &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();

  if (GetFormat() == rhs.GetFormat())
    return GetOptions() == rhs.GetOptions();
  else
    return false;
}

bool SBTypeFormat::operator!=(lldb::SBTypeFormat &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();
  return m_opaque_sp != rhs.m_opaque_sp;
}

lldb::TypeFormatImplSP SBTypeFormat::GetSP() { return m_opaque_sp; }

void SBTypeFormat::SetSP(const lldb::TypeFormatImplSP &typeformat_impl_sp) {
  m_opaque_sp = typeformat_impl_sp;
}

SBTypeFormat::SBTypeFormat(const lldb::TypeFormatImplSP &typeformat_impl_sp)
    : m_opaque_sp(typeformat_impl_sp) {}

bool SBTypeFormat::CopyOnWrite_Impl(Type type) {
  if (!IsValid())
    return false;

  if (m_opaque_sp.use_count() == 1 &&
      ((type == Type::eTypeKeepSame) ||
       (type == Type::eTypeFormat &&
        m_opaque_sp->GetType() == TypeFormatImpl::Type::eTypeFormat) ||
       (type == Type::eTypeEnum &&
        m_opaque_sp->GetType() == TypeFormatImpl::Type::eTypeEnum)))
    return true;

  if (type == Type::eTypeKeepSame) {
    if (m_opaque_sp->GetType() == TypeFormatImpl::Type::eTypeFormat)
      type = Type::eTypeFormat;
    else
      type = Type::eTypeEnum;
  }

  if (type == Type::eTypeFormat)
    SetSP(
        TypeFormatImplSP(new TypeFormatImpl_Format(GetFormat(), GetOptions())));
  else
    SetSP(TypeFormatImplSP(
        new TypeFormatImpl_EnumType(ConstString(GetTypeName()), GetOptions())));

  return true;
}
