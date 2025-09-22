//===-- SBValueList.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBValueList.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBValue.h"
#include "lldb/Core/ValueObjectList.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Status.h"
#include <vector>

using namespace lldb;
using namespace lldb_private;

class ValueListImpl {
public:
  ValueListImpl() = default;

  ValueListImpl(const ValueListImpl &rhs) = default;

  ValueListImpl &operator=(const ValueListImpl &rhs) {
    if (this == &rhs)
      return *this;
    m_values = rhs.m_values;
    m_error = rhs.m_error;
    return *this;
  }

  uint32_t GetSize() { return m_values.size(); }

  void Append(const lldb::SBValue &sb_value) { m_values.push_back(sb_value); }

  void Append(const ValueListImpl &list) {
    for (auto val : list.m_values)
      Append(val);
  }

  lldb::SBValue GetValueAtIndex(uint32_t index) {
    if (index >= GetSize())
      return lldb::SBValue();
    return m_values[index];
  }

  lldb::SBValue FindValueByUID(lldb::user_id_t uid) {
    for (auto val : m_values) {
      if (val.IsValid() && val.GetID() == uid)
        return val;
    }
    return lldb::SBValue();
  }

  lldb::SBValue GetFirstValueByName(const char *name) const {
    if (name) {
      for (auto val : m_values) {
        if (val.IsValid() && val.GetName() && strcmp(name, val.GetName()) == 0)
          return val;
      }
    }
    return lldb::SBValue();
  }

  const Status &GetError() const { return m_error; }

  void SetError(const Status &error) { m_error = error; }

private:
  std::vector<lldb::SBValue> m_values;
  Status m_error;
};

SBValueList::SBValueList() { LLDB_INSTRUMENT_VA(this); }

SBValueList::SBValueList(const SBValueList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (rhs.IsValid())
    m_opaque_up = std::make_unique<ValueListImpl>(*rhs);
}

SBValueList::SBValueList(const ValueListImpl *lldb_object_ptr) {
  if (lldb_object_ptr)
    m_opaque_up = std::make_unique<ValueListImpl>(*lldb_object_ptr);
}

SBValueList::~SBValueList() = default;

bool SBValueList::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBValueList::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_up != nullptr);
}

void SBValueList::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_up.reset();
}

const SBValueList &SBValueList::operator=(const SBValueList &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs) {
    if (rhs.IsValid())
      m_opaque_up = std::make_unique<ValueListImpl>(*rhs);
    else
      m_opaque_up.reset();
  }
  return *this;
}

ValueListImpl *SBValueList::operator->() { return m_opaque_up.get(); }

ValueListImpl &SBValueList::operator*() { return *m_opaque_up; }

const ValueListImpl *SBValueList::operator->() const {
  return m_opaque_up.get();
}

const ValueListImpl &SBValueList::operator*() const { return *m_opaque_up; }

void SBValueList::Append(const SBValue &val_obj) {
  LLDB_INSTRUMENT_VA(this, val_obj);

  CreateIfNeeded();
  m_opaque_up->Append(val_obj);
}

void SBValueList::Append(lldb::ValueObjectSP &val_obj_sp) {
  if (val_obj_sp) {
    CreateIfNeeded();
    m_opaque_up->Append(SBValue(val_obj_sp));
  }
}

void SBValueList::Append(const lldb::SBValueList &value_list) {
  LLDB_INSTRUMENT_VA(this, value_list);

  if (value_list.IsValid()) {
    CreateIfNeeded();
    m_opaque_up->Append(*value_list);
  }
}

SBValue SBValueList::GetValueAtIndex(uint32_t idx) const {
  LLDB_INSTRUMENT_VA(this, idx);

  SBValue sb_value;
  if (m_opaque_up)
    sb_value = m_opaque_up->GetValueAtIndex(idx);

  return sb_value;
}

uint32_t SBValueList::GetSize() const {
  LLDB_INSTRUMENT_VA(this);

  uint32_t size = 0;
  if (m_opaque_up)
    size = m_opaque_up->GetSize();

  return size;
}

void SBValueList::CreateIfNeeded() {
  if (m_opaque_up == nullptr)
    m_opaque_up = std::make_unique<ValueListImpl>();
}

SBValue SBValueList::FindValueObjectByUID(lldb::user_id_t uid) {
  LLDB_INSTRUMENT_VA(this, uid);

  SBValue sb_value;
  if (m_opaque_up)
    sb_value = m_opaque_up->FindValueByUID(uid);
  return sb_value;
}

SBValue SBValueList::GetFirstValueByName(const char *name) const {
  LLDB_INSTRUMENT_VA(this, name);

  SBValue sb_value;
  if (m_opaque_up)
    sb_value = m_opaque_up->GetFirstValueByName(name);
  return sb_value;
}

void *SBValueList::opaque_ptr() { return m_opaque_up.get(); }

ValueListImpl &SBValueList::ref() {
  CreateIfNeeded();
  return *m_opaque_up;
}

lldb::SBError SBValueList::GetError() {
  LLDB_INSTRUMENT_VA(this);
  SBError sb_error;
  if (m_opaque_up)
    sb_error.SetError(m_opaque_up->GetError());
  return sb_error;
}

void SBValueList::SetError(const lldb_private::Status &status) {
  ref().SetError(status);
}
