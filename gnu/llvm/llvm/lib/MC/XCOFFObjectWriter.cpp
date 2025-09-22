//===-- lib/MC/XCOFFObjectWriter.cpp - XCOFF file writer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements XCOFF object file writer information.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSectionXCOFF.h"
#include "llvm/MC/MCSymbolXCOFF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCXCOFFObjectWriter.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

#include <deque>
#include <map>

using namespace llvm;

// An XCOFF object file has a limited set of predefined sections. The most
// important ones for us (right now) are:
// .text --> contains program code and read-only data.
// .data --> contains initialized data, function descriptors, and the TOC.
// .bss  --> contains uninitialized data.
// Each of these sections is composed of 'Control Sections'. A Control Section
// is more commonly referred to as a csect. A csect is an indivisible unit of
// code or data, and acts as a container for symbols. A csect is mapped
// into a section based on its storage-mapping class, with the exception of
// XMC_RW which gets mapped to either .data or .bss based on whether it's
// explicitly initialized or not.
//
// We don't represent the sections in the MC layer as there is nothing
// interesting about them at at that level: they carry information that is
// only relevant to the ObjectWriter, so we materialize them in this class.
namespace {

constexpr unsigned DefaultSectionAlign = 4;
constexpr int16_t MaxSectionIndex = INT16_MAX;

// Packs the csect's alignment and type into a byte.
uint8_t getEncodedType(const MCSectionXCOFF *);

struct XCOFFRelocation {
  uint32_t SymbolTableIndex;
  uint32_t FixupOffsetInCsect;
  uint8_t SignAndSize;
  uint8_t Type;
};

// Wrapper around an MCSymbolXCOFF.
struct Symbol {
  const MCSymbolXCOFF *const MCSym;
  uint32_t SymbolTableIndex;

  XCOFF::VisibilityType getVisibilityType() const {
    return MCSym->getVisibilityType();
  }

  XCOFF::StorageClass getStorageClass() const {
    return MCSym->getStorageClass();
  }
  StringRef getSymbolTableName() const { return MCSym->getSymbolTableName(); }
  Symbol(const MCSymbolXCOFF *MCSym) : MCSym(MCSym), SymbolTableIndex(-1) {}
};

// Wrapper for an MCSectionXCOFF.
// It can be a Csect or debug section or DWARF section and so on.
struct XCOFFSection {
  const MCSectionXCOFF *const MCSec;
  uint32_t SymbolTableIndex;
  uint64_t Address;
  uint64_t Size;

  SmallVector<Symbol, 1> Syms;
  SmallVector<XCOFFRelocation, 1> Relocations;
  StringRef getSymbolTableName() const { return MCSec->getSymbolTableName(); }
  XCOFF::VisibilityType getVisibilityType() const {
    return MCSec->getVisibilityType();
  }
  XCOFFSection(const MCSectionXCOFF *MCSec)
      : MCSec(MCSec), SymbolTableIndex(-1), Address(-1), Size(0) {}
};

// Type to be used for a container representing a set of csects with
// (approximately) the same storage mapping class. For example all the csects
// with a storage mapping class of `xmc_pr` will get placed into the same
// container.
using CsectGroup = std::deque<XCOFFSection>;
using CsectGroups = std::deque<CsectGroup *>;

// The basic section entry defination. This Section represents a section entry
// in XCOFF section header table.
struct SectionEntry {
  char Name[XCOFF::NameSize];
  // The physical/virtual address of the section. For an object file these
  // values are equivalent, except for in the overflow section header, where
  // the physical address specifies the number of relocation entries and the
  // virtual address specifies the number of line number entries.
  // TODO: Divide Address into PhysicalAddress and VirtualAddress when line
  // number entries are supported.
  uint64_t Address;
  uint64_t Size;
  uint64_t FileOffsetToData;
  uint64_t FileOffsetToRelocations;
  uint32_t RelocationCount;
  int32_t Flags;

  int16_t Index;

  virtual uint64_t advanceFileOffset(const uint64_t MaxRawDataSize,
                                     const uint64_t RawPointer) {
    FileOffsetToData = RawPointer;
    uint64_t NewPointer = RawPointer + Size;
    if (NewPointer > MaxRawDataSize)
      report_fatal_error("Section raw data overflowed this object file.");
    return NewPointer;
  }

  // XCOFF has special section numbers for symbols:
  // -2 Specifies N_DEBUG, a special symbolic debugging symbol.
  // -1 Specifies N_ABS, an absolute symbol. The symbol has a value but is not
  // relocatable.
  //  0 Specifies N_UNDEF, an undefined external symbol.
  // Therefore, we choose -3 (N_DEBUG - 1) to represent a section index that
  // hasn't been initialized.
  static constexpr int16_t UninitializedIndex =
      XCOFF::ReservedSectionNum::N_DEBUG - 1;

  SectionEntry(StringRef N, int32_t Flags)
      : Name(), Address(0), Size(0), FileOffsetToData(0),
        FileOffsetToRelocations(0), RelocationCount(0), Flags(Flags),
        Index(UninitializedIndex) {
    assert(N.size() <= XCOFF::NameSize && "section name too long");
    memcpy(Name, N.data(), N.size());
  }

  virtual void reset() {
    Address = 0;
    Size = 0;
    FileOffsetToData = 0;
    FileOffsetToRelocations = 0;
    RelocationCount = 0;
    Index = UninitializedIndex;
  }

  virtual ~SectionEntry() = default;
};

// Represents the data related to a section excluding the csects that make up
// the raw data of the section. The csects are stored separately as not all
// sections contain csects, and some sections contain csects which are better
// stored separately, e.g. the .data section containing read-write, descriptor,
// TOCBase and TOC-entry csects.
struct CsectSectionEntry : public SectionEntry {
  // Virtual sections do not need storage allocated in the object file.
  const bool IsVirtual;

  // This is a section containing csect groups.
  CsectGroups Groups;

  CsectSectionEntry(StringRef N, XCOFF::SectionTypeFlags Flags, bool IsVirtual,
                    CsectGroups Groups)
      : SectionEntry(N, Flags), IsVirtual(IsVirtual), Groups(Groups) {
    assert(N.size() <= XCOFF::NameSize && "section name too long");
    memcpy(Name, N.data(), N.size());
  }

  void reset() override {
    SectionEntry::reset();
    // Clear any csects we have stored.
    for (auto *Group : Groups)
      Group->clear();
  }

  virtual ~CsectSectionEntry() = default;
};

struct DwarfSectionEntry : public SectionEntry {
  // For DWARF section entry.
  std::unique_ptr<XCOFFSection> DwarfSect;

  // For DWARF section, we must use real size in the section header. MemorySize
  // is for the size the DWARF section occupies including paddings.
  uint32_t MemorySize;

  // TODO: Remove this override. Loadable sections (e.g., .text, .data) may need
  // to be aligned. Other sections generally don't need any alignment, but if
  // they're aligned, the RawPointer should be adjusted before writing the
  // section. Then a dwarf-specific function wouldn't be needed.
  uint64_t advanceFileOffset(const uint64_t MaxRawDataSize,
                             const uint64_t RawPointer) override {
    FileOffsetToData = RawPointer;
    uint64_t NewPointer = RawPointer + MemorySize;
    assert(NewPointer <= MaxRawDataSize &&
           "Section raw data overflowed this object file.");
    return NewPointer;
  }

  DwarfSectionEntry(StringRef N, int32_t Flags,
                    std::unique_ptr<XCOFFSection> Sect)
      : SectionEntry(N, Flags | XCOFF::STYP_DWARF), DwarfSect(std::move(Sect)),
        MemorySize(0) {
    assert(DwarfSect->MCSec->isDwarfSect() &&
           "This should be a DWARF section!");
    assert(N.size() <= XCOFF::NameSize && "section name too long");
    memcpy(Name, N.data(), N.size());
  }

  DwarfSectionEntry(DwarfSectionEntry &&s) = default;

  virtual ~DwarfSectionEntry() = default;
};

struct ExceptionTableEntry {
  const MCSymbol *Trap;
  uint64_t TrapAddress = ~0ul;
  unsigned Lang;
  unsigned Reason;

  ExceptionTableEntry(const MCSymbol *Trap, unsigned Lang, unsigned Reason)
      : Trap(Trap), Lang(Lang), Reason(Reason) {}
};

struct ExceptionInfo {
  const MCSymbol *FunctionSymbol;
  unsigned FunctionSize;
  std::vector<ExceptionTableEntry> Entries;
};

struct ExceptionSectionEntry : public SectionEntry {
  std::map<const StringRef, ExceptionInfo> ExceptionTable;
  bool isDebugEnabled = false;

  ExceptionSectionEntry(StringRef N, int32_t Flags)
      : SectionEntry(N, Flags | XCOFF::STYP_EXCEPT) {
    assert(N.size() <= XCOFF::NameSize && "Section too long.");
    memcpy(Name, N.data(), N.size());
  }

  virtual ~ExceptionSectionEntry() = default;
};

struct CInfoSymInfo {
  // Name of the C_INFO symbol associated with the section
  std::string Name;
  std::string Metadata;
  // Offset into the start of the metadata in the section
  uint64_t Offset;

  CInfoSymInfo(std::string Name, std::string Metadata)
      : Name(Name), Metadata(Metadata) {}
  // Metadata needs to be padded out to an even word size.
  uint32_t paddingSize() const {
    return alignTo(Metadata.size(), sizeof(uint32_t)) - Metadata.size();
  };

