//===-- SBSymbolContextList.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBSymbolContextList.h"
#include "Utils.h"
#include "lldb/API/SBStream.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/Instrumentation.h"

using namespace lldb;
using namespace lldb_private;

SBSymbolContextList::SBSymbolContextList()
    : m_opaque_up(new SymbolContextList()) {
  LLDB_INSTRUMENT_VA(this);
}

SBSymbolContextList::SBSymbolContextList(const SBSymbolContextList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBSymbolContextList::~SBSymbolContextList() = default;

const SBSymbolContextList &SBSymbolContextList::
operator=(const SBSymbolContextList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

uint32_t SBSymbolContextList::GetSize() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    return m_opaque_up->GetSize();
  return 0;
}

SBSymbolContext SBSymbolContextList::GetContextAtIndex(uint32_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBSymbolContext sb_sc;
  if (m_opaque_up) {
    SymbolContext sc;
    if (m_opaque_up->GetContextAtIndex(idx, sc))
      sb_sc = sc;
  }
  return sb_sc;
}

void SBSymbolContextList::Clear() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    m_opaque_up->Clear();
}

void SBSymbolContextList::Append(SBSymbolContext &sc) {
  LLDB_INSTRUMENT_VA(this, sc);

  if (sc.IsValid() && m_opaque_up.get())
    m_opaque_up->Append(*sc);
}

void SBSymbolContextList::Append(SBSymbolContextList &sc_list) {
  LLDB_INSTRUMENT_VA(this, sc_list);

  if (sc_list.IsValid() && m_opaque_up.get())
    m_opaque_up->Append(*sc_list);
}

bool SBSymbolContextList::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBSymbolContextList::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up != nullptr;
}

lldb_private::SymbolContextList *SBSymbolContextList::operator->() const {
  return m_opaque_up.get();
}

lldb_private::SymbolContextList &SBSymbolContextList::operator*() const {
  assert(m_opaque_up.get());
  return *m_opaque_up;
}

bool SBSymbolContextList::GetDescription(lldb::SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();
  if (m_opaque_up)
    m_opaque_up->GetDescription(&strm, lldb::eDescriptionLevelFull, nullptr);
  return true;
}
