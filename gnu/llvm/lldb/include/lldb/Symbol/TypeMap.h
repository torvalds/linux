//===-- TypeMap.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_TYPEMAP_H
#define LLDB_SYMBOL_TYPEMAP_H

#include "lldb/Symbol/Type.h"
#include "lldb/Utility/Iterable.h"
#include "lldb/lldb-private.h"
#include <functional>
#include <map>

namespace lldb_private {

class TypeMap {
public:
  // Constructors and Destructors
  TypeMap();

  virtual ~TypeMap();

  void Clear();

  void Dump(Stream *s, bool show_context,
            lldb::DescriptionLevel level = lldb::eDescriptionLevelFull) const;

  TypeMap FindTypes(ConstString name);

  void Insert(const lldb::TypeSP &type);

  bool Empty() const;

  bool InsertUnique(const lldb::TypeSP &type);

  uint32_t GetSize() const;

  lldb::TypeSP GetTypeAtIndex(uint32_t idx);

  lldb::TypeSP FirstType() const;

  typedef std::multimap<lldb::user_id_t, lldb::TypeSP> collection;
  typedef AdaptedIterable<collection, lldb::TypeSP, map_adapter> TypeIterable;

  TypeIterable Types() const { return TypeIterable(m_types); }

  void ForEach(
      std::function<bool(const lldb::TypeSP &type_sp)> const &callback) const;

  void ForEach(std::function<bool(lldb::TypeSP &type_sp)> const &callback);

  bool Remove(const lldb::TypeSP &type_sp);

private:
  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  collection m_types;

  TypeMap(const TypeMap &) = delete;
  const TypeMap &operator=(const TypeMap &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_TYPEMAP_H
