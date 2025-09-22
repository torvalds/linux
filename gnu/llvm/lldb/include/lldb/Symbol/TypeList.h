//===-- TypeList.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_TYPELIST_H
#define LLDB_SYMBOL_TYPELIST_H

#include "lldb/Symbol/Type.h"
#include "lldb/Utility/Iterable.h"
#include "lldb/lldb-private.h"
#include <functional>
#include <vector>

namespace lldb_private {

class TypeList {
public:
  // Constructors and Destructors
  TypeList();

  virtual ~TypeList();

  void Clear();

  void Dump(Stream *s, bool show_context);

  TypeList FindTypes(ConstString name);

  void Insert(const lldb::TypeSP &type);

  uint32_t GetSize() const;

  bool Empty() const { return !GetSize(); }

  lldb::TypeSP GetTypeAtIndex(uint32_t idx);

  typedef std::vector<lldb::TypeSP> collection;
  typedef AdaptedIterable<collection, lldb::TypeSP, vector_adapter>
      TypeIterable;

  TypeIterable Types() { return TypeIterable(m_types); }

  void ForEach(
      std::function<bool(const lldb::TypeSP &type_sp)> const &callback) const;

  void ForEach(std::function<bool(lldb::TypeSP &type_sp)> const &callback);

private:
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  collection m_types;

  TypeList(const TypeList &) = delete;
  const TypeList &operator=(const TypeList &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_TYPELIST_H
