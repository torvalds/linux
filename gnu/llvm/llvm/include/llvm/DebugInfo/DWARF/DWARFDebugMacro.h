//===- DWARFDebugMacro.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGMACRO_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGMACRO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

class raw_ostream;

namespace dwarf_linker {
namespace classic {
class DwarfStreamer;
}
} // namespace dwarf_linker

class DWARFDebugMacro {
  friend dwarf_linker::classic::DwarfStreamer;
  friend dwarf_linker::parallel::CompileUnit;

  /// DWARFv5 section 6.3.1 Macro Information Header.
  enum HeaderFlagMask {
#define HANDLE_MACRO_FLAG(ID, NAME) MACRO_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  };
  struct MacroHeader {
    /// Macro version information number.
    uint16_t Version = 0;

    /// The bits of the flags field are interpreted as a set of flags, some of
    /// which may indicate that additional fields follow. The following flags,
    /// beginning with the least significant bit, are defined:
    /// offset_size_flag:
    ///   If the offset_size_flag is zero, the header is for a 32-bit DWARF
    ///   format macro section and all offsets are 4 bytes long; if it is one,
    ///   the header is for a 64-bit DWARF format macro section and all offsets
    ///   are 8 bytes long.
    /// debug_line_offset_flag:
    ///   If the debug_line_offset_flag is one, the debug_line_offset field (see
    ///   below) is present. If zero, that field is omitted.
    /// opcode_operands_table_flag:
    ///   If the opcode_operands_table_flag is one, the opcode_operands_table
    ///   field (see below) is present. If zero, that field is omitted.
    uint8_t Flags = 0;

    /// debug_line_offset
    ///   An offset in the .debug_line section of the beginning of the line
    ///   number information in the containing compilation unit, encoded as a
    ///   4-byte offset for a 32-bit DWARF format macro section and an 8-byte
    ///   offset for a 64-bit DWARF format macro section.
    uint64_t DebugLineOffset;

    /// Print the macro header from the debug_macro section.
    void dumpMacroHeader(raw_ostream &OS) const;

    /// Parse the debug_macro header.
    Error parseMacroHeader(DWARFDataExtractor Data, uint64_t *Offset);

    /// Get the DWARF format according to the flags.
    dwarf::DwarfFormat getDwarfFormat() const;

    /// Get the size of a reference according to the DWARF format.
    uint8_t getOffsetByteSize() const;
  };

  /// A single macro entry within a macro list.
  struct Entry {
    /// The type of the macro entry.
    uint32_t Type;
    union {
      /// The source line where the macro is defined.
      uint64_t Line;
      /// Vendor extension constant value.
      uint64_t ExtConstant;
      /// Macro unit import offset.
      uint64_t ImportOffset;
    };

    union {
      /// The string (name, value) of the macro entry.
      const char *MacroStr;
      // An unsigned integer indicating the identity of the source file.
      uint64_t File;
      /// Vendor extension string.
      const char *ExtStr;
    };
  };

  struct MacroList {
    // A value 0 in the `Header.Version` field indicates that we're parsing
    // a macinfo[.dwo] section which doesn't have header itself, hence
    // for that case other fields in the `Header` are uninitialized.
    MacroHeader Header;
    SmallVector<Entry, 4> Macros;
    uint64_t Offset;

    /// Whether or not this is a .debug_macro section.
    bool IsDebugMacro;
  };

  /// A list of all the macro entries in the debug_macinfo section.
  std::vector<MacroList> MacroLists;

public:
  DWARFDebugMacro() = default;

  /// Print the macro list found within the debug_macinfo/debug_macro section.
  void dump(raw_ostream &OS) const;

  Error parseMacro(DWARFUnitVector::compile_unit_range Units,
                   DataExtractor StringExtractor,
                   DWARFDataExtractor MacroData) {
    return parseImpl(Units, StringExtractor, MacroData, /*IsMacro=*/true);
  }

  Error parseMacinfo(DWARFDataExtractor MacroData) {
    return parseImpl(std::nullopt, std::nullopt, MacroData, /*IsMacro=*/false);
  }

  /// Return whether the section has any entries.
  bool empty() const { return MacroLists.empty(); }

  bool hasEntryForOffset(uint64_t Offset) const {
    for (const MacroList &List : MacroLists)
      if (Offset == List.Offset)
        return true;

    return false;
  }

private:
  /// Parse the debug_macinfo/debug_macro section accessible via the 'MacroData'
  /// parameter.
  Error parseImpl(std::optional<DWARFUnitVector::compile_unit_range> Units,
                  std::optional<DataExtractor> StringExtractor,
                  DWARFDataExtractor Data, bool IsMacro);
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGMACRO_H
