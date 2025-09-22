//===-- UniqueDWARFASTType.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_UNIQUEDWARFASTTYPE_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_UNIQUEDWARFASTTYPE_H

#include <vector>

#include "llvm/ADT/DenseMap.h"

#include "DWARFDIE.h"
#include "lldb/Core/Declaration.h"
#include "lldb/Symbol/Type.h"

namespace lldb_private::plugin {
namespace dwarf {
class UniqueDWARFASTType {
public:
  // Constructors and Destructors
  UniqueDWARFASTType() : m_type_sp(), m_die(), m_declaration() {}

  UniqueDWARFASTType(const UniqueDWARFASTType &rhs)
      : m_type_sp(rhs.m_type_sp), m_die(rhs.m_die),
        m_declaration(rhs.m_declaration), m_byte_size(rhs.m_byte_size),
        m_is_forward_declaration(rhs.m_is_forward_declaration) {}

  ~UniqueDWARFASTType() = default;

  // This UniqueDWARFASTType might be created from declaration, update its info
  // to definition DIE.
  void UpdateToDefDIE(const DWARFDIE &def_die, Declaration &declaration,
                      int32_t byte_size) {
    // Need to update Type ID to refer to the definition DIE, because
    // it's used in DWARFASTParserClang::ParseCXXMethod to determine if we need
    // to copy cxx method types from a declaration DIE to this definition DIE.
    m_type_sp->SetID(def_die.GetID());
    if (declaration.IsValid())
      m_declaration = declaration;
    if (byte_size)
      m_byte_size = byte_size;
    m_is_forward_declaration = false;
  }

  lldb::TypeSP m_type_sp;
  DWARFDIE m_die;
  Declaration m_declaration;
  int32_t m_byte_size = -1;
  // True if the m_die is a forward declaration DIE.
  bool m_is_forward_declaration = true;
};

class UniqueDWARFASTTypeList {
public:
  UniqueDWARFASTTypeList() : m_collection() {}

  ~UniqueDWARFASTTypeList() = default;

  uint32_t GetSize() { return (uint32_t)m_collection.size(); }

  void Append(const UniqueDWARFASTType &entry) {
    m_collection.push_back(entry);
  }

  UniqueDWARFASTType *Find(const DWARFDIE &die, const Declaration &decl,
                           const int32_t byte_size,
                           bool is_forward_declaration);

protected:
  typedef std::vector<UniqueDWARFASTType> collection;
  collection m_collection;
};

class UniqueDWARFASTTypeMap {
public:
  UniqueDWARFASTTypeMap() : m_collection() {}

  ~UniqueDWARFASTTypeMap() = default;

  void Insert(ConstString name, const UniqueDWARFASTType &entry) {
    m_collection[name.GetCString()].Append(entry);
  }

  UniqueDWARFASTType *Find(ConstString name, const DWARFDIE &die,
                           const Declaration &decl, const int32_t byte_size,
                           bool is_forward_declaration) {
    const char *unique_name_cstr = name.GetCString();
    collection::iterator pos = m_collection.find(unique_name_cstr);
    if (pos != m_collection.end()) {
      return pos->second.Find(die, decl, byte_size, is_forward_declaration);
    }
    return nullptr;
  }

protected:
  // A unique name string should be used
  typedef llvm::DenseMap<const char *, UniqueDWARFASTTypeList> collection;
  collection m_collection;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_UNIQUEDWARFASTTYPE_H