  // Total size of the entry, including the 4 byte length
  uint32_t size() const {
    return Metadata.size() + paddingSize() + sizeof(uint32_t);
  };
};

struct CInfoSymSectionEntry : public SectionEntry {
  std::unique_ptr<CInfoSymInfo> Entry;

  CInfoSymSectionEntry(StringRef N, int32_t Flags) : SectionEntry(N, Flags) {}
  virtual ~CInfoSymSectionEntry() = default;
  void addEntry(std::unique_ptr<CInfoSymInfo> NewEntry) {
    Entry = std::move(NewEntry);
    Entry->Offset = sizeof(uint32_t);
    Size += Entry->size();
  }
  void reset() override {
    SectionEntry::reset();
    Entry.reset();
  }
};

class XCOFFObjectWriter : public MCObjectWriter {

  uint32_t SymbolTableEntryCount = 0;
  uint64_t SymbolTableOffset = 0;
  uint16_t SectionCount = 0;
  uint32_t PaddingsBeforeDwarf = 0;
  bool HasVisibility = false;

  support::endian::Writer W;
  std::unique_ptr<MCXCOFFObjectTargetWriter> TargetObjectWriter;
  StringTableBuilder Strings;

  const uint64_t MaxRawDataSize =
      TargetObjectWriter->is64Bit() ? UINT64_MAX : UINT32_MAX;

  // Maps the MCSection representation to its corresponding XCOFFSection
  // wrapper. Needed for finding the XCOFFSection to insert an MCSymbol into
  // from its containing MCSectionXCOFF.
  DenseMap<const MCSectionXCOFF *, XCOFFSection *> SectionMap;

  // Maps the MCSymbol representation to its corrresponding symbol table index.
  // Needed for relocation.
  DenseMap<const MCSymbol *, uint32_t> SymbolIndexMap;

  // CsectGroups. These store the csects which make up different parts of
  // the sections. Should have one for each set of csects that get mapped into
  // the same section and get handled in a 'similar' way.
  CsectGroup UndefinedCsects;
  CsectGroup ProgramCodeCsects;
  CsectGroup ReadOnlyCsects;
  CsectGroup DataCsects;
  CsectGroup FuncDSCsects;
  CsectGroup TOCCsects;
  CsectGroup BSSCsects;
  CsectGroup TDataCsects;
  CsectGroup TBSSCsects;

  // The Predefined sections.
  CsectSectionEntry Text;
  CsectSectionEntry Data;
  CsectSectionEntry BSS;
  CsectSectionEntry TData;
  CsectSectionEntry TBSS;

  // All the XCOFF sections, in the order they will appear in the section header
  // table.
  std::array<CsectSectionEntry *const, 5> Sections{
      {&Text, &Data, &BSS, &TData, &TBSS}};

  std::vector<DwarfSectionEntry> DwarfSections;
  std::vector<SectionEntry> OverflowSections;

  ExceptionSectionEntry ExceptionSection;
  CInfoSymSectionEntry CInfoSymSection;

  CsectGroup &getCsectGroup(const MCSectionXCOFF *MCSec);

  void reset() override;

  void executePostLayoutBinding(MCAssembler &) override;

  void recordRelocation(MCAssembler &, const MCFragment *, const MCFixup &,
                        MCValue, uint64_t &) override;

  uint64_t writeObject(MCAssembler &) override;

  bool is64Bit() const { return TargetObjectWriter->is64Bit(); }
  bool nameShouldBeInStringTable(const StringRef &);
  void writeSymbolName(const StringRef &);
  bool auxFileSymNameShouldBeInStringTable(const StringRef &);
  void writeAuxFileSymName(const StringRef &);

  void writeSymbolEntryForCsectMemberLabel(const Symbol &SymbolRef,
                                           const XCOFFSection &CSectionRef,
                                           int16_t SectionIndex,
                                           uint64_t SymbolOffset);
  void writeSymbolEntryForControlSection(const XCOFFSection &CSectionRef,
                                         int16_t SectionIndex,
                                         XCOFF::StorageClass StorageClass);
  void writeSymbolEntryForDwarfSection(const XCOFFSection &DwarfSectionRef,
                                       int16_t SectionIndex);
  void writeFileHeader();
  void writeAuxFileHeader();
  void writeSectionHeader(const SectionEntry *Sec);
  void writeSectionHeaderTable();
  void writeSections(const MCAssembler &Asm);
  void writeSectionForControlSectionEntry(const MCAssembler &Asm,
                                          const CsectSectionEntry &CsectEntry,
                                          uint64_t &CurrentAddressLocation);
  void writeSectionForDwarfSectionEntry(const MCAssembler &Asm,
                                        const DwarfSectionEntry &DwarfEntry,
                                        uint64_t &CurrentAddressLocation);
  void
  writeSectionForExceptionSectionEntry(const MCAssembler &Asm,
                                       ExceptionSectionEntry &ExceptionEntry,
                                       uint64_t &CurrentAddressLocation);
  void writeSectionForCInfoSymSectionEntry(const MCAssembler &Asm,
                                           CInfoSymSectionEntry &CInfoSymEntry,
                                           uint64_t &CurrentAddressLocation);
  void writeSymbolTable(MCAssembler &Asm);
  void writeSymbolAuxFileEntry(StringRef &Name, uint8_t ftype);
  void writeSymbolAuxDwarfEntry(uint64_t LengthOfSectionPortion,
                                uint64_t NumberOfRelocEnt = 0);
  void writeSymbolAuxCsectEntry(uint64_t SectionOrLength,
                                uint8_t SymbolAlignmentAndType,
                                uint8_t StorageMappingClass);
  void writeSymbolAuxFunctionEntry(uint32_t EntryOffset, uint32_t FunctionSize,
                                   uint64_t LineNumberPointer,
                                   uint32_t EndIndex);
  void writeSymbolAuxExceptionEntry(uint64_t EntryOffset, uint32_t FunctionSize,
                                    uint32_t EndIndex);
  void writeSymbolEntry(StringRef SymbolName, uint64_t Value,
                        int16_t SectionNumber, uint16_t SymbolType,
                        uint8_t StorageClass, uint8_t NumberOfAuxEntries = 1);
  void writeRelocations();
  void writeRelocation(XCOFFRelocation Reloc, const XCOFFSection &Section);

  // Called after all the csects and symbols have been processed by
  // `executePostLayoutBinding`, this function handles building up the majority
  // of the structures in the object file representation. Namely:
  // *) Calculates physical/virtual addresses, raw-pointer offsets, and section
  //    sizes.
  // *) Assigns symbol table indices.
  // *) Builds up the section header table by adding any non-empty sections to
  //    `Sections`.
  void assignAddressesAndIndices(MCAssembler &Asm);
  // Called after relocations are recorded.
  void finalizeSectionInfo();
  void finalizeRelocationInfo(SectionEntry *Sec, uint64_t RelCount);
  void calcOffsetToRelocations(SectionEntry *Sec, uint64_t &RawPointer);

  bool hasExceptionSection() {
    return !ExceptionSection.ExceptionTable.empty();
  }
  unsigned getExceptionSectionSize();
  unsigned getExceptionOffset(const MCSymbol *Symbol);

  size_t auxiliaryHeaderSize() const {
    // 64-bit object files have no auxiliary header.
    return HasVisibility && !is64Bit() ? XCOFF::AuxFileHeaderSizeShort : 0;
  }

public:
  XCOFFObjectWriter(std::unique_ptr<MCXCOFFObjectTargetWriter> MOTW,
                    raw_pwrite_stream &OS);

  void writeWord(uint64_t Word) {
    is64Bit() ? W.write<uint64_t>(Word) : W.write<uint32_t>(Word);
  }

