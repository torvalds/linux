//===- DWARFDebugMacro.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugMacro.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

using namespace llvm;
using namespace dwarf;

DwarfFormat DWARFDebugMacro::MacroHeader::getDwarfFormat() const {
  return Flags & MACRO_OFFSET_SIZE ? DWARF64 : DWARF32;
}

uint8_t DWARFDebugMacro::MacroHeader::getOffsetByteSize() const {
  return getDwarfOffsetByteSize(getDwarfFormat());
}

void DWARFDebugMacro::MacroHeader::dumpMacroHeader(raw_ostream &OS) const {
  // FIXME: Add support for dumping opcode_operands_table
  OS << format("macro header: version = 0x%04" PRIx16, Version)
     << format(", flags = 0x%02" PRIx8, Flags)
     << ", format = " << FormatString(getDwarfFormat());
  if (Flags & MACRO_DEBUG_LINE_OFFSET)
    OS << format(", debug_line_offset = 0x%0*" PRIx64, 2 * getOffsetByteSize(),
                 DebugLineOffset);
  OS << "\n";
}

void DWARFDebugMacro::dump(raw_ostream &OS) const {
  unsigned IndLevel = 0;
  for (const auto &Macros : MacroLists) {
    OS << format("0x%08" PRIx64 ":\n", Macros.Offset);
    if (Macros.IsDebugMacro)
      Macros.Header.dumpMacroHeader(OS);
    for (const Entry &E : Macros.Macros) {
      // There should not be DW_MACINFO_end_file when IndLevel is Zero. However,
      // this check handles the case of corrupted ".debug_macinfo" section.
      if (IndLevel > 0)
        IndLevel -= (E.Type == DW_MACINFO_end_file);
      // Print indentation.
      for (unsigned I = 0; I < IndLevel; I++)
        OS << "  ";
      IndLevel += (E.Type == DW_MACINFO_start_file);
      // Based on which version we are handling choose appropriate macro forms.
      if (Macros.IsDebugMacro)
        WithColor(OS, HighlightColor::Macro).get()
            << (Macros.Header.Version < 5 ? GnuMacroString(E.Type)
                                          : MacroString(E.Type));
      else
        WithColor(OS, HighlightColor::Macro).get() << MacinfoString(E.Type);
      switch (E.Type) {
      default:
        // Got a corrupted ".debug_macinfo/.debug_macro" section (invalid
        // macinfo type).
        break;
        // debug_macro and debug_macinfo share some common encodings.
        // DW_MACRO_define     == DW_MACINFO_define
        // DW_MACRO_undef      == DW_MACINFO_undef
        // DW_MACRO_start_file == DW_MACINFO_start_file
        // DW_MACRO_end_file   == DW_MACINFO_end_file
        // For readability/uniformity we are using DW_MACRO_*.
        //
        // The GNU .debug_macro extension's entries have the same encoding
        // as DWARF 5's DW_MACRO_* entries, so we only use the latter here.
      case DW_MACRO_define:
      case DW_MACRO_undef:
      case DW_MACRO_define_strp:
      case DW_MACRO_undef_strp:
      case DW_MACRO_define_strx:
      case DW_MACRO_undef_strx:
        OS << " - lineno: " << E.Line;
        OS << " macro: " << E.MacroStr;
        break;
      case DW_MACRO_start_file:
        OS << " - lineno: " << E.Line;
        OS << " filenum: " << E.File;
        break;
      case DW_MACRO_import:
        OS << format(" - import offset: 0x%0*" PRIx64,
                     2 * Macros.Header.getOffsetByteSize(), E.ImportOffset);
        break;
      case DW_MACRO_end_file:
        break;
      case DW_MACINFO_vendor_ext:
        OS << " - constant: " << E.ExtConstant;
        OS << " string: " << E.ExtStr;
        break;
      }
      OS << "\n";
    }
  }
}

