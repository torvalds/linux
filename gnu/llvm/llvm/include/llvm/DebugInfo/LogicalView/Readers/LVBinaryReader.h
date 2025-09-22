//===-- LVBinaryReader.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVBinaryReader class, which is used to describe a
// binary reader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVBINARYREADER_H
#define LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVBINARYREADER_H

#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ObjectFile.h"

namespace llvm {
namespace logicalview {

constexpr bool UpdateHighAddress = false;

// Logical scope, Section address, Section index, IsComdat.
struct LVSymbolTableEntry final {
  LVScope *Scope = nullptr;
  LVAddress Address = 0;
  LVSectionIndex SectionIndex = 0;
  bool IsComdat = false;
  LVSymbolTableEntry() = default;
  LVSymbolTableEntry(LVScope *Scope, LVAddress Address,
                     LVSectionIndex SectionIndex, bool IsComdat)
      : Scope(Scope), Address(Address), SectionIndex(SectionIndex),
        IsComdat(IsComdat) {}
};

// Function names extracted from the object symbol table.
class LVSymbolTable final {
  using LVSymbolNames = std::map<std::string, LVSymbolTableEntry>;
  LVSymbolNames SymbolNames;

public:
  LVSymbolTable() = default;

  void add(StringRef Name, LVScope *Function, LVSectionIndex SectionIndex = 0);
  void add(StringRef Name, LVAddress Address, LVSectionIndex SectionIndex,
           bool IsComdat);
  LVSectionIndex update(LVScope *Function);

  const LVSymbolTableEntry &getEntry(StringRef Name);
  LVAddress getAddress(StringRef Name);
  LVSectionIndex getIndex(StringRef Name);
  bool getIsComdat(StringRef Name);

  void print(raw_ostream &OS);
};

class LVBinaryReader : public LVReader {
  // Function names extracted from the object symbol table.
  LVSymbolTable SymbolTable;

  // It contains the LVLineDebug elements representing the inlined logical
  // lines for the current compile unit, created by parsing the CodeView
  // S_INLINESITE symbol annotation data.
  using LVInlineeLine = std::map<LVScope *, std::unique_ptr<LVLines>>;
  LVInlineeLine CUInlineeLines;

  // Instruction lines for a logical scope. These instructions are fetched
  // during its merge with the debug lines.
  LVDoubleMap<LVSectionIndex, LVScope *, LVLines *> ScopeInstructions;

  // Links the scope with its first assembler address line.
  LVDoubleMap<LVSectionIndex, LVAddress, LVScope *> AssemblerMappings;

  // Mapping from virtual address to section.
  // The virtual address refers to the address where the section is loaded.
  using LVSectionAddresses = std::map<LVSectionIndex, object::SectionRef>;
  LVSectionAddresses SectionAddresses;

  void addSectionAddress(const object::SectionRef &Section) {
    if (SectionAddresses.find(Section.getAddress()) == SectionAddresses.end())
      SectionAddresses.emplace(Section.getAddress(), Section);
  }

  // Scopes with ranges for current compile unit. It is used to find a line
  // giving its exact or closest address. To support comdat functions, all
  // addresses for the same section are recorded in the same map.
  using LVSectionRanges = std::map<LVSectionIndex, std::unique_ptr<LVRange>>;
  LVSectionRanges SectionRanges;

  // Image base and virtual address for Executable file.
  uint64_t ImageBaseAddress = 0;
  uint64_t VirtualAddress = 0;

  // Object sections with machine code.
  using LVSections = std::map<LVSectionIndex, object::SectionRef>;
  LVSections Sections;

  std::vector<std::unique_ptr<LVLines>> DiscoveredLines;

protected:
  // It contains the LVLineDebug elements representing the logical lines for
  // the current compile unit, created by parsing the debug line section.
  LVLines CULines;

  std::unique_ptr<const MCRegisterInfo> MRI;
  std::unique_ptr<const MCAsmInfo> MAI;
  std::unique_ptr<const MCSubtargetInfo> STI;
  std::unique_ptr<const MCInstrInfo> MII;
  std::unique_ptr<const MCDisassembler> MD;
  std::unique_ptr<MCContext> MC;
  std::unique_ptr<MCInstPrinter> MIP;

