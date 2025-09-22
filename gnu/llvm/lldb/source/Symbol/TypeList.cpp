//===-- TypeList.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <vector>

#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"

#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeList.h"

using namespace lldb;
using namespace lldb_private;

TypeList::TypeList() : m_types() {}

// Destructor
TypeList::~TypeList() = default;

void TypeList::Insert(const TypeSP &type_sp) {
  // Just push each type on the back for now. We will worry about uniquing
  // later
  if (type_sp)
    m_types.push_back(type_sp);
}

// Find a base type by its unique ID.
// TypeSP
// TypeList::FindType(lldb::user_id_t uid)
//{
//    iterator pos = m_types.find(uid);
//    if (pos != m_types.end())
//        return pos->second;
//    return TypeSP();
//}

// Find a type by name.
// TypeList
// TypeList::FindTypes (ConstString name)
//{
//    // Do we ever need to make a lookup by name map? Here we are doing
//    // a linear search which isn't going to be fast.
//    TypeList types(m_ast.getTargetInfo()->getTriple().getTriple().c_str());
//    iterator pos, end;
//    for (pos = m_types.begin(), end = m_types.end(); pos != end; ++pos)
//        if (pos->second->GetName() == name)
//            types.Insert (pos->second);
//    return types;
//}

void TypeList::Clear() { m_types.clear(); }

uint32_t TypeList::GetSize() const { return m_types.size(); }

// GetTypeAtIndex isn't used a lot for large type lists, currently only for
// type lists that are returned for "image dump -t TYPENAME" commands and other
// simple symbol queries that grab the first result...

TypeSP TypeList::GetTypeAtIndex(uint32_t idx) {
  iterator pos, end;
  uint32_t i = idx;
  assert(i < GetSize() && "Accessing past the end of a TypeList");
  for (pos = m_types.begin(), end = m_types.end(); pos != end; ++pos) {
    if (i == 0)
      return *pos;
    --i;
  }
  return TypeSP();
}

void TypeList::ForEach(
    std::function<bool(const lldb::TypeSP &type_sp)> const &callback) const {
  for (auto pos = m_types.begin(), end = m_types.end(); pos != end; ++pos) {
    if (!callback(*pos))
      break;
  }
}

void TypeList::ForEach(
    std::function<bool(lldb::TypeSP &type_sp)> const &callback) {
  for (auto pos = m_types.begin(), end = m_types.end(); pos != end; ++pos) {
    if (!callback(*pos))
      break;
  }
}

void TypeList::Dump(Stream *s, bool show_context) {
  for (iterator pos = m_types.begin(), end = m_types.end(); pos != end; ++pos)
    if (Type *t = pos->get())
      t->Dump(s, show_context);
}
