//===-- LVDWARFReader.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVDWARFReader class, which is used to describe a
// debug information (DWARF) reader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVDWARFREADER_H
#define LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVDWARFREADER_H

#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/LogicalView/Readers/LVBinaryReader.h"
#include <unordered_set>

namespace llvm {
namespace logicalview {

class LVElement;
class LVLine;
class LVScopeCompileUnit;
class LVSymbol;
class LVType;

using AttributeSpec = DWARFAbbreviationDeclaration::AttributeSpec;

class LVDWARFReader final : public LVBinaryReader {
  object::ObjectFile &Obj;

  // Indicates if ranges data are available; in the case of split DWARF any
  // reference to ranges is valid only if the skeleton DIE has been loaded.
  bool RangesDataAvailable = false;
  LVAddress CUBaseAddress = 0;
  LVAddress CUHighAddress = 0;

  // Current elements during the processing of a DIE.
  LVElement *CurrentElement = nullptr;
  LVScope *CurrentScope = nullptr;
  LVSymbol *CurrentSymbol = nullptr;
  LVType *CurrentType = nullptr;
  LVOffset CurrentOffset = 0;
  LVOffset CurrentEndOffset = 0;

  // In DWARF v4, the files are 1-indexed.
  // In DWARF v5, the files are 0-indexed.
  // The DWARF reader expects the indexes as 1-indexed.
  bool IncrementFileIndex = false;

  // Address ranges collected for current DIE.
  std::vector<LVAddressRange> CurrentRanges;

  // Symbols with locations for current compile unit.
  LVSymbols SymbolsWithLocations;

  // Global Offsets (Offset, Element).
  LVOffsetElementMap GlobalOffsets;

  // Low PC and High PC values for DIE being processed.
  LVAddress CurrentLowPC = 0;
  LVAddress CurrentHighPC = 0;
  bool FoundLowPC = false;
  bool FoundHighPC = false;

  // Cross references (Elements).
  using LVElementSet = std::unordered_set<LVElement *>;
  struct LVElementEntry {
    LVElement *Element;
    LVElementSet References;
    LVElementSet Types;
    LVElementEntry(LVElement *Element = nullptr) : Element(Element) {}
  };
  using LVElementReference = std::unordered_map<LVOffset, LVElementEntry>;
  LVElementReference ElementTable;

  Error loadTargetInfo(const object::ObjectFile &Obj);

  void mapRangeAddress(const object::ObjectFile &Obj) override;

  LVElement *createElement(dwarf::Tag Tag);
  void traverseDieAndChildren(DWARFDie &DIE, LVScope *Parent,
                              DWARFDie &SkeletonDie);
  // Process the attributes for the given DIE.
  LVScope *processOneDie(const DWARFDie &InputDIE, LVScope *Parent,
                         DWARFDie &SkeletonDie);
  void processOneAttribute(const DWARFDie &Die, LVOffset *OffsetPtr,
                           const AttributeSpec &AttrSpec);
  void createLineAndFileRecords(const DWARFDebugLine::LineTable *Lines);
  void processLocationGaps();

  // Add offset to global map.
  void addGlobalOffset(LVOffset Offset) {
    if (GlobalOffsets.find(Offset) == GlobalOffsets.end())
      // Just associate the DIE offset with a null element, as we do not
      // know if the referenced element has been created.
      GlobalOffsets.emplace(Offset, nullptr);
  }

  // Remove offset from global map.
  void removeGlobalOffset(LVOffset Offset) {
    LVOffsetElementMap::iterator Iter = GlobalOffsets.find(Offset);
    if (Iter != GlobalOffsets.end())
      GlobalOffsets.erase(Iter);
  }

  // Get the location information for DW_AT_data_member_location.
  void processLocationMember(dwarf::Attribute Attr,
                             const DWARFFormValue &FormValue,
                             const DWARFDie &Die, uint64_t OffsetOnEntry);
  void processLocationList(dwarf::Attribute Attr,
                           const DWARFFormValue &FormValue, const DWARFDie &Die,
                           uint64_t OffsetOnEntry,
                           bool CallSiteLocation = false);
  void updateReference(dwarf::Attribute Attr, const DWARFFormValue &FormValue);

  // Get an element given the DIE offset.
  LVElement *getElementForOffset(LVOffset offset, LVElement *Element,
                                 bool IsType);

protected:
  Error createScopes() override;
  void sortScopes() override;

public:
  LVDWARFReader() = delete;
  LVDWARFReader(StringRef Filename, StringRef FileFormatName,
                object::ObjectFile &Obj, ScopedPrinter &W)
      : LVBinaryReader(Filename, FileFormatName, W, LVBinaryType::ELF),
        Obj(Obj) {}
  LVDWARFReader(const LVDWARFReader &) = delete;
  LVDWARFReader &operator=(const LVDWARFReader &) = delete;
  ~LVDWARFReader() = default;

  LVAddress getCUBaseAddress() const { return CUBaseAddress; }
  void setCUBaseAddress(LVAddress Address) { CUBaseAddress = Address; }
  LVAddress getCUHighAddress() const { return CUHighAddress; }
  void setCUHighAddress(LVAddress Address) { CUHighAddress = Address; }

  const LVSymbols &GetSymbolsWithLocations() const {
    return SymbolsWithLocations;
  }

  std::string getRegisterName(LVSmall Opcode,
                              ArrayRef<uint64_t> Operands) override;

  void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const { print(dbgs()); }
#endif
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVDWARFREADER_H
