//===-- SBStringList.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBStringList.h"
#include "Utils.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/StringList.h"

using namespace lldb;
using namespace lldb_private;

SBStringList::SBStringList() { LLDB_INSTRUMENT_VA(this); }

SBStringList::SBStringList(const lldb_private::StringList *lldb_strings_ptr) {
  if (lldb_strings_ptr)
    m_opaque_up = std::make_unique<StringList>(*lldb_strings_ptr);
}

SBStringList::SBStringList(const SBStringList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

const SBStringList &SBStringList::operator=(const SBStringList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

SBStringList::~SBStringList() = default;

lldb_private::StringList *SBStringList::operator->() {
  if (!IsValid())
    m_opaque_up = std::make_unique<lldb_private::StringList>();

  return m_opaque_up.get();
}

const lldb_private::StringList *SBStringList::operator->() const {
  return m_opaque_up.get();
}

const lldb_private::StringList &SBStringList::operator*() const {
  return *m_opaque_up;
}

bool SBStringList::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBStringList::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_up != nullptr);
}

void SBStringList::AppendString(const char *str) {
  LLDB_INSTRUMENT_VA(this, str);

  if (str != nullptr) {
    if (IsValid())
      m_opaque_up->AppendString(str);
    else
      m_opaque_up = std::make_unique<lldb_private::StringList>(str);
  }
}

void SBStringList::AppendList(const char **strv, int strc) {
  LLDB_INSTRUMENT_VA(this, strv, strc);

  if ((strv != nullptr) && (strc > 0)) {
    if (IsValid())
      m_opaque_up->AppendList(strv, strc);
    else
      m_opaque_up = std::make_unique<lldb_private::StringList>(strv, strc);
  }
}

void SBStringList::AppendList(const SBStringList &strings) {
  LLDB_INSTRUMENT_VA(this, strings);

  if (strings.IsValid()) {
    if (!IsValid())
      m_opaque_up = std::make_unique<lldb_private::StringList>();
    m_opaque_up->AppendList(*(strings.m_opaque_up));
  }
}

void SBStringList::AppendList(const StringList &strings) {
  if (!IsValid())
    m_opaque_up = std::make_unique<lldb_private::StringList>();
  m_opaque_up->AppendList(strings);
}

uint32_t SBStringList::GetSize() const {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid()) {
    return m_opaque_up->GetSize();
  }
  return 0;
}

const char *SBStringList::GetStringAtIndex(size_t idx) {
  LLDB_INSTRUMENT_VA(this, idx);

  if (IsValid()) {
    return ConstString(m_opaque_up->GetStringAtIndex(idx)).GetCString();
  }
  return nullptr;
}

const char *SBStringList::GetStringAtIndex(size_t idx) const {
  LLDB_INSTRUMENT_VA(this, idx);

  if (IsValid()) {
    return ConstString(m_opaque_up->GetStringAtIndex(idx)).GetCString();
  }
  return nullptr;
}

void SBStringList::Clear() {
  LLDB_INSTRUMENT_VA(this);

  if (IsValid()) {
    m_opaque_up->Clear();
  }
}
