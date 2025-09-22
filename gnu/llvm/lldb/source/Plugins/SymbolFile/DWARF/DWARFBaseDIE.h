//===-- DWARFBaseDIE.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFBASEDIE_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFBASEDIE_H

#include "lldb/Core/dwarf.h"
#include "lldb/lldb-types.h"

#include "llvm/Support/Error.h"
#include <optional>

namespace lldb_private::plugin {
namespace dwarf {
class DIERef;
class DWARFASTParser;
class DWARFAttributes;
class DWARFUnit;
class DWARFDebugInfoEntry;
class DWARFDeclContext;
class SymbolFileDWARF;

class DWARFBaseDIE {
public:
  DWARFBaseDIE() = default;

  DWARFBaseDIE(DWARFUnit *cu, DWARFDebugInfoEntry *die)
      : m_cu(cu), m_die(die) {}

  DWARFBaseDIE(const DWARFUnit *cu, DWARFDebugInfoEntry *die)
      : m_cu(const_cast<DWARFUnit *>(cu)), m_die(die) {}

  DWARFBaseDIE(DWARFUnit *cu, const DWARFDebugInfoEntry *die)
      : m_cu(cu), m_die(const_cast<DWARFDebugInfoEntry *>(die)) {}

  DWARFBaseDIE(const DWARFUnit *cu, const DWARFDebugInfoEntry *die)
      : m_cu(const_cast<DWARFUnit *>(cu)),
        m_die(const_cast<DWARFDebugInfoEntry *>(die)) {}

  // Tests
  explicit operator bool() const { return IsValid(); }

  bool IsValid() const { return m_cu && m_die; }

  bool HasChildren() const;

  bool Supports_DW_AT_APPLE_objc_complete_type() const;

  // Accessors
  SymbolFileDWARF *GetDWARF() const;

  DWARFUnit *GetCU() const { return m_cu; }

  DWARFDebugInfoEntry *GetDIE() const { return m_die; }

  std::optional<DIERef> GetDIERef() const;

  void Set(DWARFUnit *cu, DWARFDebugInfoEntry *die) {
    if (cu && die) {
      m_cu = cu;
      m_die = die;
    } else {
      Clear();
    }
  }

  void Clear() {
    m_cu = nullptr;
    m_die = nullptr;
  }

  // Get the data that contains the attribute values for this DIE. Support
  // for .debug_types means that any DIE can have its data either in the
  // .debug_info or the .debug_types section; this method will return the
  // correct section data.
  //
  // Clients must validate that this object is valid before calling this.
  const DWARFDataExtractor &GetData() const;

  // Accessing information about a DIE
  dw_tag_t Tag() const;

  dw_offset_t GetOffset() const;

  // Get the LLDB user ID for this DIE. This is often just the DIE offset,
  // but it might have a SymbolFileDWARF::GetID() in the high 32 bits if
  // we are doing Darwin DWARF in .o file, or DWARF stand alone debug
  // info.
  lldb::user_id_t GetID() const;

  const char *GetName() const;

  lldb::ModuleSP GetModule() const;

  // Getting attribute values from the DIE.
  //
  // GetAttributeValueAsXXX() functions should only be used if you are
  // looking for one or two attributes on a DIE. If you are trying to
  // parse all attributes, use GetAttributes (...) instead
  const char *GetAttributeValueAsString(const dw_attr_t attr,
                                        const char *fail_value) const;

  uint64_t GetAttributeValueAsUnsigned(const dw_attr_t attr,
                                       uint64_t fail_value) const;

  std::optional<uint64_t>
  GetAttributeValueAsOptionalUnsigned(const dw_attr_t attr) const;

  uint64_t GetAttributeValueAsAddress(const dw_attr_t attr,
                                      uint64_t fail_value) const;

  enum class Recurse : bool { no, yes };
  DWARFAttributes GetAttributes(Recurse recurse = Recurse::yes) const;

protected:
  DWARFUnit *m_cu = nullptr;
  DWARFDebugInfoEntry *m_die = nullptr;
};

bool operator==(const DWARFBaseDIE &lhs, const DWARFBaseDIE &rhs);
bool operator!=(const DWARFBaseDIE &lhs, const DWARFBaseDIE &rhs);
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFBASEDIE_H