  // https://yurydelendik.github.io/webassembly-dwarf/
  // 2. Consuming and Generating DWARF for WebAssembly Code
  // Note: Some DWARF constructs don't map one-to-one onto WebAssembly
  // constructs. We strive to enumerate and resolve any ambiguities here.
  //
  // 2.1. Code Addresses
  // Note: DWARF associates various bits of debug info
  // with particular locations in the program via its code address (instruction
  // pointer or PC). However, WebAssembly's linear memory address space does not
  // contain WebAssembly instructions.
  //
  // Wherever a code address (see 2.17 of [DWARF]) is used in DWARF for
  // WebAssembly, it must be the offset of an instruction relative within the
  // Code section of the WebAssembly file. The DWARF is considered malformed if
  // a PC offset is between instruction boundaries within the Code section.
  //
  // Note: It is expected that a DWARF consumer does not know how to decode
  // WebAssembly instructions. The instruction pointer is selected as the offset
  // in the binary file of the first byte of the instruction, and it is
  // consistent with the WebAssembly Web API conventions definition of the code
  // location.
  //
  // EXAMPLE: .DEBUG_LINE INSTRUCTION POINTERS
  // The .debug_line DWARF section maps instruction pointers to source
  // locations. With WebAssembly, the .debug_line section maps Code
  // section-relative instruction offsets to source locations.
  //
  // EXAMPLE: DW_AT_* ATTRIBUTES
  // For entities with a single associated code address, DWARF uses
  // the DW_AT_low_pc attribute to specify the associated code address value.
  // For WebAssembly, the DW_AT_low_pc's value is a Code section-relative
  // instruction offset.
  //
  // For entities with a single contiguous range of code, DWARF uses a
  // pair of DW_AT_low_pc and DW_AT_high_pc attributes to specify the associated
  // contiguous range of code address values. For WebAssembly, these attributes
  // are Code section-relative instruction offsets.
  //
  // For entities with multiple ranges of code, DWARF uses the DW_AT_ranges
  // attribute, which refers to the array located at the .debug_ranges section.
  LVAddress WasmCodeSectionOffset = 0;

  // Loads all info for the architecture of the provided object file.
  Error loadGenericTargetInfo(StringRef TheTriple, StringRef TheFeatures);

  virtual void mapRangeAddress(const object::ObjectFile &Obj) {}
  virtual void mapRangeAddress(const object::ObjectFile &Obj,
                               const object::SectionRef &Section,
                               bool IsComdat) {}

  // Create a mapping from virtual address to section.
  void mapVirtualAddress(const object::ObjectFile &Obj);
  void mapVirtualAddress(const object::COFFObjectFile &COFFObj);

  Expected<std::pair<LVSectionIndex, object::SectionRef>>
  getSection(LVScope *Scope, LVAddress Address, LVSectionIndex SectionIndex);

  void addSectionRange(LVSectionIndex SectionIndex, LVScope *Scope);
  void addSectionRange(LVSectionIndex SectionIndex, LVScope *Scope,
                       LVAddress LowerAddress, LVAddress UpperAddress);
  LVRange *getSectionRanges(LVSectionIndex SectionIndex);

  void includeInlineeLines(LVSectionIndex SectionIndex, LVScope *Function);

  Error createInstructions();
  Error createInstructions(LVScope *Function, LVSectionIndex SectionIndex);
  Error createInstructions(LVScope *Function, LVSectionIndex SectionIndex,
                           const LVNameInfo &NameInfo);

  void processLines(LVLines *DebugLines, LVSectionIndex SectionIndex);
  void processLines(LVLines *DebugLines, LVSectionIndex SectionIndex,
                    LVScope *Function);

public:
  LVBinaryReader() = delete;
  LVBinaryReader(StringRef Filename, StringRef FileFormatName, ScopedPrinter &W,
                 LVBinaryType BinaryType)
      : LVReader(Filename, FileFormatName, W, BinaryType) {}
  LVBinaryReader(const LVBinaryReader &) = delete;
  LVBinaryReader &operator=(const LVBinaryReader &) = delete;
  virtual ~LVBinaryReader() = default;

  void addInlineeLines(LVScope *Scope, LVLines &Lines) {
    CUInlineeLines.emplace(Scope, std::make_unique<LVLines>(std::move(Lines)));
  }

  // Convert Segment::Offset pair to absolute address.
  LVAddress linearAddress(uint16_t Segment, uint32_t Offset,
                          LVAddress Addendum = 0) {
    return ImageBaseAddress + (Segment * VirtualAddress) + Offset + Addendum;
  }

  void addToSymbolTable(StringRef Name, LVScope *Function,
                        LVSectionIndex SectionIndex = 0);
  void addToSymbolTable(StringRef Name, LVAddress Address,
                        LVSectionIndex SectionIndex, bool IsComdat);
  LVSectionIndex updateSymbolTable(LVScope *Function);

  const LVSymbolTableEntry &getSymbolTableEntry(StringRef Name);
  LVAddress getSymbolTableAddress(StringRef Name);
  LVSectionIndex getSymbolTableIndex(StringRef Name);
  bool getSymbolTableIsComdat(StringRef Name);

  LVSectionIndex getSectionIndex(LVScope *Scope) override {
    return Scope ? getSymbolTableIndex(Scope->getLinkageName())
                 : DotTextSectionIndex;
  }

  void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const { print(dbgs()); }
#endif
};

} // end namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVBINARYREADER_H
