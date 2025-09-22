//===-- SBThreadCollection.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBThreadCollection.h"
#include "lldb/API/SBThread.h"
#include "lldb/Target/ThreadList.h"
#include "lldb/Utility/Instrumentation.h"

using namespace lldb;
using namespace lldb_private;

SBThreadCollection::SBThreadCollection() { LLDB_INSTRUMENT_VA(this); }

SBThreadCollection::SBThreadCollection(const SBThreadCollection &rhs)
    : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const SBThreadCollection &SBThreadCollection::
operator=(const SBThreadCollection &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

SBThreadCollection::SBThreadCollection(const ThreadCollectionSP &threads)
    : m_opaque_sp(threads) {}

SBThreadCollection::~SBThreadCollection() = default;

void SBThreadCollection::SetOpaque(const lldb::ThreadCollectionSP &threads) {
  m_opaque_sp = threads;
}

lldb_private::ThreadCollection *SBThreadCollection::get() const {
  return m_opaque_sp.get();
}

lldb_private::ThreadCollection *SBThreadCollection::operator->() const {
  return m_opaque_sp.operator->();
}

lldb::ThreadCollectionSP &SBThreadCollection::operator*() {
  return m_opaque_sp;
}

const lldb::ThreadCollectionSP &SBThreadCollection::operator*() const {
  return m_opaque_sp;
}

bool SBThreadCollection::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBThreadCollection::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_sp.get() != nullptr;
}

size_t SBThreadCollection::GetSize() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_sp)
    return m_opaque_sp->GetSize();
  return 0;
}

SBThread SBThreadCollection::GetThreadAtIndex(size_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  SBThread thread;
  if (m_opaque_sp && idx < m_opaque_sp->GetSize())
    thread = m_opaque_sp->GetThreadAtIndex(idx);
  return thread;
}
