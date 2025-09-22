//===-- DWARFDebugInfo.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolFileDWARF.h"

#include <algorithm>
#include <set>

#include "lldb/Host/PosixApi.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Stream.h"
#include "llvm/Support/Casting.h"

#include "DWARFCompileUnit.h"
#include "DWARFContext.h"
#include "DWARFDebugAranges.h"
#include "DWARFDebugInfo.h"
#include "DWARFDebugInfoEntry.h"
#include "DWARFFormValue.h"
#include "DWARFTypeUnit.h"
#include "LogChannelDWARF.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

// Constructor
DWARFDebugInfo::DWARFDebugInfo(SymbolFileDWARF &dwarf, DWARFContext &context)
    : m_dwarf(dwarf), m_context(context), m_units(), m_cu_aranges_up() {}

const DWARFDebugAranges &DWARFDebugInfo::GetCompileUnitAranges() {
  if (m_cu_aranges_up)
    return *m_cu_aranges_up;

  m_cu_aranges_up = std::make_unique<DWARFDebugAranges>();
  const DWARFDataExtractor &debug_aranges_data =
      m_context.getOrLoadArangesData();

  // Extract what we can from the .debug_aranges first.
  m_cu_aranges_up->extract(debug_aranges_data);

  // Make a list of all CUs represented by the .debug_aranges data.
  std::set<dw_offset_t> cus_with_data;
  for (size_t n = 0; n < m_cu_aranges_up->GetNumRanges(); n++) {
    dw_offset_t offset = m_cu_aranges_up->OffsetAtIndex(n);
    if (offset != DW_INVALID_OFFSET)
      cus_with_data.insert(offset);
  }

  // Manually build arange data for everything that wasn't in .debug_aranges.
  // The .debug_aranges accelerator is not guaranteed to be complete.
  // Tools such as dsymutil can provide stronger guarantees than required by the
  // standard. Without that guarantee, we have to iterate over every CU in the
  // .debug_info and make sure there's a corresponding entry in the table and if
  // not, add one for every subprogram.
  ObjectFile *OF = m_dwarf.GetObjectFile();
  if (!OF || !OF->CanTrustAddressRanges()) {
    const size_t num_units = GetNumUnits();
    for (size_t idx = 0; idx < num_units; ++idx) {
      DWARFUnit *cu = GetUnitAtIndex(idx);

      dw_offset_t offset = cu->GetOffset();
      if (cus_with_data.find(offset) == cus_with_data.end())
        cu->BuildAddressRangeTable(m_cu_aranges_up.get());
    }
  }

  const bool minimize = true;
  m_cu_aranges_up->Sort(minimize);
  return *m_cu_aranges_up;
}

void DWARFDebugInfo::ParseUnitsFor(DIERef::Section section) {
  DWARFDataExtractor data = section == DIERef::Section::DebugTypes
                                ? m_context.getOrLoadDebugTypesData()
                                : m_context.getOrLoadDebugInfoData();
  lldb::offset_t offset = 0;
  while (data.ValidOffset(offset)) {
    const lldb::offset_t unit_header_offset = offset;
    llvm::Expected<DWARFUnitSP> expected_unit_sp =
        DWARFUnit::extract(m_dwarf, m_units.size(), data, section, &offset);

    if (!expected_unit_sp) {
      Log *log = GetLog(DWARFLog::DebugInfo);
      if (log)
        LLDB_LOG(log, "Unable to extract DWARFUnitHeader at {0:x}: {1}",
                 unit_header_offset,
                 llvm::toString(expected_unit_sp.takeError()));
      else
        llvm::consumeError(expected_unit_sp.takeError());
      return;
    }

    DWARFUnitSP unit_sp = *expected_unit_sp;

    // If it didn't return an error, then it should be returning a valid Unit.
    assert((bool)unit_sp);

    // Keep a map of DWO ID back to the skeleton units. Sometimes accelerator
    // table lookups can cause the DWO files to be accessed before the skeleton
    // compile unit is parsed, so we keep a map to allow us to match up the DWO
    // file to the back to the skeleton compile units.
    if (unit_sp->GetUnitType() == lldb_private::dwarf::DW_UT_skeleton) {
      if (std::optional<uint64_t> unit_dwo_id = unit_sp->GetHeaderDWOId())
        m_dwarf5_dwo_id_to_skeleton_unit[*unit_dwo_id] = unit_sp.get();
    }

    m_units.push_back(unit_sp);
    offset = unit_sp->GetNextUnitOffset();

    if (auto *type_unit = llvm::dyn_cast<DWARFTypeUnit>(unit_sp.get())) {
      m_type_hash_to_unit_index.emplace_back(type_unit->GetTypeHash(),
                                             unit_sp->GetID());
    }
  }
}

