//===-- ValueObjectList.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ValueObjectList.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Utility/ConstString.h"

#include <utility>

using namespace lldb;
using namespace lldb_private;

const ValueObjectList &ValueObjectList::operator=(const ValueObjectList &rhs) {
  if (this != &rhs)
    m_value_objects = rhs.m_value_objects;
  return *this;
}

void ValueObjectList::Append(const ValueObjectSP &val_obj_sp) {
  m_value_objects.push_back(val_obj_sp);
}

void ValueObjectList::Append(const ValueObjectList &valobj_list) {
  std::copy(valobj_list.m_value_objects.begin(), // source begin
            valobj_list.m_value_objects.end(),   // source end
            back_inserter(m_value_objects));     // destination
}

size_t ValueObjectList::GetSize() const { return m_value_objects.size(); }

void ValueObjectList::Resize(size_t size) { m_value_objects.resize(size); }

lldb::ValueObjectSP ValueObjectList::GetValueObjectAtIndex(size_t idx) {
  lldb::ValueObjectSP valobj_sp;
  if (idx < m_value_objects.size())
    valobj_sp = m_value_objects[idx];
  return valobj_sp;
}

lldb::ValueObjectSP ValueObjectList::RemoveValueObjectAtIndex(size_t idx) {
  lldb::ValueObjectSP valobj_sp;
  if (idx < m_value_objects.size()) {
    valobj_sp = m_value_objects[idx];
    m_value_objects.erase(m_value_objects.begin() + idx);
  }
  return valobj_sp;
}

void ValueObjectList::SetValueObjectAtIndex(size_t idx,
                                            const ValueObjectSP &valobj_sp) {
  if (idx >= m_value_objects.size())
    m_value_objects.resize(idx + 1);
  m_value_objects[idx] = valobj_sp;
}

ValueObjectSP ValueObjectList::FindValueObjectByValueName(const char *name) {
  ConstString name_const_str(name);
  ValueObjectSP val_obj_sp;
  collection::iterator pos, end = m_value_objects.end();
  for (pos = m_value_objects.begin(); pos != end; ++pos) {
    ValueObject *valobj = (*pos).get();
    if (valobj && valobj->GetName() == name_const_str) {
      val_obj_sp = *pos;
      break;
    }
  }
  return val_obj_sp;
}

ValueObjectSP ValueObjectList::FindValueObjectByUID(lldb::user_id_t uid) {
  ValueObjectSP valobj_sp;
  collection::iterator pos, end = m_value_objects.end();

  for (pos = m_value_objects.begin(); pos != end; ++pos) {
    // Watch out for NULL objects in our list as the list might get resized to
    // a specific size and lazily filled in
    ValueObject *valobj = (*pos).get();
    if (valobj && valobj->GetID() == uid) {
      valobj_sp = *pos;
      break;
    }
  }
  return valobj_sp;
}

ValueObjectSP
ValueObjectList::FindValueObjectByPointer(ValueObject *find_valobj) {
  ValueObjectSP valobj_sp;
  collection::iterator pos, end = m_value_objects.end();

  for (pos = m_value_objects.begin(); pos != end; ++pos) {
    ValueObject *valobj = (*pos).get();
    if (valobj && valobj == find_valobj) {
      valobj_sp = *pos;
      break;
    }
  }
  return valobj_sp;
}

void ValueObjectList::Swap(ValueObjectList &value_object_list) {
  m_value_objects.swap(value_object_list.m_value_objects);
}
