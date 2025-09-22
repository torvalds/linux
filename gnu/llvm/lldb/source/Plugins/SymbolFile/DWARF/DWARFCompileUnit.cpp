//===-- DWARFCompileUnit.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFCompileUnit.h"
#include "DWARFDebugAranges.h"
#include "SymbolFileDWARFDebugMap.h"

#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

void DWARFCompileUnit::Dump(Stream *s) const {
  s->Format(

      "{0:x16}: Compile Unit: length = {1:x8}, version = {2:x}, "
      "abbr_offset = {3:x8}, addr_size = {4:x2} (next CU at "
      "[{5:x16}])\n",
      GetOffset(), GetLength(), GetVersion(), (uint32_t)GetAbbrevOffset(),
      GetAddressByteSize(), GetNextUnitOffset());
}

void DWARFCompileUnit::BuildAddressRangeTable(
    DWARFDebugAranges *debug_aranges) {
  // This function is usually called if there in no .debug_aranges section in
  // order to produce a compile unit level set of address ranges that is
  // accurate.

  size_t num_debug_aranges = debug_aranges->GetNumRanges();

  // First get the compile unit DIE only and check contains ranges information.
  const DWARFDebugInfoEntry *die = GetUnitDIEPtrOnly();

  const dw_offset_t cu_offset = GetOffset();
  if (die) {
    DWARFRangeList ranges =
        die->GetAttributeAddressRanges(this, /*check_hi_lo_pc=*/true);
    for (const DWARFRangeList::Entry &range : ranges)
      debug_aranges->AppendRange(cu_offset, range.GetRangeBase(),
                                 range.GetRangeEnd());

    if (!ranges.IsEmpty())
      return;
  }

  if (debug_aranges->GetNumRanges() == num_debug_aranges) {
    // We got nothing from the debug info, try to build the arange table from
    // the debug map OSO aranges.
    SymbolContext sc;
    sc.comp_unit = m_dwarf.GetCompUnitForDWARFCompUnit(*this);
    if (sc.comp_unit) {
      SymbolFileDWARFDebugMap *debug_map_sym_file =
          m_dwarf.GetDebugMapSymfile();
      if (debug_map_sym_file) {
        auto *cu_info =
            debug_map_sym_file->GetCompileUnitInfo(&GetSymbolFileDWARF());
        // If there are extra compile units the OSO entries aren't a reliable
        // source of information.
        if (cu_info->compile_units_sps.empty())
          debug_map_sym_file->AddOSOARanges(&m_dwarf, debug_aranges);
      }
    }
  }

  if (debug_aranges->GetNumRanges() == num_debug_aranges) {
    // We got nothing from the functions, maybe we have a line tables only
    // situation. Check the line tables and build the arange table from this.
    SymbolContext sc;
    sc.comp_unit = m_dwarf.GetCompUnitForDWARFCompUnit(*this);
    if (sc.comp_unit) {
      if (LineTable *line_table = sc.comp_unit->GetLineTable()) {
        LineTable::FileAddressRanges file_ranges;
        const bool append = true;
        const size_t num_ranges =
            line_table->GetContiguousFileAddressRanges(file_ranges, append);
        for (uint32_t idx = 0; idx < num_ranges; ++idx) {
          const LineTable::FileAddressRanges::Entry &range =
              file_ranges.GetEntryRef(idx);
          debug_aranges->AppendRange(GetOffset(), range.GetRangeBase(),
                                     range.GetRangeEnd());
        }
      }
    }
  }
}

DWARFCompileUnit &DWARFCompileUnit::GetNonSkeletonUnit() {
  return llvm::cast<DWARFCompileUnit>(DWARFUnit::GetNonSkeletonUnit());
}

DWARFDIE DWARFCompileUnit::LookupAddress(const dw_addr_t address) {
  if (DIE()) {
    const DWARFDebugAranges &func_aranges = GetFunctionAranges();

    // Re-check the aranges auto pointer contents in case it was created above
    if (!func_aranges.IsEmpty())
      return GetDIE(func_aranges.FindAddress(address));
  }
  return DWARFDIE();
}