Error DWARFDebugMacro::parseImpl(
    std::optional<DWARFUnitVector::compile_unit_range> Units,
    std::optional<DataExtractor> StringExtractor, DWARFDataExtractor Data,
    bool IsMacro) {
  uint64_t Offset = 0;
  MacroList *M = nullptr;
  using MacroToUnitsMap = DenseMap<uint64_t, DWARFUnit *>;
  MacroToUnitsMap MacroToUnits;
  if (IsMacro && Data.isValidOffset(Offset)) {
    // Keep a mapping from Macro contribution to CUs, this will
    // be needed while retrieving macro from DW_MACRO_define_strx form.
    for (const auto &U : *Units)
      if (auto CUDIE = U->getUnitDIE())
        // Skip units which does not contibutes to macro section.
        if (auto MacroOffset = toSectionOffset(CUDIE.find(DW_AT_macros)))
          MacroToUnits.try_emplace(*MacroOffset, U.get());
  }
  while (Data.isValidOffset(Offset)) {
    if (!M) {
      MacroLists.emplace_back();
      M = &MacroLists.back();
      M->Offset = Offset;
      M->IsDebugMacro = IsMacro;
      if (IsMacro) {
        auto Err = M->Header.parseMacroHeader(Data, &Offset);
        if (Err)
          return Err;
      }
    }
    // A macro list entry consists of:
    M->Macros.emplace_back();
    Entry &E = M->Macros.back();
    // 1. Macinfo type
    E.Type = Data.getULEB128(&Offset);

    if (E.Type == 0) {
      // Reached end of a ".debug_macinfo/debug_macro" section contribution.
      M = nullptr;
      continue;
    }

    switch (E.Type) {
    default:
      // Got a corrupted ".debug_macinfo" section (invalid macinfo type).
      // Push the corrupted entry to the list and halt parsing.
      E.Type = DW_MACINFO_invalid;
      return Error::success();
    // debug_macro and debug_macinfo share some common encodings.
    // DW_MACRO_define     == DW_MACINFO_define
    // DW_MACRO_undef      == DW_MACINFO_undef
    // DW_MACRO_start_file == DW_MACINFO_start_file
    // DW_MACRO_end_file   == DW_MACINFO_end_file
    // For readibility/uniformity we are using DW_MACRO_*.
    case DW_MACRO_define:
    case DW_MACRO_undef:
      // 2. Source line
      E.Line = Data.getULEB128(&Offset);
      // 3. Macro string
      E.MacroStr = Data.getCStr(&Offset);
      break;
    case DW_MACRO_define_strp:
    case DW_MACRO_undef_strp: {
      if (!IsMacro) {
        // DW_MACRO_define_strp is a new form introduced in DWARFv5, it is
        // not supported in debug_macinfo[.dwo] sections. Assume it as an
        // invalid entry, push it and halt parsing.
        E.Type = DW_MACINFO_invalid;
        return Error::success();
      }
      uint64_t StrOffset = 0;
      // 2. Source line
      E.Line = Data.getULEB128(&Offset);
      // 3. Macro string
      StrOffset =
          Data.getRelocatedValue(M->Header.getOffsetByteSize(), &Offset);
      assert(StringExtractor && "String Extractor not found");
      E.MacroStr = StringExtractor->getCStr(&StrOffset);
      break;
    }
    case DW_MACRO_define_strx:
    case DW_MACRO_undef_strx: {
      if (!IsMacro) {
        // DW_MACRO_define_strx is a new form introduced in DWARFv5, it is
        // not supported in debug_macinfo[.dwo] sections. Assume it as an
        // invalid entry, push it and halt parsing.
        E.Type = DW_MACINFO_invalid;
        return Error::success();
      }
      E.Line = Data.getULEB128(&Offset);
      auto MacroContributionOffset = MacroToUnits.find(M->Offset);
      if (MacroContributionOffset == MacroToUnits.end())
        return createStringError(errc::invalid_argument,
                                 "Macro contribution of the unit not found");
      Expected<uint64_t> StrOffset =
          MacroContributionOffset->second->getStringOffsetSectionItem(
              Data.getULEB128(&Offset));
      if (!StrOffset)
        return StrOffset.takeError();
      E.MacroStr =
          MacroContributionOffset->second->getStringExtractor().getCStr(
              &*StrOffset);
      break;
    }
    case DW_MACRO_start_file:
      // 2. Source line
      E.Line = Data.getULEB128(&Offset);
      // 3. Source file id
      E.File = Data.getULEB128(&Offset);
      break;
    case DW_MACRO_end_file:
      break;
    case DW_MACRO_import:
      E.ImportOffset =
          Data.getRelocatedValue(M->Header.getOffsetByteSize(), &Offset);
      break;
    case DW_MACINFO_vendor_ext:
      // 2. Vendor extension constant
      E.ExtConstant = Data.getULEB128(&Offset);
      // 3. Vendor extension string
      E.ExtStr = Data.getCStr(&Offset);
      break;
    }
  }
  return Error::success();
}

Error DWARFDebugMacro::MacroHeader::parseMacroHeader(DWARFDataExtractor Data,
                                                     uint64_t *Offset) {
  Version = Data.getU16(Offset);
  uint8_t FlagData = Data.getU8(Offset);

  // FIXME: Add support for parsing opcode_operands_table
  if (FlagData & MACRO_OPCODE_OPERANDS_TABLE)
    return createStringError(errc::not_supported,
                             "opcode_operands_table is not supported");
  Flags = FlagData;
  if (Flags & MACRO_DEBUG_LINE_OFFSET)
    DebugLineOffset = Data.getUnsigned(Offset, getOffsetByteSize());
  return Error::success();
}