  void addExceptionEntry(const MCSymbol *Symbol, const MCSymbol *Trap,
                         unsigned LanguageCode, unsigned ReasonCode,
                         unsigned FunctionSize, bool hasDebug);
  void addCInfoSymEntry(StringRef Name, StringRef Metadata);
};

XCOFFObjectWriter::XCOFFObjectWriter(
    std::unique_ptr<MCXCOFFObjectTargetWriter> MOTW, raw_pwrite_stream &OS)
    : W(OS, llvm::endianness::big), TargetObjectWriter(std::move(MOTW)),
      Strings(StringTableBuilder::XCOFF),
      Text(".text", XCOFF::STYP_TEXT, /* IsVirtual */ false,
           CsectGroups{&ProgramCodeCsects, &ReadOnlyCsects}),
      Data(".data", XCOFF::STYP_DATA, /* IsVirtual */ false,
           CsectGroups{&DataCsects, &FuncDSCsects, &TOCCsects}),
      BSS(".bss", XCOFF::STYP_BSS, /* IsVirtual */ true,
          CsectGroups{&BSSCsects}),
      TData(".tdata", XCOFF::STYP_TDATA, /* IsVirtual */ false,
            CsectGroups{&TDataCsects}),
      TBSS(".tbss", XCOFF::STYP_TBSS, /* IsVirtual */ true,
           CsectGroups{&TBSSCsects}),
      ExceptionSection(".except", XCOFF::STYP_EXCEPT),
      CInfoSymSection(".info", XCOFF::STYP_INFO) {}

void XCOFFObjectWriter::reset() {
  // Clear the mappings we created.
  SymbolIndexMap.clear();
  SectionMap.clear();

  UndefinedCsects.clear();
  // Reset any sections we have written to, and empty the section header table.
  for (auto *Sec : Sections)
    Sec->reset();
  for (auto &DwarfSec : DwarfSections)
    DwarfSec.reset();
  for (auto &OverflowSec : OverflowSections)
    OverflowSec.reset();
  ExceptionSection.reset();
  CInfoSymSection.reset();

  // Reset states in XCOFFObjectWriter.
  SymbolTableEntryCount = 0;
  SymbolTableOffset = 0;
  SectionCount = 0;
  PaddingsBeforeDwarf = 0;
  Strings.clear();

  MCObjectWriter::reset();
}

CsectGroup &XCOFFObjectWriter::getCsectGroup(const MCSectionXCOFF *MCSec) {
  switch (MCSec->getMappingClass()) {
  case XCOFF::XMC_PR:
    assert(XCOFF::XTY_SD == MCSec->getCSectType() &&
           "Only an initialized csect can contain program code.");
    return ProgramCodeCsects;
  case XCOFF::XMC_RO:
    assert(XCOFF::XTY_SD == MCSec->getCSectType() &&
           "Only an initialized csect can contain read only data.");
    return ReadOnlyCsects;
  case XCOFF::XMC_RW:
    if (XCOFF::XTY_CM == MCSec->getCSectType())
      return BSSCsects;

    if (XCOFF::XTY_SD == MCSec->getCSectType())
      return DataCsects;

    report_fatal_error("Unhandled mapping of read-write csect to section.");
  case XCOFF::XMC_DS:
    return FuncDSCsects;
  case XCOFF::XMC_BS:
    assert(XCOFF::XTY_CM == MCSec->getCSectType() &&
           "Mapping invalid csect. CSECT with bss storage class must be "
           "common type.");
    return BSSCsects;
  case XCOFF::XMC_TL:
    assert(XCOFF::XTY_SD == MCSec->getCSectType() &&
           "Mapping invalid csect. CSECT with tdata storage class must be "
           "an initialized csect.");
    return TDataCsects;
  case XCOFF::XMC_UL:
    assert(XCOFF::XTY_CM == MCSec->getCSectType() &&
           "Mapping invalid csect. CSECT with tbss storage class must be "
           "an uninitialized csect.");
    return TBSSCsects;
  case XCOFF::XMC_TC0:
    assert(XCOFF::XTY_SD == MCSec->getCSectType() &&
           "Only an initialized csect can contain TOC-base.");
    assert(TOCCsects.empty() &&
           "We should have only one TOC-base, and it should be the first csect "
           "in this CsectGroup.");
    return TOCCsects;
  case XCOFF::XMC_TC:
  case XCOFF::XMC_TE:
    assert(XCOFF::XTY_SD == MCSec->getCSectType() &&
           "A TOC symbol must be an initialized csect.");
    assert(!TOCCsects.empty() &&
           "We should at least have a TOC-base in this CsectGroup.");
    return TOCCsects;
  case XCOFF::XMC_TD:
    assert((XCOFF::XTY_SD == MCSec->getCSectType() ||
            XCOFF::XTY_CM == MCSec->getCSectType()) &&
           "Symbol type incompatible with toc-data.");
    assert(!TOCCsects.empty() &&
           "We should at least have a TOC-base in this CsectGroup.");
    return TOCCsects;
  default:
    report_fatal_error("Unhandled mapping of csect to section.");
  }
}

static MCSectionXCOFF *getContainingCsect(const MCSymbolXCOFF *XSym) {
  if (XSym->isDefined())
    return cast<MCSectionXCOFF>(XSym->getFragment()->getParent());
  return XSym->getRepresentedCsect();
}

void XCOFFObjectWriter::executePostLayoutBinding(MCAssembler &Asm) {
  for (const auto &S : Asm) {
    const auto *MCSec = cast<const MCSectionXCOFF>(&S);
    assert(!SectionMap.contains(MCSec) && "Cannot add a section twice.");

    // If the name does not fit in the storage provided in the symbol table
    // entry, add it to the string table.
    if (nameShouldBeInStringTable(MCSec->getSymbolTableName()))
      Strings.add(MCSec->getSymbolTableName());
    if (MCSec->isCsect()) {
      // A new control section. Its CsectSectionEntry should already be staticly
      // generated as Text/Data/BSS/TDATA/TBSS. Add this section to the group of
      // the CsectSectionEntry.
      assert(XCOFF::XTY_ER != MCSec->getCSectType() &&
             "An undefined csect should not get registered.");
      CsectGroup &Group = getCsectGroup(MCSec);
      Group.emplace_back(MCSec);
      SectionMap[MCSec] = &Group.back();
    } else if (MCSec->isDwarfSect()) {
      // A new DwarfSectionEntry.
      std::unique_ptr<XCOFFSection> DwarfSec =
          std::make_unique<XCOFFSection>(MCSec);
      SectionMap[MCSec] = DwarfSec.get();

      DwarfSectionEntry SecEntry(MCSec->getName(),
                                 *MCSec->getDwarfSubtypeFlags(),
                                 std::move(DwarfSec));
      DwarfSections.push_back(std::move(SecEntry));
    } else
      llvm_unreachable("unsupport section type!");
  }

  for (const MCSymbol &S : Asm.symbols()) {
    // Nothing to do for temporary symbols.
    if (S.isTemporary())
      continue;

    const MCSymbolXCOFF *XSym = cast<MCSymbolXCOFF>(&S);
    const MCSectionXCOFF *ContainingCsect = getContainingCsect(XSym);

    if (ContainingCsect->isDwarfSect())
      continue;

    if (XSym->getVisibilityType() != XCOFF::SYM_V_UNSPECIFIED)
      HasVisibility = true;

    if (ContainingCsect->getCSectType() == XCOFF::XTY_ER) {
      // Handle undefined symbol.
      UndefinedCsects.emplace_back(ContainingCsect);
      SectionMap[ContainingCsect] = &UndefinedCsects.back();
      if (nameShouldBeInStringTable(ContainingCsect->getSymbolTableName()))
        Strings.add(ContainingCsect->getSymbolTableName());
      continue;
    }

    // If the symbol is the csect itself, we don't need to put the symbol
    // into csect's Syms.
    if (XSym == ContainingCsect->getQualNameSymbol())
      continue;

    // Only put a label into the symbol table when it is an external label.
    if (!XSym->isExternal())
      continue;

    assert(SectionMap.contains(ContainingCsect) &&
           "Expected containing csect to exist in map");
    XCOFFSection *Csect = SectionMap[ContainingCsect];
    // Lookup the containing csect and add the symbol to it.
    assert(Csect->MCSec->isCsect() && "only csect is supported now!");
    Csect->Syms.emplace_back(XSym);

    // If the name does not fit in the storage provided in the symbol table
    // entry, add it to the string table.
    if (nameShouldBeInStringTable(XSym->getSymbolTableName()))
      Strings.add(XSym->getSymbolTableName());
  }

  std::unique_ptr<CInfoSymInfo> &CISI = CInfoSymSection.Entry;
  if (CISI && nameShouldBeInStringTable(CISI->Name))
    Strings.add(CISI->Name);

  // Emit ".file" as the source file name when there is no file name.
  if (FileNames.empty())
    FileNames.emplace_back(".file", 0);
  for (const std::pair<std::string, size_t> &F : FileNames) {
    if (auxFileSymNameShouldBeInStringTable(F.first))
      Strings.add(F.first);
  }

  // Always add ".file" to the symbol table. The actual file name will be in
  // the AUX_FILE auxiliary entry.
  if (nameShouldBeInStringTable(".file"))
    Strings.add(".file");
  StringRef Vers = CompilerVersion;
  if (auxFileSymNameShouldBeInStringTable(Vers))
    Strings.add(Vers);

  Strings.finalize();
  assignAddressesAndIndices(Asm);
}

void XCOFFObjectWriter::recordRelocation(MCAssembler &Asm,
                                         const MCFragment *Fragment,
                                         const MCFixup &Fixup, MCValue Target,
                                         uint64_t &FixedValue) {
  auto getIndex = [this](const MCSymbol *Sym,
                         const MCSectionXCOFF *ContainingCsect) {
    // If we could not find the symbol directly in SymbolIndexMap, this symbol
    // could either be a temporary symbol or an undefined symbol. In this case,
    // we would need to have the relocation reference its csect instead.
    return SymbolIndexMap.contains(Sym)
               ? SymbolIndexMap[Sym]
               : SymbolIndexMap[ContainingCsect->getQualNameSymbol()];
  };

  auto getVirtualAddress =
      [this, &Asm](const MCSymbol *Sym,
                   const MCSectionXCOFF *ContainingSect) -> uint64_t {
    // A DWARF section.
    if (ContainingSect->isDwarfSect())
      return Asm.getSymbolOffset(*Sym);

    // A csect.
    if (!Sym->isDefined())
      return SectionMap[ContainingSect]->Address;

    // A label.
    assert(Sym->isDefined() && "not a valid object that has address!");
    return SectionMap[ContainingSect]->Address + Asm.getSymbolOffset(*Sym);
  };

  const MCSymbol *const SymA = &Target.getSymA()->getSymbol();

  MCAsmBackend &Backend = Asm.getBackend();
  bool IsPCRel = Backend.getFixupKindInfo(Fixup.getKind()).Flags &
                 MCFixupKindInfo::FKF_IsPCRel;

  uint8_t Type;
  uint8_t SignAndSize;
  std::tie(Type, SignAndSize) =
      TargetObjectWriter->getRelocTypeAndSignSize(Target, Fixup, IsPCRel);

  const MCSectionXCOFF *SymASec = getContainingCsect(cast<MCSymbolXCOFF>(SymA));
  assert(SectionMap.contains(SymASec) &&
         "Expected containing csect to exist in map.");

  assert((Fixup.getOffset() <=
          MaxRawDataSize - Asm.getFragmentOffset(*Fragment)) &&
         "Fragment offset + fixup offset is overflowed.");
  uint32_t FixupOffsetInCsect =
      Asm.getFragmentOffset(*Fragment) + Fixup.getOffset();

  const uint32_t Index = getIndex(SymA, SymASec);
  if (Type == XCOFF::RelocationType::R_POS ||
      Type == XCOFF::RelocationType::R_TLS ||
      Type == XCOFF::RelocationType::R_TLS_LE ||
      Type == XCOFF::RelocationType::R_TLS_IE ||
      Type == XCOFF::RelocationType::R_TLS_LD)
    // The FixedValue should be symbol's virtual address in this object file
    // plus any constant value that we might get.
    FixedValue = getVirtualAddress(SymA, SymASec) + Target.getConstant();
  else if (Type == XCOFF::RelocationType::R_TLSM)
    // The FixedValue should always be zero since the region handle is only
    // known at load time.
    FixedValue = 0;
  else if (Type == XCOFF::RelocationType::R_TOC ||
           Type == XCOFF::RelocationType::R_TOCL) {
    // For non toc-data external symbols, R_TOC type relocation will relocate to
    // data symbols that have XCOFF::XTY_SD type csect. For toc-data external
    // symbols, R_TOC type relocation will relocate to data symbols that have
    // XCOFF_ER type csect. For XCOFF_ER kind symbols, there will be no TOC
    // entry for them, so the FixedValue should always be 0.
    if (SymASec->getCSectType() == XCOFF::XTY_ER) {
      FixedValue = 0;
    } else {
      // The FixedValue should be the TOC entry offset from the TOC-base plus
      // any constant offset value.
      int64_t TOCEntryOffset = SectionMap[SymASec]->Address -
                               TOCCsects.front().Address + Target.getConstant();
      // For small code model, if the TOCEntryOffset overflows the 16-bit value,
      // we truncate it back down to 16 bits. The linker will be able to insert
      // fix-up code when needed.
      // For non toc-data symbols, we already did the truncation in
      // PPCAsmPrinter.cpp through setting Target.getConstant() in the
      // expression above by calling getTOCEntryLoadingExprForXCOFF for the
      // various TOC PseudoOps.
      // For toc-data symbols, we were not able to calculate the offset from
      // the TOC in PPCAsmPrinter.cpp since the TOC has not been finalized at
      // that point, so we are adjusting it here though
      // llvm::SignExtend64<16>(TOCEntryOffset);
      // TODO: Since the time that the handling for offsets over 16-bits was
      // added in PPCAsmPrinter.cpp using getTOCEntryLoadingExprForXCOFF, the
      // system assembler and linker have been updated to be able to handle the
      // overflowing offsets, so we no longer need to keep
      // getTOCEntryLoadingExprForXCOFF.
      if (Type == XCOFF::RelocationType::R_TOC && !isInt<16>(TOCEntryOffset))
        TOCEntryOffset = llvm::SignExtend64<16>(TOCEntryOffset);

      FixedValue = TOCEntryOffset;
    }
  } else if (Type == XCOFF::RelocationType::R_RBR) {
    MCSectionXCOFF *ParentSec = cast<MCSectionXCOFF>(Fragment->getParent());
    assert((SymASec->getMappingClass() == XCOFF::XMC_PR &&
            ParentSec->getMappingClass() == XCOFF::XMC_PR) &&
           "Only XMC_PR csect may have the R_RBR relocation.");

    // The address of the branch instruction should be the sum of section
    // address, fragment offset and Fixup offset.
    uint64_t BRInstrAddress =
        SectionMap[ParentSec]->Address + FixupOffsetInCsect;
    // The FixedValue should be the difference between symbol's virtual address
    // and BR instr address plus any constant value.
    FixedValue = getVirtualAddress(SymA, SymASec) - BRInstrAddress +
                 Target.getConstant();
  } else if (Type == XCOFF::RelocationType::R_REF) {
    // The FixedValue and FixupOffsetInCsect should always be 0 since it
    // specifies a nonrelocating reference.
    FixedValue = 0;
    FixupOffsetInCsect = 0;
  }

  XCOFFRelocation Reloc = {Index, FixupOffsetInCsect, SignAndSize, Type};
  MCSectionXCOFF *RelocationSec = cast<MCSectionXCOFF>(Fragment->getParent());
  assert(SectionMap.contains(RelocationSec) &&
         "Expected containing csect to exist in map.");
  SectionMap[RelocationSec]->Relocations.push_back(Reloc);

  if (!Target.getSymB())
    return;

  const MCSymbol *const SymB = &Target.getSymB()->getSymbol();
  if (SymA == SymB)
    report_fatal_error("relocation for opposite term is not yet supported");

  const MCSectionXCOFF *SymBSec = getContainingCsect(cast<MCSymbolXCOFF>(SymB));
  assert(SectionMap.contains(SymBSec) &&
         "Expected containing csect to exist in map.");
  if (SymASec == SymBSec)
    report_fatal_error(
        "relocation for paired relocatable term is not yet supported");

  assert(Type == XCOFF::RelocationType::R_POS &&
         "SymA must be R_POS here if it's not opposite term or paired "
         "relocatable term.");
  const uint32_t IndexB = getIndex(SymB, SymBSec);
  // SymB must be R_NEG here, given the general form of Target(MCValue) is
  // "SymbolA - SymbolB + imm64".
  const uint8_t TypeB = XCOFF::RelocationType::R_NEG;
  XCOFFRelocation RelocB = {IndexB, FixupOffsetInCsect, SignAndSize, TypeB};
  SectionMap[RelocationSec]->Relocations.push_back(RelocB);
  // We already folded "SymbolA + imm64" above when Type is R_POS for SymbolA,
  // now we just need to fold "- SymbolB" here.
  FixedValue -= getVirtualAddress(SymB, SymBSec);
}

void XCOFFObjectWriter::writeSections(const MCAssembler &Asm) {
  uint64_t CurrentAddressLocation = 0;
  for (const auto *Section : Sections)
    writeSectionForControlSectionEntry(Asm, *Section, CurrentAddressLocation);
  for (const auto &DwarfSection : DwarfSections)
    writeSectionForDwarfSectionEntry(Asm, DwarfSection, CurrentAddressLocation);
  writeSectionForExceptionSectionEntry(Asm, ExceptionSection,
                                       CurrentAddressLocation);
  writeSectionForCInfoSymSectionEntry(Asm, CInfoSymSection,
                                      CurrentAddressLocation);
}

uint64_t XCOFFObjectWriter::writeObject(MCAssembler &Asm) {
  // We always emit a timestamp of 0 for reproducibility, so ensure incremental
  // linking is not enabled, in case, like with Windows COFF, such a timestamp
  // is incompatible with incremental linking of XCOFF.

  finalizeSectionInfo();
  uint64_t StartOffset = W.OS.tell();

  writeFileHeader();
  writeAuxFileHeader();
  writeSectionHeaderTable();
  writeSections(Asm);
  writeRelocations();
  writeSymbolTable(Asm);
  // Write the string table.
  Strings.write(W.OS);

  return W.OS.tell() - StartOffset;
}

bool XCOFFObjectWriter::nameShouldBeInStringTable(const StringRef &SymbolName) {
  return SymbolName.size() > XCOFF::NameSize || is64Bit();
}

void XCOFFObjectWriter::writeSymbolName(const StringRef &SymbolName) {
  // Magic, Offset or SymbolName.
  if (nameShouldBeInStringTable(SymbolName)) {
    W.write<int32_t>(0);
    W.write<uint32_t>(Strings.getOffset(SymbolName));
  } else {
    char Name[XCOFF::NameSize + 1];
    std::strncpy(Name, SymbolName.data(), XCOFF::NameSize);
    ArrayRef<char> NameRef(Name, XCOFF::NameSize);
    W.write(NameRef);
  }
}

void XCOFFObjectWriter::writeSymbolEntry(StringRef SymbolName, uint64_t Value,
                                         int16_t SectionNumber,
                                         uint16_t SymbolType,
                                         uint8_t StorageClass,
                                         uint8_t NumberOfAuxEntries) {
  if (is64Bit()) {
    W.write<uint64_t>(Value);
    W.write<uint32_t>(Strings.getOffset(SymbolName));
  } else {
    writeSymbolName(SymbolName);
    W.write<uint32_t>(Value);
  }
  W.write<int16_t>(SectionNumber);
  W.write<uint16_t>(SymbolType);
  W.write<uint8_t>(StorageClass);
  W.write<uint8_t>(NumberOfAuxEntries);
}

void XCOFFObjectWriter::writeSymbolAuxCsectEntry(uint64_t SectionOrLength,
                                                 uint8_t SymbolAlignmentAndType,
                                                 uint8_t StorageMappingClass) {
  W.write<uint32_t>(is64Bit() ? Lo_32(SectionOrLength) : SectionOrLength);
  W.write<uint32_t>(0); // ParameterHashIndex
  W.write<uint16_t>(0); // TypeChkSectNum
  W.write<uint8_t>(SymbolAlignmentAndType);
  W.write<uint8_t>(StorageMappingClass);
  if (is64Bit()) {
    W.write<uint32_t>(Hi_32(SectionOrLength));
    W.OS.write_zeros(1); // Reserved
    W.write<uint8_t>(XCOFF::AUX_CSECT);
  } else {
    W.write<uint32_t>(0); // StabInfoIndex
    W.write<uint16_t>(0); // StabSectNum
  }
}

bool XCOFFObjectWriter::auxFileSymNameShouldBeInStringTable(
    const StringRef &SymbolName) {
  return SymbolName.size() > XCOFF::AuxFileEntNameSize;
}

void XCOFFObjectWriter::writeAuxFileSymName(const StringRef &SymbolName) {
  // Magic, Offset or SymbolName.
  if (auxFileSymNameShouldBeInStringTable(SymbolName)) {
    W.write<int32_t>(0);
    W.write<uint32_t>(Strings.getOffset(SymbolName));
    W.OS.write_zeros(XCOFF::FileNamePadSize);
  } else {
    char Name[XCOFF::AuxFileEntNameSize + 1];
    std::strncpy(Name, SymbolName.data(), XCOFF::AuxFileEntNameSize);
    ArrayRef<char> NameRef(Name, XCOFF::AuxFileEntNameSize);
    W.write(NameRef);
  }
}

void XCOFFObjectWriter::writeSymbolAuxFileEntry(StringRef &Name,
                                                uint8_t ftype) {
  writeAuxFileSymName(Name);
  W.write<uint8_t>(ftype);
  W.OS.write_zeros(2);
  if (is64Bit())
    W.write<uint8_t>(XCOFF::AUX_FILE);
  else
    W.OS.write_zeros(1);
}

void XCOFFObjectWriter::writeSymbolAuxDwarfEntry(
    uint64_t LengthOfSectionPortion, uint64_t NumberOfRelocEnt) {
  writeWord(LengthOfSectionPortion);
  if (!is64Bit())
    W.OS.write_zeros(4); // Reserved
  writeWord(NumberOfRelocEnt);
  if (is64Bit()) {
    W.OS.write_zeros(1); // Reserved
    W.write<uint8_t>(XCOFF::AUX_SECT);
  } else {
    W.OS.write_zeros(6); // Reserved
  }
}

void XCOFFObjectWriter::writeSymbolEntryForCsectMemberLabel(
    const Symbol &SymbolRef, const XCOFFSection &CSectionRef,
    int16_t SectionIndex, uint64_t SymbolOffset) {
  assert(SymbolOffset <= MaxRawDataSize - CSectionRef.Address &&
         "Symbol address overflowed.");

  auto Entry = ExceptionSection.ExceptionTable.find(SymbolRef.MCSym->getName());
  if (Entry != ExceptionSection.ExceptionTable.end()) {
    writeSymbolEntry(SymbolRef.getSymbolTableName(),
                     CSectionRef.Address + SymbolOffset, SectionIndex,
                     // In the old version of the 32-bit XCOFF interpretation,
                     // symbols may require bit 10 (0x0020) to be set if the
                     // symbol is a function, otherwise the bit should be 0.
                     is64Bit() ? SymbolRef.getVisibilityType()
                               : SymbolRef.getVisibilityType() | 0x0020,
                     SymbolRef.getStorageClass(),
                     (is64Bit() && ExceptionSection.isDebugEnabled) ? 3 : 2);
    if (is64Bit() && ExceptionSection.isDebugEnabled) {
      // On 64 bit with debugging enabled, we have a csect, exception, and
      // function auxilliary entries, so we must increment symbol index by 4.
      writeSymbolAuxExceptionEntry(
          ExceptionSection.FileOffsetToData +
              getExceptionOffset(Entry->second.FunctionSymbol),
          Entry->second.FunctionSize,
          SymbolIndexMap[Entry->second.FunctionSymbol] + 4);
    }
    // For exception section entries, csect and function auxilliary entries
    // must exist. On 64-bit there is also an exception auxilliary entry.
    writeSymbolAuxFunctionEntry(
        ExceptionSection.FileOffsetToData +
            getExceptionOffset(Entry->second.FunctionSymbol),
        Entry->second.FunctionSize, 0,
        (is64Bit() && ExceptionSection.isDebugEnabled)
            ? SymbolIndexMap[Entry->second.FunctionSymbol] + 4
            : SymbolIndexMap[Entry->second.FunctionSymbol] + 3);
  } else {
    writeSymbolEntry(SymbolRef.getSymbolTableName(),
                     CSectionRef.Address + SymbolOffset, SectionIndex,
                     SymbolRef.getVisibilityType(),
                     SymbolRef.getStorageClass());
  }
  writeSymbolAuxCsectEntry(CSectionRef.SymbolTableIndex, XCOFF::XTY_LD,
                           CSectionRef.MCSec->getMappingClass());
}

void XCOFFObjectWriter::writeSymbolEntryForDwarfSection(
    const XCOFFSection &DwarfSectionRef, int16_t SectionIndex) {
  assert(DwarfSectionRef.MCSec->isDwarfSect() && "Not a DWARF section!");

  writeSymbolEntry(DwarfSectionRef.getSymbolTableName(), /*Value=*/0,
                   SectionIndex, /*SymbolType=*/0, XCOFF::C_DWARF);

  writeSymbolAuxDwarfEntry(DwarfSectionRef.Size);
}

void XCOFFObjectWriter::writeSymbolEntryForControlSection(
    const XCOFFSection &CSectionRef, int16_t SectionIndex,
    XCOFF::StorageClass StorageClass) {
  writeSymbolEntry(CSectionRef.getSymbolTableName(), CSectionRef.Address,
                   SectionIndex, CSectionRef.getVisibilityType(), StorageClass);

  writeSymbolAuxCsectEntry(CSectionRef.Size, getEncodedType(CSectionRef.MCSec),
                           CSectionRef.MCSec->getMappingClass());
}

void XCOFFObjectWriter::writeSymbolAuxFunctionEntry(uint32_t EntryOffset,
                                                    uint32_t FunctionSize,
                                                    uint64_t LineNumberPointer,
                                                    uint32_t EndIndex) {
  if (is64Bit())
    writeWord(LineNumberPointer);
  else
    W.write<uint32_t>(EntryOffset);
  W.write<uint32_t>(FunctionSize);
  if (!is64Bit())
    writeWord(LineNumberPointer);
  W.write<uint32_t>(EndIndex);
  if (is64Bit()) {
    W.OS.write_zeros(1);
    W.write<uint8_t>(XCOFF::AUX_FCN);
  } else {
    W.OS.write_zeros(2);
  }
}

void XCOFFObjectWriter::writeSymbolAuxExceptionEntry(uint64_t EntryOffset,
                                                     uint32_t FunctionSize,
                                                     uint32_t EndIndex) {
  assert(is64Bit() && "Exception auxilliary entries are 64-bit only.");
  W.write<uint64_t>(EntryOffset);
  W.write<uint32_t>(FunctionSize);
  W.write<uint32_t>(EndIndex);
  W.OS.write_zeros(1); // Pad (unused)
  W.write<uint8_t>(XCOFF::AUX_EXCEPT);
}

void XCOFFObjectWriter::writeFileHeader() {
  W.write<uint16_t>(is64Bit() ? XCOFF::XCOFF64 : XCOFF::XCOFF32);
  W.write<uint16_t>(SectionCount);
  W.write<int32_t>(0); // TimeStamp
  writeWord(SymbolTableOffset);
  if (is64Bit()) {
    W.write<uint16_t>(auxiliaryHeaderSize());
    W.write<uint16_t>(0); // Flags
    W.write<int32_t>(SymbolTableEntryCount);
  } else {
    W.write<int32_t>(SymbolTableEntryCount);
    W.write<uint16_t>(auxiliaryHeaderSize());
    W.write<uint16_t>(0); // Flags
  }
}

void XCOFFObjectWriter::writeAuxFileHeader() {
  if (!auxiliaryHeaderSize())
    return;
  W.write<uint16_t>(0); // Magic
  W.write<uint16_t>(
      XCOFF::NEW_XCOFF_INTERPRET); // Version. The new interpretation of the
                                   // n_type field in the symbol table entry is
                                   // used in XCOFF32.
  W.write<uint32_t>(Sections[0]->Size);    // TextSize
  W.write<uint32_t>(Sections[1]->Size);    // InitDataSize
  W.write<uint32_t>(Sections[2]->Size);    // BssDataSize
  W.write<uint32_t>(0);                    // EntryPointAddr
  W.write<uint32_t>(Sections[0]->Address); // TextStartAddr
  W.write<uint32_t>(Sections[1]->Address); // DataStartAddr
}

void XCOFFObjectWriter::writeSectionHeader(const SectionEntry *Sec) {
  bool IsDwarf = (Sec->Flags & XCOFF::STYP_DWARF) != 0;
  bool IsOvrflo = (Sec->Flags & XCOFF::STYP_OVRFLO) != 0;
  // Nothing to write for this Section.
  if (Sec->Index == SectionEntry::UninitializedIndex)
    return;

  // Write Name.
  ArrayRef<char> NameRef(Sec->Name, XCOFF::NameSize);
  W.write(NameRef);

  // Write the Physical Address and Virtual Address.
  // We use 0 for DWARF sections' Physical and Virtual Addresses.
  writeWord(IsDwarf ? 0 : Sec->Address);
  // Since line number is not supported, we set it to 0 for overflow sections.
  writeWord((IsDwarf || IsOvrflo) ? 0 : Sec->Address);

  writeWord(Sec->Size);
  writeWord(Sec->FileOffsetToData);
  writeWord(Sec->FileOffsetToRelocations);
  writeWord(0); // FileOffsetToLineNumberInfo. Not supported yet.

  if (is64Bit()) {
    W.write<uint32_t>(Sec->RelocationCount);
    W.write<uint32_t>(0); // NumberOfLineNumbers. Not supported yet.
    W.write<int32_t>(Sec->Flags);
    W.OS.write_zeros(4);
  } else {
    // For the overflow section header, s_nreloc provides a reference to the
    // primary section header and s_nlnno must have the same value.
    // For common section headers, if either of s_nreloc or s_nlnno are set to
    // 65535, the other one must also be set to 65535.
    W.write<uint16_t>(Sec->RelocationCount);
    W.write<uint16_t>((IsOvrflo || Sec->RelocationCount == XCOFF::RelocOverflow)
                          ? Sec->RelocationCount
                          : 0); // NumberOfLineNumbers. Not supported yet.
    W.write<int32_t>(Sec->Flags);
  }
}

void XCOFFObjectWriter::writeSectionHeaderTable() {
  for (const auto *CsectSec : Sections)
    writeSectionHeader(CsectSec);
  for (const auto &DwarfSec : DwarfSections)
    writeSectionHeader(&DwarfSec);
  for (const auto &OverflowSec : OverflowSections)
    writeSectionHeader(&OverflowSec);
  if (hasExceptionSection())
    writeSectionHeader(&ExceptionSection);
  if (CInfoSymSection.Entry)
    writeSectionHeader(&CInfoSymSection);
}

void XCOFFObjectWriter::writeRelocation(XCOFFRelocation Reloc,
                                        const XCOFFSection &Section) {
  if (Section.MCSec->isCsect())
    writeWord(Section.Address + Reloc.FixupOffsetInCsect);
  else {
    // DWARF sections' address is set to 0.
    assert(Section.MCSec->isDwarfSect() && "unsupport section type!");
    writeWord(Reloc.FixupOffsetInCsect);
  }
  W.write<uint32_t>(Reloc.SymbolTableIndex);
  W.write<uint8_t>(Reloc.SignAndSize);
  W.write<uint8_t>(Reloc.Type);
}

void XCOFFObjectWriter::writeRelocations() {
  for (const auto *Section : Sections) {
    if (Section->Index == SectionEntry::UninitializedIndex)
      // Nothing to write for this Section.
      continue;

    for (const auto *Group : Section->Groups) {
      if (Group->empty())
        continue;

      for (const auto &Csect : *Group) {
        for (const auto Reloc : Csect.Relocations)
          writeRelocation(Reloc, Csect);
      }
    }
  }

  for (const auto &DwarfSection : DwarfSections)
    for (const auto &Reloc : DwarfSection.DwarfSect->Relocations)
      writeRelocation(Reloc, *DwarfSection.DwarfSect);
}

void XCOFFObjectWriter::writeSymbolTable(MCAssembler &Asm) {
  // Write C_FILE symbols.
  StringRef Vers = CompilerVersion;

  for (const std::pair<std::string, size_t> &F : FileNames) {
    // The n_name of a C_FILE symbol is the source file's name when no auxiliary
    // entries are present.
    StringRef FileName = F.first;

    // For C_FILE symbols, the Source Language ID overlays the high-order byte
    // of the SymbolType field, and the CPU Version ID is defined as the
    // low-order byte.
    // AIX's system assembler determines the source language ID based on the
    // source file's name suffix, and the behavior here is consistent with it.
    uint8_t LangID;
    if (FileName.ends_with(".c"))
      LangID = XCOFF::TB_C;
    else if (FileName.ends_with_insensitive(".f") ||
             FileName.ends_with_insensitive(".f77") ||
             FileName.ends_with_insensitive(".f90") ||
             FileName.ends_with_insensitive(".f95") ||
             FileName.ends_with_insensitive(".f03") ||
             FileName.ends_with_insensitive(".f08"))
      LangID = XCOFF::TB_Fortran;
    else
      LangID = XCOFF::TB_CPLUSPLUS;
    uint8_t CpuID;
    if (is64Bit())
      CpuID = XCOFF::TCPU_PPC64;
    else
      CpuID = XCOFF::TCPU_COM;

    int NumberOfFileAuxEntries = 1;
    if (!Vers.empty())
      ++NumberOfFileAuxEntries;
    writeSymbolEntry(".file", /*Value=*/0, XCOFF::ReservedSectionNum::N_DEBUG,
                     /*SymbolType=*/(LangID << 8) | CpuID, XCOFF::C_FILE,
                     NumberOfFileAuxEntries);
    writeSymbolAuxFileEntry(FileName, XCOFF::XFT_FN);
    if (!Vers.empty())
      writeSymbolAuxFileEntry(Vers, XCOFF::XFT_CV);
  }

  if (CInfoSymSection.Entry)
    writeSymbolEntry(CInfoSymSection.Entry->Name, CInfoSymSection.Entry->Offset,
                     CInfoSymSection.Index,
                     /*SymbolType=*/0, XCOFF::C_INFO,
                     /*NumberOfAuxEntries=*/0);

  for (const auto &Csect : UndefinedCsects) {
    writeSymbolEntryForControlSection(Csect, XCOFF::ReservedSectionNum::N_UNDEF,
                                      Csect.MCSec->getStorageClass());
  }

  for (const auto *Section : Sections) {
    if (Section->Index == SectionEntry::UninitializedIndex)
      // Nothing to write for this Section.
      continue;

    for (const auto *Group : Section->Groups) {
      if (Group->empty())
        continue;

      const int16_t SectionIndex = Section->Index;
      for (const auto &Csect : *Group) {
        // Write out the control section first and then each symbol in it.
        writeSymbolEntryForControlSection(Csect, SectionIndex,
                                          Csect.MCSec->getStorageClass());

        for (const auto &Sym : Csect.Syms)
          writeSymbolEntryForCsectMemberLabel(
              Sym, Csect, SectionIndex, Asm.getSymbolOffset(*(Sym.MCSym)));
      }
    }
  }

  for (const auto &DwarfSection : DwarfSections)
    writeSymbolEntryForDwarfSection(*DwarfSection.DwarfSect,
                                    DwarfSection.Index);
}

void XCOFFObjectWriter::finalizeRelocationInfo(SectionEntry *Sec,
                                               uint64_t RelCount) {
  // Handles relocation field overflows in an XCOFF32 file. An XCOFF64 file
  // may not contain an overflow section header.
  if (!is64Bit() && (RelCount >= static_cast<uint32_t>(XCOFF::RelocOverflow))) {
    // Generate an overflow section header.
    SectionEntry SecEntry(".ovrflo", XCOFF::STYP_OVRFLO);

    // This field specifies the file section number of the section header that
    // overflowed.
    SecEntry.RelocationCount = Sec->Index;

    // This field specifies the number of relocation entries actually
    // required.
    SecEntry.Address = RelCount;
    SecEntry.Index = ++SectionCount;
    OverflowSections.push_back(std::move(SecEntry));

    // The field in the primary section header is always 65535
    // (XCOFF::RelocOverflow).
    Sec->RelocationCount = XCOFF::RelocOverflow;
  } else {
    Sec->RelocationCount = RelCount;
  }
}

void XCOFFObjectWriter::calcOffsetToRelocations(SectionEntry *Sec,
                                                uint64_t &RawPointer) {
  if (!Sec->RelocationCount)
    return;

  Sec->FileOffsetToRelocations = RawPointer;
  uint64_t RelocationSizeInSec = 0;
  if (!is64Bit() &&
      Sec->RelocationCount == static_cast<uint32_t>(XCOFF::RelocOverflow)) {
    // Find its corresponding overflow section.
    for (auto &OverflowSec : OverflowSections) {
      if (OverflowSec.RelocationCount == static_cast<uint32_t>(Sec->Index)) {
        RelocationSizeInSec =
            OverflowSec.Address * XCOFF::RelocationSerializationSize32;

        // This field must have the same values as in the corresponding
        // primary section header.
        OverflowSec.FileOffsetToRelocations = Sec->FileOffsetToRelocations;
      }
    }
    assert(RelocationSizeInSec && "Overflow section header doesn't exist.");
  } else {
    RelocationSizeInSec = Sec->RelocationCount *
                          (is64Bit() ? XCOFF::RelocationSerializationSize64
                                     : XCOFF::RelocationSerializationSize32);
  }

  RawPointer += RelocationSizeInSec;
  if (RawPointer > MaxRawDataSize)
    report_fatal_error("Relocation data overflowed this object file.");
}

void XCOFFObjectWriter::finalizeSectionInfo() {
  for (auto *Section : Sections) {
    if (Section->Index == SectionEntry::UninitializedIndex)
      // Nothing to record for this Section.
      continue;

    uint64_t RelCount = 0;
    for (const auto *Group : Section->Groups) {
      if (Group->empty())
        continue;

      for (auto &Csect : *Group)
        RelCount += Csect.Relocations.size();
    }
    finalizeRelocationInfo(Section, RelCount);
  }

  for (auto &DwarfSection : DwarfSections)
    finalizeRelocationInfo(&DwarfSection,
                           DwarfSection.DwarfSect->Relocations.size());

  // Calculate the RawPointer value for all headers.
  uint64_t RawPointer =
      (is64Bit() ? (XCOFF::FileHeaderSize64 +
                    SectionCount * XCOFF::SectionHeaderSize64)
                 : (XCOFF::FileHeaderSize32 +
                    SectionCount * XCOFF::SectionHeaderSize32)) +
      auxiliaryHeaderSize();

  // Calculate the file offset to the section data.
  for (auto *Sec : Sections) {
    if (Sec->Index == SectionEntry::UninitializedIndex || Sec->IsVirtual)
      continue;

    RawPointer = Sec->advanceFileOffset(MaxRawDataSize, RawPointer);
  }

  if (!DwarfSections.empty()) {
    RawPointer += PaddingsBeforeDwarf;
    for (auto &DwarfSection : DwarfSections) {
      RawPointer = DwarfSection.advanceFileOffset(MaxRawDataSize, RawPointer);
    }
  }

  if (hasExceptionSection())
    RawPointer = ExceptionSection.advanceFileOffset(MaxRawDataSize, RawPointer);

  if (CInfoSymSection.Entry)
    RawPointer = CInfoSymSection.advanceFileOffset(MaxRawDataSize, RawPointer);

  for (auto *Sec : Sections) {
    if (Sec->Index != SectionEntry::UninitializedIndex)
      calcOffsetToRelocations(Sec, RawPointer);
  }

  for (auto &DwarfSec : DwarfSections)
    calcOffsetToRelocations(&DwarfSec, RawPointer);

  // TODO Error check that the number of symbol table entries fits in 32-bits
  // signed ...
  if (SymbolTableEntryCount)
    SymbolTableOffset = RawPointer;
}

void XCOFFObjectWriter::addExceptionEntry(
    const MCSymbol *Symbol, const MCSymbol *Trap, unsigned LanguageCode,
    unsigned ReasonCode, unsigned FunctionSize, bool hasDebug) {
  // If a module had debug info, debugging is enabled and XCOFF emits the
  // exception auxilliary entry.
  if (hasDebug)
    ExceptionSection.isDebugEnabled = true;
  auto Entry = ExceptionSection.ExceptionTable.find(Symbol->getName());
  if (Entry != ExceptionSection.ExceptionTable.end()) {
    Entry->second.Entries.push_back(
        ExceptionTableEntry(Trap, LanguageCode, ReasonCode));
    return;
  }
  ExceptionInfo NewEntry;
  NewEntry.FunctionSymbol = Symbol;
  NewEntry.FunctionSize = FunctionSize;
  NewEntry.Entries.push_back(
      ExceptionTableEntry(Trap, LanguageCode, ReasonCode));
  ExceptionSection.ExceptionTable.insert(
      std::pair<const StringRef, ExceptionInfo>(Symbol->getName(), NewEntry));
}

unsigned XCOFFObjectWriter::getExceptionSectionSize() {
  unsigned EntryNum = 0;

  for (const auto &TableEntry : ExceptionSection.ExceptionTable)
    // The size() gets +1 to account for the initial entry containing the
    // symbol table index.
    EntryNum += TableEntry.second.Entries.size() + 1;

  return EntryNum * (is64Bit() ? XCOFF::ExceptionSectionEntrySize64
                               : XCOFF::ExceptionSectionEntrySize32);
}

unsigned XCOFFObjectWriter::getExceptionOffset(const MCSymbol *Symbol) {
  unsigned EntryNum = 0;
  for (const auto &TableEntry : ExceptionSection.ExceptionTable) {
    if (Symbol == TableEntry.second.FunctionSymbol)
      break;
    EntryNum += TableEntry.second.Entries.size() + 1;
  }
  return EntryNum * (is64Bit() ? XCOFF::ExceptionSectionEntrySize64
                               : XCOFF::ExceptionSectionEntrySize32);
}

void XCOFFObjectWriter::addCInfoSymEntry(StringRef Name, StringRef Metadata) {
  assert(!CInfoSymSection.Entry && "Multiple entries are not supported");
  CInfoSymSection.addEntry(
      std::make_unique<CInfoSymInfo>(Name.str(), Metadata.str()));
}

void XCOFFObjectWriter::assignAddressesAndIndices(MCAssembler &Asm) {
  // The symbol table starts with all the C_FILE symbols. Each C_FILE symbol
  // requires 1 or 2 auxiliary entries.
  uint32_t SymbolTableIndex =
      (2 + (CompilerVersion.empty() ? 0 : 1)) * FileNames.size();

  if (CInfoSymSection.Entry)
    SymbolTableIndex++;

  // Calculate indices for undefined symbols.
  for (auto &Csect : UndefinedCsects) {
    Csect.Size = 0;
    Csect.Address = 0;
    Csect.SymbolTableIndex = SymbolTableIndex;
    SymbolIndexMap[Csect.MCSec->getQualNameSymbol()] = Csect.SymbolTableIndex;
    // 1 main and 1 auxiliary symbol table entry for each contained symbol.
    SymbolTableIndex += 2;
  }

  // The address corrresponds to the address of sections and symbols in the
  // object file. We place the shared address 0 immediately after the
  // section header table.
  uint64_t Address = 0;
  // Section indices are 1-based in XCOFF.
  int32_t SectionIndex = 1;
  bool HasTDataSection = false;

  for (auto *Section : Sections) {
    const bool IsEmpty =
        llvm::all_of(Section->Groups,
                     [](const CsectGroup *Group) { return Group->empty(); });
    if (IsEmpty)
      continue;

    if (SectionIndex > MaxSectionIndex)
      report_fatal_error("Section index overflow!");
    Section->Index = SectionIndex++;
    SectionCount++;

    bool SectionAddressSet = false;
    // Reset the starting address to 0 for TData section.
    if (Section->Flags == XCOFF::STYP_TDATA) {
      Address = 0;
      HasTDataSection = true;
    }
    // Reset the starting address to 0 for TBSS section if the object file does
    // not contain TData Section.
    if ((Section->Flags == XCOFF::STYP_TBSS) && !HasTDataSection)
      Address = 0;

    for (auto *Group : Section->Groups) {
      if (Group->empty())
        continue;

      for (auto &Csect : *Group) {
        const MCSectionXCOFF *MCSec = Csect.MCSec;
        Csect.Address = alignTo(Address, MCSec->getAlign());
        Csect.Size = Asm.getSectionAddressSize(*MCSec);
        Address = Csect.Address + Csect.Size;
        Csect.SymbolTableIndex = SymbolTableIndex;
        SymbolIndexMap[MCSec->getQualNameSymbol()] = Csect.SymbolTableIndex;
        // 1 main and 1 auxiliary symbol table entry for the csect.
        SymbolTableIndex += 2;

        for (auto &Sym : Csect.Syms) {
          bool hasExceptEntry = false;
          auto Entry =
              ExceptionSection.ExceptionTable.find(Sym.MCSym->getName());
          if (Entry != ExceptionSection.ExceptionTable.end()) {
            hasExceptEntry = true;
            for (auto &TrapEntry : Entry->second.Entries) {
              TrapEntry.TrapAddress = Asm.getSymbolOffset(*(Sym.MCSym)) +
                                      TrapEntry.Trap->getOffset();
            }
          }
          Sym.SymbolTableIndex = SymbolTableIndex;
          SymbolIndexMap[Sym.MCSym] = Sym.SymbolTableIndex;
          // 1 main and 1 auxiliary symbol table entry for each contained
          // symbol. For symbols with exception section entries, a function
          // auxilliary entry is needed, and on 64-bit XCOFF with debugging
          // enabled, an additional exception auxilliary entry is needed.
          SymbolTableIndex += 2;
          if (hasExceptionSection() && hasExceptEntry) {
            if (is64Bit() && ExceptionSection.isDebugEnabled)
              SymbolTableIndex += 2;
            else
              SymbolTableIndex += 1;
          }
        }
      }

      if (!SectionAddressSet) {
        Section->Address = Group->front().Address;
        SectionAddressSet = true;
      }
    }

    // Make sure the address of the next section aligned to
    // DefaultSectionAlign.
    Address = alignTo(Address, DefaultSectionAlign);
    Section->Size = Address - Section->Address;
  }

  // Start to generate DWARF sections. Sections other than DWARF section use
  // DefaultSectionAlign as the default alignment, while DWARF sections have
  // their own alignments. If these two alignments are not the same, we need
  // some paddings here and record the paddings bytes for FileOffsetToData
  // calculation.
  if (!DwarfSections.empty())
    PaddingsBeforeDwarf =
        alignTo(Address,
                (*DwarfSections.begin()).DwarfSect->MCSec->getAlign()) -
        Address;

  DwarfSectionEntry *LastDwarfSection = nullptr;
  for (auto &DwarfSection : DwarfSections) {
    assert((SectionIndex <= MaxSectionIndex) && "Section index overflow!");

    XCOFFSection &DwarfSect = *DwarfSection.DwarfSect;
    const MCSectionXCOFF *MCSec = DwarfSect.MCSec;

    // Section index.
    DwarfSection.Index = SectionIndex++;
    SectionCount++;

    // Symbol index.
    DwarfSect.SymbolTableIndex = SymbolTableIndex;
    SymbolIndexMap[MCSec->getQualNameSymbol()] = DwarfSect.SymbolTableIndex;
    // 1 main and 1 auxiliary symbol table entry for the csect.
    SymbolTableIndex += 2;

    // Section address. Make it align to section alignment.
    // We use address 0 for DWARF sections' Physical and Virtual Addresses.
    // This address is used to tell where is the section in the final object.
    // See writeSectionForDwarfSectionEntry().
    DwarfSection.Address = DwarfSect.Address =
        alignTo(Address, MCSec->getAlign());

    // Section size.
    // For DWARF section, we must use the real size which may be not aligned.
    DwarfSection.Size = DwarfSect.Size = Asm.getSectionAddressSize(*MCSec);

    Address = DwarfSection.Address + DwarfSection.Size;

    if (LastDwarfSection)
      LastDwarfSection->MemorySize =
          DwarfSection.Address - LastDwarfSection->Address;
    LastDwarfSection = &DwarfSection;
  }
  if (LastDwarfSection) {
    // Make the final DWARF section address align to the default section
    // alignment for follow contents.
    Address = alignTo(LastDwarfSection->Address + LastDwarfSection->Size,
                      DefaultSectionAlign);
    LastDwarfSection->MemorySize = Address - LastDwarfSection->Address;
  }
  if (hasExceptionSection()) {
    ExceptionSection.Index = SectionIndex++;
    SectionCount++;
    ExceptionSection.Address = 0;
    ExceptionSection.Size = getExceptionSectionSize();
    Address += ExceptionSection.Size;
    Address = alignTo(Address, DefaultSectionAlign);
  }

  if (CInfoSymSection.Entry) {
    CInfoSymSection.Index = SectionIndex++;
    SectionCount++;
    CInfoSymSection.Address = 0;
    Address += CInfoSymSection.Size;
    Address = alignTo(Address, DefaultSectionAlign);
  }

  SymbolTableEntryCount = SymbolTableIndex;
}

void XCOFFObjectWriter::writeSectionForControlSectionEntry(
    const MCAssembler &Asm, const CsectSectionEntry &CsectEntry,
    uint64_t &CurrentAddressLocation) {
  // Nothing to write for this Section.
  if (CsectEntry.Index == SectionEntry::UninitializedIndex)
    return;

  // There could be a gap (without corresponding zero padding) between
  // sections.
  // There could be a gap (without corresponding zero padding) between
  // sections.
  assert(((CurrentAddressLocation <= CsectEntry.Address) ||
          (CsectEntry.Flags == XCOFF::STYP_TDATA) ||
          (CsectEntry.Flags == XCOFF::STYP_TBSS)) &&
         "CurrentAddressLocation should be less than or equal to section "
         "address if the section is not TData or TBSS.");

  CurrentAddressLocation = CsectEntry.Address;

  // For virtual sections, nothing to write. But need to increase
  // CurrentAddressLocation for later sections like DWARF section has a correct
  // writing location.
  if (CsectEntry.IsVirtual) {
    CurrentAddressLocation += CsectEntry.Size;
    return;
  }

  for (const auto &Group : CsectEntry.Groups) {
    for (const auto &Csect : *Group) {
      if (uint32_t PaddingSize = Csect.Address - CurrentAddressLocation)
        W.OS.write_zeros(PaddingSize);
      if (Csect.Size)
        Asm.writeSectionData(W.OS, Csect.MCSec);
      CurrentAddressLocation = Csect.Address + Csect.Size;
    }
  }

  // The size of the tail padding in a section is the end virtual address of
  // the current section minus the end virtual address of the last csect
  // in that section.
  if (uint64_t PaddingSize =
          CsectEntry.Address + CsectEntry.Size - CurrentAddressLocation) {
    W.OS.write_zeros(PaddingSize);
    CurrentAddressLocation += PaddingSize;
  }
}

void XCOFFObjectWriter::writeSectionForDwarfSectionEntry(
    const MCAssembler &Asm, const DwarfSectionEntry &DwarfEntry,
    uint64_t &CurrentAddressLocation) {
  // There could be a gap (without corresponding zero padding) between
  // sections. For example DWARF section alignment is bigger than
  // DefaultSectionAlign.
  assert(CurrentAddressLocation <= DwarfEntry.Address &&
         "CurrentAddressLocation should be less than or equal to section "
         "address.");

  if (uint64_t PaddingSize = DwarfEntry.Address - CurrentAddressLocation)
    W.OS.write_zeros(PaddingSize);

  if (DwarfEntry.Size)
    Asm.writeSectionData(W.OS, DwarfEntry.DwarfSect->MCSec);

  CurrentAddressLocation = DwarfEntry.Address + DwarfEntry.Size;

  // DWARF section size is not aligned to DefaultSectionAlign.
  // Make sure CurrentAddressLocation is aligned to DefaultSectionAlign.
  uint32_t Mod = CurrentAddressLocation % DefaultSectionAlign;
  uint32_t TailPaddingSize = Mod ? DefaultSectionAlign - Mod : 0;
  if (TailPaddingSize)
    W.OS.write_zeros(TailPaddingSize);

  CurrentAddressLocation += TailPaddingSize;
}

void XCOFFObjectWriter::writeSectionForExceptionSectionEntry(
    const MCAssembler &Asm, ExceptionSectionEntry &ExceptionEntry,
    uint64_t &CurrentAddressLocation) {
  for (const auto &TableEntry : ExceptionEntry.ExceptionTable) {
    // For every symbol that has exception entries, you must start the entries
    // with an initial symbol table index entry
    W.write<uint32_t>(SymbolIndexMap[TableEntry.second.FunctionSymbol]);
    if (is64Bit()) {
      // 4-byte padding on 64-bit.
      W.OS.write_zeros(4);
    }
    W.OS.write_zeros(2);
    for (auto &TrapEntry : TableEntry.second.Entries) {
      writeWord(TrapEntry.TrapAddress);
      W.write<uint8_t>(TrapEntry.Lang);
      W.write<uint8_t>(TrapEntry.Reason);
    }
  }

  CurrentAddressLocation += getExceptionSectionSize();
}

void XCOFFObjectWriter::writeSectionForCInfoSymSectionEntry(
    const MCAssembler &Asm, CInfoSymSectionEntry &CInfoSymEntry,
    uint64_t &CurrentAddressLocation) {
  if (!CInfoSymSection.Entry)
    return;

  constexpr int WordSize = sizeof(uint32_t);
  std::unique_ptr<CInfoSymInfo> &CISI = CInfoSymEntry.Entry;
  const std::string &Metadata = CISI->Metadata;

  // Emit the 4-byte length of the metadata.
  W.write<uint32_t>(Metadata.size());

  if (Metadata.size() == 0)
    return;

  // Write out the payload one word at a time.
  size_t Index = 0;
  while (Index + WordSize <= Metadata.size()) {
    uint32_t NextWord =
        llvm::support::endian::read32be(Metadata.data() + Index);
    W.write<uint32_t>(NextWord);
    Index += WordSize;
  }

  // If there is padding, we have at least one byte of payload left to emit.
  if (CISI->paddingSize()) {
    std::array<uint8_t, WordSize> LastWord = {0};
    ::memcpy(LastWord.data(), Metadata.data() + Index, Metadata.size() - Index);
    W.write<uint32_t>(llvm::support::endian::read32be(LastWord.data()));
  }

  CurrentAddressLocation += CISI->size();
}

// Takes the log base 2 of the alignment and shifts the result into the 5 most
// significant bits of a byte, then or's in the csect type into the least
// significant 3 bits.
uint8_t getEncodedType(const MCSectionXCOFF *Sec) {
  unsigned Log2Align = Log2(Sec->getAlign());
  // Result is a number in the range [0, 31] which fits in the 5 least
  // significant bits. Shift this value into the 5 most significant bits, and
  // bitwise-or in the csect type.
  uint8_t EncodedAlign = Log2Align << 3;
  return EncodedAlign | Sec->getCSectType();
}

} // end anonymous namespace

std::unique_ptr<MCObjectWriter>
llvm::createXCOFFObjectWriter(std::unique_ptr<MCXCOFFObjectTargetWriter> MOTW,
                              raw_pwrite_stream &OS) {
  return std::make_unique<XCOFFObjectWriter>(std::move(MOTW), OS);
}

// TODO: Export XCOFFObjectWriter to llvm/MC/MCXCOFFObjectWriter.h and remove
// the forwarders.
void XCOFF::addExceptionEntry(MCObjectWriter &Writer, const MCSymbol *Symbol,
                              const MCSymbol *Trap, unsigned LanguageCode,
                              unsigned ReasonCode, unsigned FunctionSize,
                              bool hasDebug) {
  static_cast<XCOFFObjectWriter &>(Writer).addExceptionEntry(
      Symbol, Trap, LanguageCode, ReasonCode, FunctionSize, hasDebug);
}

void XCOFF::addCInfoSymEntry(MCObjectWriter &Writer, StringRef Name,
                             StringRef Metadata) {
  static_cast<XCOFFObjectWriter &>(Writer).addCInfoSymEntry(Name, Metadata);
}
