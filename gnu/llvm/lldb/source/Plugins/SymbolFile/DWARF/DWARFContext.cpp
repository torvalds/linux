//===-- DWARFContext.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFContext.h"

#include "lldb/Core/Section.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

static DWARFDataExtractor LoadSection(SectionList *section_list,
                                      SectionType section_type) {
  if (!section_list)
    return DWARFDataExtractor();

  auto section_sp = section_list->FindSectionByType(section_type, true);
  if (!section_sp)
    return DWARFDataExtractor();

  DWARFDataExtractor data;
  section_sp->GetSectionData(data);
  return data;
}

const DWARFDataExtractor &
DWARFContext::LoadOrGetSection(std::optional<SectionType> main_section_type,
                               std::optional<SectionType> dwo_section_type,
                               SectionData &data) {
  llvm::call_once(data.flag, [&] {
    if (dwo_section_type && isDwo())
      data.data = LoadSection(m_dwo_section_list, *dwo_section_type);
    else if (main_section_type)
      data.data = LoadSection(m_main_section_list, *main_section_type);
  });
  return data.data;
}

const DWARFDataExtractor &DWARFContext::getOrLoadCuIndexData() {
  return LoadOrGetSection(std::nullopt, eSectionTypeDWARFDebugCuIndex,
                          m_data_debug_cu_index);
}

const DWARFDataExtractor &DWARFContext::getOrLoadTuIndexData() {
  return LoadOrGetSection(std::nullopt, eSectionTypeDWARFDebugTuIndex,
                          m_data_debug_tu_index);
}

const DWARFDataExtractor &DWARFContext::getOrLoadAbbrevData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugAbbrev,
                          eSectionTypeDWARFDebugAbbrevDwo, m_data_debug_abbrev);
}

const DWARFDataExtractor &DWARFContext::getOrLoadArangesData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugAranges, std::nullopt,
                          m_data_debug_aranges);
}

const DWARFDataExtractor &DWARFContext::getOrLoadAddrData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugAddr, std::nullopt,
                          m_data_debug_addr);
}

const DWARFDataExtractor &DWARFContext::getOrLoadDebugInfoData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugInfo,
                          eSectionTypeDWARFDebugInfoDwo, m_data_debug_info);
}

const DWARFDataExtractor &DWARFContext::getOrLoadLineData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugLine, std::nullopt,
                          m_data_debug_line);
}

const DWARFDataExtractor &DWARFContext::getOrLoadLineStrData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugLineStr, std::nullopt,
                          m_data_debug_line_str);
}

const DWARFDataExtractor &DWARFContext::getOrLoadLocData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugLoc,
                          eSectionTypeDWARFDebugLocDwo, m_data_debug_loc);
}

const DWARFDataExtractor &DWARFContext::getOrLoadLocListsData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugLocLists,
                          eSectionTypeDWARFDebugLocListsDwo,
                          m_data_debug_loclists);
}

const DWARFDataExtractor &DWARFContext::getOrLoadMacroData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugMacro, std::nullopt,
                          m_data_debug_macro);
}

const DWARFDataExtractor &DWARFContext::getOrLoadRangesData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugRanges, std::nullopt,
                          m_data_debug_ranges);
}

const DWARFDataExtractor &DWARFContext::getOrLoadRngListsData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugRngLists,
                          eSectionTypeDWARFDebugRngListsDwo,
                          m_data_debug_rnglists);
}

const DWARFDataExtractor &DWARFContext::getOrLoadStrData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugStr,
                          eSectionTypeDWARFDebugStrDwo, m_data_debug_str);
}

const DWARFDataExtractor &DWARFContext::getOrLoadStrOffsetsData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugStrOffsets,
                          eSectionTypeDWARFDebugStrOffsetsDwo,
                          m_data_debug_str_offsets);
}

const DWARFDataExtractor &DWARFContext::getOrLoadDebugTypesData() {
  return LoadOrGetSection(eSectionTypeDWARFDebugTypes,
                          eSectionTypeDWARFDebugTypesDwo, m_data_debug_types);
}

llvm::DWARFContext &DWARFContext::GetAsLLVM() {
  if (!m_llvm_context) {
    llvm::StringMap<std::unique_ptr<llvm::MemoryBuffer>> section_map;
    uint8_t addr_size = 0;
    auto AddSection = [&](llvm::StringRef name, DWARFDataExtractor data) {
      // Set the address size the first time we see it.
      if (addr_size == 0)
        addr_size = data.GetAddressByteSize();

      section_map.try_emplace(
          name, llvm::MemoryBuffer::getMemBuffer(toStringRef(data.GetData()),
                                                 name, false));
    };

    AddSection("debug_line_str", getOrLoadLineStrData());
    AddSection("debug_cu_index", getOrLoadCuIndexData());
    AddSection("debug_tu_index", getOrLoadTuIndexData());
    if (isDwo()) {
      AddSection("debug_info.dwo", getOrLoadDebugInfoData());
      AddSection("debug_types.dwo", getOrLoadDebugTypesData());
    }
    m_llvm_context = llvm::DWARFContext::create(section_map, addr_size);
  }
  return *m_llvm_context;
}
