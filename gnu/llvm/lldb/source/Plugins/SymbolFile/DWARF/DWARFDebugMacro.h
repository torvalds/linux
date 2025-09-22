//===-- DWARFDebugMacro.h ----------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEBUGMACRO_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEBUGMACRO_H

#include <map>

#include "lldb/Core/dwarf.h"
#include "lldb/Symbol/DebugMacros.h"
#include "lldb/lldb-types.h"

namespace lldb_private {
class DWARFDataExtractor;
}

namespace lldb_private::plugin {
namespace dwarf {
class SymbolFileDWARF;

class DWARFDebugMacroHeader {
public:
  enum HeaderFlagMask {
    OFFSET_SIZE_MASK = 0x1,
    DEBUG_LINE_OFFSET_MASK = 0x2,
    OPCODE_OPERANDS_TABLE_MASK = 0x4
  };

  static DWARFDebugMacroHeader
  ParseHeader(const DWARFDataExtractor &debug_macro_data,
              lldb::offset_t *offset);

  bool OffsetIs64Bit() const { return m_offset_is_64_bit; }

private:
  static void SkipOperandTable(const DWARFDataExtractor &debug_macro_data,
                               lldb::offset_t *offset);

  uint16_t m_version = 0;
  bool m_offset_is_64_bit = false;
  uint64_t m_debug_line_offset = 0;
};

class DWARFDebugMacroEntry {
public:
  static void ReadMacroEntries(const DWARFDataExtractor &debug_macro_data,
                               const DWARFDataExtractor &debug_str_data,
                               const bool offset_is_64_bit,
                               lldb::offset_t *sect_offset,
                               SymbolFileDWARF *sym_file_dwarf,
                               DebugMacrosSP &debug_macros_sp);
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFDEBUGMACRO_H
