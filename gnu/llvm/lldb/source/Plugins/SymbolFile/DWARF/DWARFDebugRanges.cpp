//===-- DWARFDebugRanges.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFDebugRanges.h"
#include "DWARFUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRangeList.h"

using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

DWARFDebugRanges::DWARFDebugRanges() : m_range_map() {}

void DWARFDebugRanges::Extract(DWARFContext &context) {
  llvm::DWARFDataExtractor extractor =
      context.getOrLoadRangesData().GetAsLLVMDWARF();
  llvm::DWARFDebugRangeList extracted_list;
  uint64_t current_offset = 0;
  auto extract_next_list = [&] {
    if (auto error = extracted_list.extract(extractor, &current_offset)) {
      consumeError(std::move(error));
      return false;
    }
    return true;
  };

  uint64_t previous_offset = current_offset;
  while (extractor.isValidOffset(current_offset) && extract_next_list()) {
    DWARFRangeList &lldb_range_list = m_range_map[previous_offset];
    lldb_range_list.Reserve(extracted_list.getEntries().size());
    for (auto &range : extracted_list.getEntries())
      lldb_range_list.Append(range.StartAddress,
                             range.EndAddress - range.StartAddress);
    lldb_range_list.Sort();
    previous_offset = current_offset;
  }
}

DWARFRangeList
DWARFDebugRanges::FindRanges(const DWARFUnit *cu,
                             dw_offset_t debug_ranges_offset) const {
  dw_addr_t debug_ranges_address = cu->GetRangesBase() + debug_ranges_offset;
  auto pos = m_range_map.find(debug_ranges_address);
  DWARFRangeList ans =
      pos == m_range_map.end() ? DWARFRangeList() : pos->second;

  // All DW_AT_ranges are relative to the base address of the compile
  // unit. We add the compile unit base address to make sure all the
  // addresses are properly fixed up.
  ans.Slide(cu->GetBaseAddress());
  return ans;
}
