//===-- SBTypeFilter.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBTypeFilter.h"
#include "lldb/Utility/Instrumentation.h"

#include "lldb/API/SBStream.h"

#include "lldb/DataFormatters/DataVisualization.h"

using namespace lldb;
using namespace lldb_private;

SBTypeFilter::SBTypeFilter() { LLDB_INSTRUMENT_VA(this); }

SBTypeFilter::SBTypeFilter(uint32_t options)
    : m_opaque_sp(TypeFilterImplSP(new TypeFilterImpl(options))) {
  LLDB_INSTRUMENT_VA(this, options);
}

SBTypeFilter::SBTypeFilter(const lldb::SBTypeFilter &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBTypeFilter::~SBTypeFilter() = default;

bool SBTypeFilter::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBTypeFilter::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get() != nullptr;
}

uint32_t SBTypeFilter::GetOptions() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_sp->GetOptions();
  return 0;
}

void SBTypeFilter::SetOptions(uint32_t value) {
  LLDB_INSTRUMENT_VA(this, value);

  if (CopyOnWrite_Impl())
    m_opaque_sp->SetOptions(value);
}

bool SBTypeFilter::GetDescription(lldb::SBStream &description,
                                  lldb::DescriptionLevel description_level) {
  LLDB_INSTRUMENT_VA(this, description, description_level);

  if (!IsValid())
    return false;
  else {
    description.Printf("%s\n", m_opaque_sp->GetDescription().c_str());
    return true;
  }
}

void SBTypeFilter::Clear() {
  LLDB_INSTRUMENT_VA(this);

  if (CopyOnWrite_Impl())
    m_opaque_sp->Clear();
}

uint32_t SBTypeFilter::GetNumberOfExpressionPaths() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid())
    return m_opaque_sp->GetCount();
  return 0;
}

const char *SBTypeFilter::GetExpressionPathAtIndex(uint32_t i) {
  LLDB_INSTRUMENT_VA(this, i);

  if (!IsValid())
    return nullptr;

  const char *item = m_opaque_sp->GetExpressionPathAtIndex(i);
  if (item && *item == '.')
    item++;
  return ConstString(item).GetCString();
}

bool SBTypeFilter::ReplaceExpressionPathAtIndex(uint32_t i, const char *item) {
  LLDB_INSTRUMENT_VA(this, i, item);

  if (CopyOnWrite_Impl())
    return m_opaque_sp->SetExpressionPathAtIndex(i, item);
  else
    return false;
}

void SBTypeFilter::AppendExpressionPath(const char *item) {
  LLDB_INSTRUMENT_VA(this, item);

  if (CopyOnWrite_Impl())
    m_opaque_sp->AddExpressionPath(item);
}

lldb::SBTypeFilter &SBTypeFilter::operator=(const lldb::SBTypeFilter &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    m_opaque_sp = rhs.m_opaque_sp;
  }
  return *this;
}

bool SBTypeFilter::operator==(lldb::SBTypeFilter &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();

  return m_opaque_sp == rhs.m_opaque_sp;
}

bool SBTypeFilter::IsEqualTo(lldb::SBTypeFilter &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();

  if (GetNumberOfExpressionPaths() != rhs.GetNumberOfExpressionPaths())
    return false;

  for (uint32_t j = 0; j < GetNumberOfExpressionPaths(); j++)
    if (strcmp(GetExpressionPathAtIndex(j), rhs.GetExpressionPathAtIndex(j)) !=
        0)
      return false;

  return GetOptions() == rhs.GetOptions();
}

bool SBTypeFilter::operator!=(lldb::SBTypeFilter &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (!IsValid())
    return !rhs.IsValid();

  return m_opaque_sp != rhs.m_opaque_sp;
}

lldb::TypeFilterImplSP SBTypeFilter::GetSP() { return m_opaque_sp; }

void SBTypeFilter::SetSP(const lldb::TypeFilterImplSP &typefilter_impl_sp) {
  m_opaque_sp = typefilter_impl_sp;
}

SBTypeFilter::SBTypeFilter(const lldb::TypeFilterImplSP &typefilter_impl_sp)
    : m_opaque_sp(typefilter_impl_sp) {}

bool SBTypeFilter::CopyOnWrite_Impl() {
  if (!IsValid())
    return false;
  if (m_opaque_sp.use_count() == 1)
    return true;

  TypeFilterImplSP new_sp(new TypeFilterImpl(GetOptions()));

  for (uint32_t j = 0; j < GetNumberOfExpressionPaths(); j++)
    new_sp->AddExpressionPath(GetExpressionPathAtIndex(j));

  SetSP(new_sp);

  return true;
}
