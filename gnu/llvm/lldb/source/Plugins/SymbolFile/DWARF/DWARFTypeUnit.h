//===-- DWARFTypeUnit.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFTYPEUNIT_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFTYPEUNIT_H

#include "DWARFUnit.h"
#include "llvm/Support/Error.h"

namespace llvm {
class DWARFAbbreviationDeclarationSet;
} // namespace llvm

namespace lldb_private::plugin {
namespace dwarf {
class DWARFTypeUnit : public DWARFUnit {
public:
  void BuildAddressRangeTable(DWARFDebugAranges *debug_aranges) override {}

  void Dump(Stream *s) const override;

  uint64_t GetTypeHash() { return m_header.getTypeHash(); }

  dw_offset_t GetTypeOffset() { return GetOffset() + m_header.getTypeOffset(); }

  static bool classof(const DWARFUnit *unit) { return unit->IsTypeUnit(); }

private:
  DWARFTypeUnit(SymbolFileDWARF &dwarf, lldb::user_id_t uid,
                const llvm::DWARFUnitHeader &header,
                const llvm::DWARFAbbreviationDeclarationSet &abbrevs,
                DIERef::Section section, bool is_dwo)
      : DWARFUnit(dwarf, uid, header, abbrevs, section, is_dwo) {}

  friend class DWARFUnit;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFTYPEUNIT_H
