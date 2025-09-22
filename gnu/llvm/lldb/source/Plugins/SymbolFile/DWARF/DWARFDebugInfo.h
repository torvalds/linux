//===-- DWARFDebugInfo.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEBUGINFO_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEBUGINFO_H

#include <map>
#include <vector>

#include "DWARFDIE.h"
#include "DWARFTypeUnit.h"
#include "DWARFUnit.h"
#include "SymbolFileDWARF.h"
#include "lldb/lldb-private.h"
#include "llvm/Support/Error.h"

namespace lldb_private::plugin {
namespace dwarf {
class DWARFContext;

class DWARFDebugInfo {
public:
  typedef dw_offset_t (*Callback)(SymbolFileDWARF *dwarf2Data, DWARFUnit *cu,
                                  DWARFDebugInfoEntry *die,
                                  const dw_offset_t next_offset,
                                  const uint32_t depth, void *userData);

  explicit DWARFDebugInfo(SymbolFileDWARF &dwarf, DWARFContext &context);

  size_t GetNumUnits();
  DWARFUnit *GetUnitAtIndex(size_t idx);
  DWARFUnit *GetUnitAtOffset(DIERef::Section section, dw_offset_t cu_offset,
                             uint32_t *idx_ptr = nullptr);
  DWARFUnit *GetUnitContainingDIEOffset(DIERef::Section section,
                                        dw_offset_t die_offset);
  DWARFUnit *GetSkeletonUnit(DWARFUnit *dwo_unit);
  DWARFTypeUnit *GetTypeUnitForHash(uint64_t hash);
  bool ContainsTypeUnits();
  DWARFDIE GetDIE(DIERef::Section section, dw_offset_t die_offset);

  enum {
    eDumpFlag_Verbose = (1 << 0),  // Verbose dumping
    eDumpFlag_ShowForm = (1 << 1), // Show the DW_form type
    eDumpFlag_ShowAncestors =
        (1 << 2) // Show all parent DIEs when dumping single DIEs
  };

  const DWARFDebugAranges &GetCompileUnitAranges();

  const std::shared_ptr<SymbolFileDWARFDwo> &GetDwpSymbolFile();

protected:
  typedef std::vector<DWARFUnitSP> UnitColl;

  SymbolFileDWARF &m_dwarf;
  DWARFContext &m_context;

  llvm::once_flag m_units_once_flag;
  UnitColl m_units;

  std::unique_ptr<DWARFDebugAranges>
      m_cu_aranges_up; // A quick address to compile unit table

  std::vector<std::pair<uint64_t, uint32_t>> m_type_hash_to_unit_index;
  llvm::DenseMap<uint64_t, DWARFUnit *> m_dwarf5_dwo_id_to_skeleton_unit;
  llvm::DenseMap<uint64_t, DWARFUnit *> m_dwarf4_dwo_id_to_skeleton_unit;
  llvm::once_flag m_dwarf4_dwo_id_to_skeleton_unit_once_flag;

private:
  // All parsing needs to be done partially any managed by this class as
  // accessors are called.
  void ParseUnitHeadersIfNeeded();

  void ParseUnitsFor(DIERef::Section section);

  uint32_t FindUnitIndex(DIERef::Section section, dw_offset_t offset);

  DWARFDebugInfo(const DWARFDebugInfo &) = delete;
  const DWARFDebugInfo &operator=(const DWARFDebugInfo &) = delete;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEBUGINFO_H