DWARFUnit *DWARFDebugInfo::GetSkeletonUnit(DWARFUnit *dwo_unit) {
  // If this isn't a DWO unit, don't try and find the skeleton unit.
  if (!dwo_unit->IsDWOUnit())
    return nullptr;

  auto dwo_id = dwo_unit->GetDWOId();
  if (!dwo_id.has_value())
    return nullptr;

  // Parse the unit headers so that m_dwarf5_dwo_id_to_skeleton_unit is filled
  // in with all of the DWARF5 skeleton compile units DWO IDs since it is easy
  // to access the DWO IDs in the DWARFUnitHeader for each DWARFUnit.
  ParseUnitHeadersIfNeeded();

  // Find the value in our cache and return it we we find it. This cache may
  // only contain DWARF5 units.
  auto iter = m_dwarf5_dwo_id_to_skeleton_unit.find(*dwo_id);
  if (iter != m_dwarf5_dwo_id_to_skeleton_unit.end())
    return iter->second;

  // DWARF5 unit headers have the DWO ID and should have already been in the map
  // so if it wasn't found in the above find() call, then we didn't find it and
  // don't need to do the more expensive DWARF4 search.
  if (dwo_unit->GetVersion() >= 5)
    return nullptr;

  // Parse all DWO IDs from all DWARF4 and earlier compile units that have DWO
  // IDs. It is more expensive to get the DWO IDs from DWARF4 compile units as
  // we need to parse the unit DIE and extract the DW_AT_dwo_id or
  // DW_AT_GNU_dwo_id attribute values, so do this only if we didn't find our
  // match above search and only for DWARF4 and earlier compile units.
  llvm::call_once(m_dwarf4_dwo_id_to_skeleton_unit_once_flag, [this]() {
    for (uint32_t i = 0, num = GetNumUnits(); i < num; ++i) {
      if (DWARFUnit *unit = GetUnitAtIndex(i)) {
        if (unit->GetVersion() < 5) {
          if (std::optional<uint64_t> unit_dwo_id = unit->GetDWOId())
            m_dwarf4_dwo_id_to_skeleton_unit[*unit_dwo_id] = unit;
        }
      }
    }
  });

  // Search the DWARF4 DWO results that we parsed lazily.
  iter = m_dwarf4_dwo_id_to_skeleton_unit.find(*dwo_id);
  if (iter != m_dwarf4_dwo_id_to_skeleton_unit.end())
    return iter->second;
  return nullptr;
}

void DWARFDebugInfo::ParseUnitHeadersIfNeeded() {
  llvm::call_once(m_units_once_flag, [&] {
    ParseUnitsFor(DIERef::Section::DebugInfo);
    ParseUnitsFor(DIERef::Section::DebugTypes);
    llvm::sort(m_type_hash_to_unit_index, llvm::less_first());
  });
}

size_t DWARFDebugInfo::GetNumUnits() {
  ParseUnitHeadersIfNeeded();
  return m_units.size();
}

DWARFUnit *DWARFDebugInfo::GetUnitAtIndex(size_t idx) {
  DWARFUnit *cu = nullptr;
  if (idx < GetNumUnits())
    cu = m_units[idx].get();
  return cu;
}

uint32_t DWARFDebugInfo::FindUnitIndex(DIERef::Section section,
                                       dw_offset_t offset) {
  ParseUnitHeadersIfNeeded();

  // llvm::lower_bound is not used as for DIE offsets it would still return
  // index +1 and GetOffset() returning index itself would be a special case.
  auto pos = llvm::upper_bound(
      m_units, std::make_pair(section, offset),
      [](const std::pair<DIERef::Section, dw_offset_t> &lhs,
         const DWARFUnitSP &rhs) {
        return lhs < std::make_pair(rhs->GetDebugSection(), rhs->GetOffset());
      });
  uint32_t idx = std::distance(m_units.begin(), pos);
  if (idx == 0)
    return DW_INVALID_INDEX;
  return idx - 1;
}

DWARFUnit *DWARFDebugInfo::GetUnitAtOffset(DIERef::Section section,
                                           dw_offset_t cu_offset,
                                           uint32_t *idx_ptr) {
  uint32_t idx = FindUnitIndex(section, cu_offset);
  DWARFUnit *result = GetUnitAtIndex(idx);
  if (result && result->GetOffset() != cu_offset) {
    result = nullptr;
    idx = DW_INVALID_INDEX;
  }
  if (idx_ptr)
    *idx_ptr = idx;
  return result;
}

DWARFUnit *
DWARFDebugInfo::GetUnitContainingDIEOffset(DIERef::Section section,
                                           dw_offset_t die_offset) {
  uint32_t idx = FindUnitIndex(section, die_offset);
  DWARFUnit *result = GetUnitAtIndex(idx);
  if (result && !result->ContainsDIEOffset(die_offset))
    return nullptr;
  return result;
}

const std::shared_ptr<SymbolFileDWARFDwo> &DWARFDebugInfo::GetDwpSymbolFile() {
  return m_dwarf.GetDwpSymbolFile();
}

DWARFTypeUnit *DWARFDebugInfo::GetTypeUnitForHash(uint64_t hash) {
  auto pos = llvm::lower_bound(m_type_hash_to_unit_index,
                               std::make_pair(hash, 0u), llvm::less_first());
  if (pos == m_type_hash_to_unit_index.end() || pos->first != hash)
    return nullptr;
  return llvm::cast<DWARFTypeUnit>(GetUnitAtIndex(pos->second));
}

bool DWARFDebugInfo::ContainsTypeUnits() {
  ParseUnitHeadersIfNeeded();
  return !m_type_hash_to_unit_index.empty();
}

// GetDIE()
//
// Get the DIE (Debug Information Entry) with the specified offset.
DWARFDIE
DWARFDebugInfo::GetDIE(DIERef::Section section, dw_offset_t die_offset) {
  if (DWARFUnit *cu = GetUnitContainingDIEOffset(section, die_offset))
    return cu->GetNonSkeletonUnit().GetDIE(die_offset);
  return DWARFDIE(); // Not found
}
