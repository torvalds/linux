//===-- DWARFContext.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFCONTEXT_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFCONTEXT_H

#include "DWARFDataExtractor.h"
#include "lldb/Core/Section.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Support/Threading.h"
#include <memory>
#include <optional>

namespace lldb_private::plugin {
namespace dwarf {
class DWARFContext {
private:
  SectionList *m_main_section_list;
  SectionList *m_dwo_section_list;
  mutable std::unique_ptr<llvm::DWARFContext> m_llvm_context;

  struct SectionData {
    llvm::once_flag flag;
    DWARFDataExtractor data;
  };

  SectionData m_data_debug_abbrev;
  SectionData m_data_debug_addr;
  SectionData m_data_debug_aranges;
  SectionData m_data_debug_cu_index;
  SectionData m_data_debug_info;
  SectionData m_data_debug_line;
  SectionData m_data_debug_line_str;
  SectionData m_data_debug_loc;
  SectionData m_data_debug_loclists;
  SectionData m_data_debug_macro;
  SectionData m_data_debug_ranges;
  SectionData m_data_debug_rnglists;
  SectionData m_data_debug_str;
  SectionData m_data_debug_str_offsets;
  SectionData m_data_debug_tu_index;
  SectionData m_data_debug_types;

  const DWARFDataExtractor &
  LoadOrGetSection(std::optional<lldb::SectionType> main_section_type,
                   std::optional<lldb::SectionType> dwo_section_type,
                   SectionData &data);

  const DWARFDataExtractor &getOrLoadCuIndexData();
  const DWARFDataExtractor &getOrLoadTuIndexData();

public:
  explicit DWARFContext(SectionList *main_section_list,
                        SectionList *dwo_section_list)
      : m_main_section_list(main_section_list),
        m_dwo_section_list(dwo_section_list) {}

  const DWARFDataExtractor &getOrLoadAbbrevData();
  const DWARFDataExtractor &getOrLoadAddrData();
  const DWARFDataExtractor &getOrLoadArangesData();
  const DWARFDataExtractor &getOrLoadDebugInfoData();
  const DWARFDataExtractor &getOrLoadLineData();
  const DWARFDataExtractor &getOrLoadLineStrData();
  const DWARFDataExtractor &getOrLoadLocData();
  const DWARFDataExtractor &getOrLoadLocListsData();
  const DWARFDataExtractor &getOrLoadMacroData();
  const DWARFDataExtractor &getOrLoadRangesData();
  const DWARFDataExtractor &getOrLoadRngListsData();
  const DWARFDataExtractor &getOrLoadStrData();
  const DWARFDataExtractor &getOrLoadStrOffsetsData();
  const DWARFDataExtractor &getOrLoadDebugTypesData();

  bool isDwo() { return m_dwo_section_list != nullptr; }

  llvm::DWARFContext &GetAsLLVM();
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif
