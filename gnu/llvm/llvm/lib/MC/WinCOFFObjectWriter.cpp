//===- llvm/MC/WinCOFFObjectWriter.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains an implementation of a Win32 COFF object file writer.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolCOFF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using llvm::support::endian::write32le;

#define DEBUG_TYPE "WinCOFFObjectWriter"

namespace {

constexpr int OffsetLabelIntervalBits = 20;

using name = SmallString<COFF::NameSize>;

enum AuxiliaryType { ATWeakExternal, ATFile, ATSectionDefinition };

struct AuxSymbol {
  AuxiliaryType AuxType;
  COFF::Auxiliary Aux;
};

class COFFSection;

class COFFSymbol {
public:
  COFF::symbol Data = {};

  using AuxiliarySymbols = SmallVector<AuxSymbol, 1>;

  name Name;
  int Index = 0;
  AuxiliarySymbols Aux;
  COFFSymbol *Other = nullptr;
  COFFSection *Section = nullptr;
  int Relocations = 0;
  const MCSymbol *MC = nullptr;

  COFFSymbol(StringRef Name) : Name(Name) {}

  void set_name_offset(uint32_t Offset);

  int64_t getIndex() const { return Index; }
  void setIndex(int Value) {
    Index = Value;
    if (MC)
      MC->setIndex(static_cast<uint32_t>(Value));
  }
};

// This class contains staging data for a COFF relocation entry.
struct COFFRelocation {
  COFF::relocation Data;
  COFFSymbol *Symb = nullptr;

  COFFRelocation() = default;

  static size_t size() { return COFF::RelocationSize; }
};

using relocations = std::vector<COFFRelocation>;

class COFFSection {
public:
  COFF::section Header = {};

  std::string Name;
  int Number = 0;
  MCSectionCOFF const *MCSection = nullptr;
  COFFSymbol *Symbol = nullptr;
  relocations Relocations;

  COFFSection(StringRef Name) : Name(std::string(Name)) {}

  SmallVector<COFFSymbol *, 1> OffsetSymbols;
};
} // namespace

class llvm::WinCOFFWriter {
  WinCOFFObjectWriter &OWriter;
  support::endian::Writer W;

  using symbols = std::vector<std::unique_ptr<COFFSymbol>>;
  using sections = std::vector<std::unique_ptr<COFFSection>>;

  using symbol_map = DenseMap<MCSymbol const *, COFFSymbol *>;
  using section_map = DenseMap<MCSection const *, COFFSection *>;

  using symbol_list = DenseSet<COFFSymbol *>;

  // Root level file contents.
  COFF::header Header = {};
  sections Sections;
  symbols Symbols;
  StringTableBuilder Strings{StringTableBuilder::WinCOFF};

  // Maps used during object file creation.
  section_map SectionMap;
  symbol_map SymbolMap;

  symbol_list WeakDefaults;

  bool UseBigObj;
  bool UseOffsetLabels = false;

public:
  enum DwoMode {
    AllSections,
    NonDwoOnly,
    DwoOnly,
  } Mode;

  WinCOFFWriter(WinCOFFObjectWriter &OWriter, raw_pwrite_stream &OS,
                DwoMode Mode);

  void reset();
  void executePostLayoutBinding(MCAssembler &Asm);
  void recordRelocation(MCAssembler &Asm, const MCFragment *Fragment,
                        const MCFixup &Fixup, MCValue Target,
                        uint64_t &FixedValue);
  uint64_t writeObject(MCAssembler &Asm);

private:
  COFFSymbol *createSymbol(StringRef Name);
  COFFSymbol *GetOrCreateCOFFSymbol(const MCSymbol *Symbol);
  COFFSection *createSection(StringRef Name);

  void defineSection(const MCAssembler &Asm, MCSectionCOFF const &Sec);

  COFFSymbol *getLinkedSymbol(const MCSymbol &Symbol);
  void defineSymbol(const MCAssembler &Asm, const MCSymbol &Symbol);

  void SetSymbolName(COFFSymbol &S);
  void SetSectionName(COFFSection &S);

  bool IsPhysicalSection(COFFSection *S);

  // Entity writing methods.
  void WriteFileHeader(const COFF::header &Header);
  void WriteSymbol(const COFFSymbol &S);
  void WriteAuxiliarySymbols(const COFFSymbol::AuxiliarySymbols &S);
  void writeSectionHeaders();
  void WriteRelocation(const COFF::relocation &R);
  uint32_t writeSectionContents(MCAssembler &Asm, const MCSection &MCSec);
  void writeSection(MCAssembler &Asm, const COFFSection &Sec);

  void createFileSymbols(MCAssembler &Asm);
  void setWeakDefaultNames();
  void assignSectionNumbers();
  void assignFileOffsets(MCAssembler &Asm);
};

WinCOFFObjectWriter::WinCOFFObjectWriter(
    std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW, raw_pwrite_stream &OS)
    : TargetObjectWriter(std::move(MOTW)),
      ObjWriter(std::make_unique<WinCOFFWriter>(*this, OS,
                                                WinCOFFWriter::AllSections)) {}
WinCOFFObjectWriter::WinCOFFObjectWriter(
    std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW, raw_pwrite_stream &OS,
    raw_pwrite_stream &DwoOS)
    : TargetObjectWriter(std::move(MOTW)),
      ObjWriter(std::make_unique<WinCOFFWriter>(*this, OS,
                                                WinCOFFWriter::NonDwoOnly)),
      DwoWriter(std::make_unique<WinCOFFWriter>(*this, DwoOS,
                                                WinCOFFWriter::DwoOnly)) {}

static bool isDwoSection(const MCSection &Sec) {
  return Sec.getName().ends_with(".dwo");
}

//------------------------------------------------------------------------------
// Symbol class implementation

// In the case that the name does not fit within 8 bytes, the offset
// into the string table is stored in the last 4 bytes instead, leaving
// the first 4 bytes as 0.
void COFFSymbol::set_name_offset(uint32_t Offset) {
  write32le(Data.Name + 0, 0);
  write32le(Data.Name + 4, Offset);
}

//------------------------------------------------------------------------------
// WinCOFFWriter class implementation

WinCOFFWriter::WinCOFFWriter(WinCOFFObjectWriter &OWriter,
                             raw_pwrite_stream &OS, DwoMode Mode)
    : OWriter(OWriter), W(OS, llvm::endianness::little), Mode(Mode) {
  Header.Machine = OWriter.TargetObjectWriter->getMachine();
  // Some relocations on ARM64 (the 21 bit ADRP relocations) have a slightly
  // limited range for the immediate offset (+/- 1 MB); create extra offset
  // label symbols with regular intervals to allow referencing a
  // non-temporary symbol that is close enough.
  UseOffsetLabels = COFF::isAnyArm64(Header.Machine);
}

COFFSymbol *WinCOFFWriter::createSymbol(StringRef Name) {
  Symbols.push_back(std::make_unique<COFFSymbol>(Name));
  return Symbols.back().get();
}

COFFSymbol *WinCOFFWriter::GetOrCreateCOFFSymbol(const MCSymbol *Symbol) {
  COFFSymbol *&Ret = SymbolMap[Symbol];
  if (!Ret)
    Ret = createSymbol(Symbol->getName());
  return Ret;
}

COFFSection *WinCOFFWriter::createSection(StringRef Name) {
  Sections.emplace_back(std::make_unique<COFFSection>(Name));
  return Sections.back().get();
}

static uint32_t getAlignment(const MCSectionCOFF &Sec) {
  switch (Sec.getAlign().value()) {
  case 1:
    return COFF::IMAGE_SCN_ALIGN_1BYTES;
  case 2:
    return COFF::IMAGE_SCN_ALIGN_2BYTES;
  case 4:
    return COFF::IMAGE_SCN_ALIGN_4BYTES;
  case 8:
    return COFF::IMAGE_SCN_ALIGN_8BYTES;
  case 16:
    return COFF::IMAGE_SCN_ALIGN_16BYTES;
  case 32:
    return COFF::IMAGE_SCN_ALIGN_32BYTES;
  case 64:
    return COFF::IMAGE_SCN_ALIGN_64BYTES;
  case 128:
    return COFF::IMAGE_SCN_ALIGN_128BYTES;
  case 256:
    return COFF::IMAGE_SCN_ALIGN_256BYTES;
  case 512:
    return COFF::IMAGE_SCN_ALIGN_512BYTES;
  case 1024:
    return COFF::IMAGE_SCN_ALIGN_1024BYTES;
  case 2048:
    return COFF::IMAGE_SCN_ALIGN_2048BYTES;
  case 4096:
    return COFF::IMAGE_SCN_ALIGN_4096BYTES;
  case 8192:
    return COFF::IMAGE_SCN_ALIGN_8192BYTES;
  }
  llvm_unreachable("unsupported section alignment");
}

/// This function takes a section data object from the assembler
/// and creates the associated COFF section staging object.
void WinCOFFWriter::defineSection(const MCAssembler &Asm,
                                  const MCSectionCOFF &MCSec) {
  COFFSection *Section = createSection(MCSec.getName());
  COFFSymbol *Symbol = createSymbol(MCSec.getName());
  Section->Symbol = Symbol;
  SymbolMap[MCSec.getBeginSymbol()] = Symbol;
  Symbol->Section = Section;
  Symbol->Data.StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;

  // Create a COMDAT symbol if needed.
  if (MCSec.getSelection() != COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE) {
    if (const MCSymbol *S = MCSec.getCOMDATSymbol()) {
      COFFSymbol *COMDATSymbol = GetOrCreateCOFFSymbol(S);
      if (COMDATSymbol->Section)
        report_fatal_error("two sections have the same comdat");
      COMDATSymbol->Section = Section;
    }
  }

  // In this case the auxiliary symbol is a Section Definition.
  Symbol->Aux.resize(1);
  Symbol->Aux[0] = {};
  Symbol->Aux[0].AuxType = ATSectionDefinition;
  Symbol->Aux[0].Aux.SectionDefinition.Selection = MCSec.getSelection();

  // Set section alignment.
  Section->Header.Characteristics = MCSec.getCharacteristics();
  Section->Header.Characteristics |= getAlignment(MCSec);

  // Bind internal COFF section to MC section.
  Section->MCSection = &MCSec;
  SectionMap[&MCSec] = Section;

  if (UseOffsetLabels && !MCSec.empty()) {
    const uint32_t Interval = 1 << OffsetLabelIntervalBits;
    uint32_t N = 1;
    for (uint32_t Off = Interval, E = Asm.getSectionAddressSize(MCSec); Off < E;
         Off += Interval) {
      auto Name = ("$L" + MCSec.getName() + "_" + Twine(N++)).str();
      COFFSymbol *Label = createSymbol(Name);
      Label->Section = Section;
      Label->Data.StorageClass = COFF::IMAGE_SYM_CLASS_LABEL;
      Label->Data.Value = Off;
      Section->OffsetSymbols.push_back(Label);
    }
  }
}

static uint64_t getSymbolValue(const MCSymbol &Symbol, const MCAssembler &Asm) {
  if (Symbol.isCommon() && Symbol.isExternal())
    return Symbol.getCommonSize();

  uint64_t Res;
  if (!Asm.getSymbolOffset(Symbol, Res))
    return 0;

  return Res;
}

COFFSymbol *WinCOFFWriter::getLinkedSymbol(const MCSymbol &Symbol) {
  if (!Symbol.isVariable())
    return nullptr;

  const MCSymbolRefExpr *SymRef =
      dyn_cast<MCSymbolRefExpr>(Symbol.getVariableValue());
  if (!SymRef)
    return nullptr;

  const MCSymbol &Aliasee = SymRef->getSymbol();
  if (Aliasee.isUndefined() || Aliasee.isExternal())
    return GetOrCreateCOFFSymbol(&Aliasee);
  else
    return nullptr;
}

/// This function takes a symbol data object from the assembler
/// and creates the associated COFF symbol staging object.
void WinCOFFWriter::defineSymbol(const MCAssembler &Asm,
                                 const MCSymbol &MCSym) {
  const MCSymbol *Base = Asm.getBaseSymbol(MCSym);
  COFFSection *Sec = nullptr;
  MCSectionCOFF *MCSec = nullptr;
  if (Base && Base->getFragment()) {
    MCSec = cast<MCSectionCOFF>(Base->getFragment()->getParent());
    Sec = SectionMap[MCSec];
  }

  if (Mode == NonDwoOnly && MCSec && isDwoSection(*MCSec))
    return;

  COFFSymbol *Sym = GetOrCreateCOFFSymbol(&MCSym);
  COFFSymbol *Local = nullptr;
  if (cast<MCSymbolCOFF>(MCSym).getWeakExternalCharacteristics()) {
    Sym->Data.StorageClass = COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL;
    Sym->Section = nullptr;

    COFFSymbol *WeakDefault = getLinkedSymbol(MCSym);
    if (!WeakDefault) {
      std::string WeakName = (".weak." + MCSym.getName() + ".default").str();
      WeakDefault = createSymbol(WeakName);
      if (!Sec)
        WeakDefault->Data.SectionNumber = COFF::IMAGE_SYM_ABSOLUTE;
      else
        WeakDefault->Section = Sec;
      WeakDefaults.insert(WeakDefault);
      Local = WeakDefault;
    }

    Sym->Other = WeakDefault;

    // Setup the Weak External auxiliary symbol.
    Sym->Aux.resize(1);
    memset(&Sym->Aux[0], 0, sizeof(Sym->Aux[0]));
    Sym->Aux[0].AuxType = ATWeakExternal;
    Sym->Aux[0].Aux.WeakExternal.TagIndex = 0; // Filled in later
    Sym->Aux[0].Aux.WeakExternal.Characteristics =
        cast<MCSymbolCOFF>(MCSym).getWeakExternalCharacteristics();
  } else {
    if (!Base)
      Sym->Data.SectionNumber = COFF::IMAGE_SYM_ABSOLUTE;
    else
      Sym->Section = Sec;
    Local = Sym;
  }

  if (Local) {
    Local->Data.Value = getSymbolValue(MCSym, Asm);

    const MCSymbolCOFF &SymbolCOFF = cast<MCSymbolCOFF>(MCSym);
    Local->Data.Type = SymbolCOFF.getType();
    Local->Data.StorageClass = SymbolCOFF.getClass();

    // If no storage class was specified in the streamer, define it here.
    if (Local->Data.StorageClass == COFF::IMAGE_SYM_CLASS_NULL) {
      bool IsExternal =
          MCSym.isExternal() || (!MCSym.getFragment() && !MCSym.isVariable());

      Local->Data.StorageClass = IsExternal ? COFF::IMAGE_SYM_CLASS_EXTERNAL
                                            : COFF::IMAGE_SYM_CLASS_STATIC;
    }
  }

  Sym->MC = &MCSym;
}

void WinCOFFWriter::SetSectionName(COFFSection &S) {
  if (S.Name.size() <= COFF::NameSize) {
    std::memcpy(S.Header.Name, S.Name.c_str(), S.Name.size());
    return;
  }

  uint64_t StringTableEntry = Strings.getOffset(S.Name);
  if (!COFF::encodeSectionName(S.Header.Name, StringTableEntry))
    report_fatal_error("COFF string table is greater than 64 GB.");
}

void WinCOFFWriter::SetSymbolName(COFFSymbol &S) {
  if (S.Name.size() > COFF::NameSize)
    S.set_name_offset(Strings.getOffset(S.Name));
  else
    std::memcpy(S.Data.Name, S.Name.c_str(), S.Name.size());
}

bool WinCOFFWriter::IsPhysicalSection(COFFSection *S) {
  return (S->Header.Characteristics & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA) ==
         0;
}

//------------------------------------------------------------------------------
// entity writing methods

void WinCOFFWriter::WriteFileHeader(const COFF::header &Header) {
  if (UseBigObj) {
    W.write<uint16_t>(COFF::IMAGE_FILE_MACHINE_UNKNOWN);
    W.write<uint16_t>(0xFFFF);
    W.write<uint16_t>(COFF::BigObjHeader::MinBigObjectVersion);
    W.write<uint16_t>(Header.Machine);
    W.write<uint32_t>(Header.TimeDateStamp);
    W.OS.write(COFF::BigObjMagic, sizeof(COFF::BigObjMagic));
    W.write<uint32_t>(0);
    W.write<uint32_t>(0);
    W.write<uint32_t>(0);
    W.write<uint32_t>(0);
    W.write<uint32_t>(Header.NumberOfSections);
    W.write<uint32_t>(Header.PointerToSymbolTable);
    W.write<uint32_t>(Header.NumberOfSymbols);
  } else {
    W.write<uint16_t>(Header.Machine);
    W.write<uint16_t>(static_cast<int16_t>(Header.NumberOfSections));
    W.write<uint32_t>(Header.TimeDateStamp);
    W.write<uint32_t>(Header.PointerToSymbolTable);
    W.write<uint32_t>(Header.NumberOfSymbols);
    W.write<uint16_t>(Header.SizeOfOptionalHeader);
    W.write<uint16_t>(Header.Characteristics);
  }
}

void WinCOFFWriter::WriteSymbol(const COFFSymbol &S) {
  W.OS.write(S.Data.Name, COFF::NameSize);
  W.write<uint32_t>(S.Data.Value);
  if (UseBigObj)
    W.write<uint32_t>(S.Data.SectionNumber);
  else
    W.write<uint16_t>(static_cast<int16_t>(S.Data.SectionNumber));
  W.write<uint16_t>(S.Data.Type);
  W.OS << char(S.Data.StorageClass);
  W.OS << char(S.Data.NumberOfAuxSymbols);
  WriteAuxiliarySymbols(S.Aux);
}

void WinCOFFWriter::WriteAuxiliarySymbols(
    const COFFSymbol::AuxiliarySymbols &S) {
  for (const AuxSymbol &i : S) {
    switch (i.AuxType) {
    case ATWeakExternal:
      W.write<uint32_t>(i.Aux.WeakExternal.TagIndex);
      W.write<uint32_t>(i.Aux.WeakExternal.Characteristics);
      W.OS.write_zeros(sizeof(i.Aux.WeakExternal.unused));
      if (UseBigObj)
        W.OS.write_zeros(COFF::Symbol32Size - COFF::Symbol16Size);
      break;
    case ATFile:
      W.OS.write(reinterpret_cast<const char *>(&i.Aux),
                 UseBigObj ? COFF::Symbol32Size : COFF::Symbol16Size);
      break;
    case ATSectionDefinition:
      W.write<uint32_t>(i.Aux.SectionDefinition.Length);
      W.write<uint16_t>(i.Aux.SectionDefinition.NumberOfRelocations);
      W.write<uint16_t>(i.Aux.SectionDefinition.NumberOfLinenumbers);
      W.write<uint32_t>(i.Aux.SectionDefinition.CheckSum);
      W.write<uint16_t>(static_cast<int16_t>(i.Aux.SectionDefinition.Number));
      W.OS << char(i.Aux.SectionDefinition.Selection);
      W.OS.write_zeros(sizeof(i.Aux.SectionDefinition.unused));
      W.write<uint16_t>(
          static_cast<int16_t>(i.Aux.SectionDefinition.Number >> 16));
      if (UseBigObj)
        W.OS.write_zeros(COFF::Symbol32Size - COFF::Symbol16Size);
      break;
    }
  }
}

// Write the section header.
void WinCOFFWriter::writeSectionHeaders() {
  // Section numbers must be monotonically increasing in the section
  // header, but our Sections array is not sorted by section number,
  // so make a copy of Sections and sort it.
  std::vector<COFFSection *> Arr;
  for (auto &Section : Sections)
    Arr.push_back(Section.get());
  llvm::sort(Arr, [](const COFFSection *A, const COFFSection *B) {
    return A->Number < B->Number;
  });

  for (auto &Section : Arr) {
    if (Section->Number == -1)
      continue;

    COFF::section &S = Section->Header;
    if (Section->Relocations.size() >= 0xffff)
      S.Characteristics |= COFF::IMAGE_SCN_LNK_NRELOC_OVFL;
    W.OS.write(S.Name, COFF::NameSize);
    W.write<uint32_t>(S.VirtualSize);
    W.write<uint32_t>(S.VirtualAddress);
    W.write<uint32_t>(S.SizeOfRawData);
    W.write<uint32_t>(S.PointerToRawData);
    W.write<uint32_t>(S.PointerToRelocations);
    W.write<uint32_t>(S.PointerToLineNumbers);
    W.write<uint16_t>(S.NumberOfRelocations);
    W.write<uint16_t>(S.NumberOfLineNumbers);
    W.write<uint32_t>(S.Characteristics);
  }
}

void WinCOFFWriter::WriteRelocation(const COFF::relocation &R) {
  W.write<uint32_t>(R.VirtualAddress);
  W.write<uint32_t>(R.SymbolTableIndex);
  W.write<uint16_t>(R.Type);
}

// Write MCSec's contents. What this function does is essentially
// "Asm.writeSectionData(&MCSec)", but it's a bit complicated
// because it needs to compute a CRC.
uint32_t WinCOFFWriter::writeSectionContents(MCAssembler &Asm,
                                             const MCSection &MCSec) {
  // Save the contents of the section to a temporary buffer, we need this
  // to CRC the data before we dump it into the object file.
  SmallVector<char, 128> Buf;
  raw_svector_ostream VecOS(Buf);
  Asm.writeSectionData(VecOS, &MCSec);

  // Write the section contents to the object file.
  W.OS << Buf;

  // Calculate our CRC with an initial value of '0', this is not how
  // JamCRC is specified but it aligns with the expected output.
  JamCRC JC(/*Init=*/0);
  JC.update(ArrayRef(reinterpret_cast<uint8_t *>(Buf.data()), Buf.size()));
  return JC.getCRC();
}

void WinCOFFWriter::writeSection(MCAssembler &Asm, const COFFSection &Sec) {
  if (Sec.Number == -1)
    return;

  // Write the section contents.
  if (Sec.Header.PointerToRawData != 0) {
    assert(W.OS.tell() == Sec.Header.PointerToRawData &&
           "Section::PointerToRawData is insane!");

    uint32_t CRC = writeSectionContents(Asm, *Sec.MCSection);

    // Update the section definition auxiliary symbol to record the CRC.
    COFFSymbol::AuxiliarySymbols &AuxSyms = Sec.Symbol->Aux;
    assert(AuxSyms.size() == 1 && AuxSyms[0].AuxType == ATSectionDefinition);
    AuxSymbol &SecDef = AuxSyms[0];
    SecDef.Aux.SectionDefinition.CheckSum = CRC;
  }

  // Write relocations for this section.
  if (Sec.Relocations.empty()) {
    assert(Sec.Header.PointerToRelocations == 0 &&
           "Section::PointerToRelocations is insane!");
    return;
  }

  assert(W.OS.tell() == Sec.Header.PointerToRelocations &&
         "Section::PointerToRelocations is insane!");

  if (Sec.Relocations.size() >= 0xffff) {
    // In case of overflow, write actual relocation count as first
    // relocation. Including the synthetic reloc itself (+ 1).
    COFF::relocation R;
    R.VirtualAddress = Sec.Relocations.size() + 1;
    R.SymbolTableIndex = 0;
    R.Type = 0;
    WriteRelocation(R);
  }

  for (const auto &Relocation : Sec.Relocations)
    WriteRelocation(Relocation.Data);
}

// Create .file symbols.
void WinCOFFWriter::createFileSymbols(MCAssembler &Asm) {
  for (const std::pair<std::string, size_t> &It : OWriter.getFileNames()) {
    // round up to calculate the number of auxiliary symbols required
    const std::string &Name = It.first;
    unsigned SymbolSize = UseBigObj ? COFF::Symbol32Size : COFF::Symbol16Size;
    unsigned Count = (Name.size() + SymbolSize - 1) / SymbolSize;

    COFFSymbol *File = createSymbol(".file");
    File->Data.SectionNumber = COFF::IMAGE_SYM_DEBUG;
    File->Data.StorageClass = COFF::IMAGE_SYM_CLASS_FILE;
    File->Aux.resize(Count);

    unsigned Offset = 0;
    unsigned Length = Name.size();
    for (auto &Aux : File->Aux) {
      Aux.AuxType = ATFile;

      if (Length > SymbolSize) {
        memcpy(&Aux.Aux, Name.c_str() + Offset, SymbolSize);
        Length = Length - SymbolSize;
      } else {
        memcpy(&Aux.Aux, Name.c_str() + Offset, Length);
        memset((char *)&Aux.Aux + Length, 0, SymbolSize - Length);
        break;
      }

      Offset += SymbolSize;
    }
  }
}

void WinCOFFWriter::setWeakDefaultNames() {
  if (WeakDefaults.empty())
    return;

  // If multiple object files use a weak symbol (either with a regular
  // defined default, or an absolute zero symbol as default), the defaults
  // cause duplicate definitions unless their names are made unique. Look
  // for a defined extern symbol, that isn't comdat - that should be unique
  // unless there are other duplicate definitions. And if none is found,
  // allow picking a comdat symbol, as that's still better than nothing.

  COFFSymbol *Unique = nullptr;
  for (bool AllowComdat : {false, true}) {
    for (auto &Sym : Symbols) {
      // Don't include the names of the defaults themselves
      if (WeakDefaults.count(Sym.get()))
        continue;
      // Only consider external symbols
      if (Sym->Data.StorageClass != COFF::IMAGE_SYM_CLASS_EXTERNAL)
        continue;
      // Only consider symbols defined in a section or that are absolute
      if (!Sym->Section && Sym->Data.SectionNumber != COFF::IMAGE_SYM_ABSOLUTE)
        continue;
      if (!AllowComdat && Sym->Section &&
          Sym->Section->Header.Characteristics & COFF::IMAGE_SCN_LNK_COMDAT)
        continue;
      Unique = Sym.get();
      break;
    }
    if (Unique)
      break;
  }
  // If we didn't find any unique symbol to use for the names, just skip this.
  if (!Unique)
    return;
  for (auto *Sym : WeakDefaults) {
    Sym->Name.append(".");
    Sym->Name.append(Unique->Name);
  }
}

static bool isAssociative(const COFFSection &Section) {
  return Section.Symbol->Aux[0].Aux.SectionDefinition.Selection ==
         COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE;
}

void WinCOFFWriter::assignSectionNumbers() {
  size_t I = 1;
  auto Assign = [&](COFFSection &Section) {
    Section.Number = I;
    Section.Symbol->Data.SectionNumber = I;
    Section.Symbol->Aux[0].Aux.SectionDefinition.Number = I;
    ++I;
  };

  // Although it is not explicitly requested by the Microsoft COFF spec,
  // we should avoid emitting forward associative section references,
  // because MSVC link.exe as of 2017 cannot handle that.
  for (const std::unique_ptr<COFFSection> &Section : Sections)
    if (!isAssociative(*Section))
      Assign(*Section);
  for (const std::unique_ptr<COFFSection> &Section : Sections)
    if (isAssociative(*Section))
      Assign(*Section);
}

// Assign file offsets to COFF object file structures.
void WinCOFFWriter::assignFileOffsets(MCAssembler &Asm) {
  unsigned Offset = W.OS.tell();

  Offset += UseBigObj ? COFF::Header32Size : COFF::Header16Size;
  Offset += COFF::SectionSize * Header.NumberOfSections;

  for (const auto &Section : Asm) {
    COFFSection *Sec = SectionMap[&Section];

    if (!Sec || Sec->Number == -1)
      continue;

    Sec->Header.SizeOfRawData = Asm.getSectionAddressSize(Section);

    if (IsPhysicalSection(Sec)) {
      Sec->Header.PointerToRawData = Offset;
      Offset += Sec->Header.SizeOfRawData;
    }

    if (!Sec->Relocations.empty()) {
      bool RelocationsOverflow = Sec->Relocations.size() >= 0xffff;

      if (RelocationsOverflow) {
        // Signal overflow by setting NumberOfRelocations to max value. Actual
        // size is found in reloc #0. Microsoft tools understand this.
        Sec->Header.NumberOfRelocations = 0xffff;
      } else {
        Sec->Header.NumberOfRelocations = Sec->Relocations.size();
      }
      Sec->Header.PointerToRelocations = Offset;

      if (RelocationsOverflow) {
        // Reloc #0 will contain actual count, so make room for it.
        Offset += COFF::RelocationSize;
      }

      Offset += COFF::RelocationSize * Sec->Relocations.size();

      for (auto &Relocation : Sec->Relocations) {
        assert(Relocation.Symb->getIndex() != -1);
        Relocation.Data.SymbolTableIndex = Relocation.Symb->getIndex();
      }
    }

    assert(Sec->Symbol->Aux.size() == 1 &&
           "Section's symbol must have one aux!");
    AuxSymbol &Aux = Sec->Symbol->Aux[0];
    assert(Aux.AuxType == ATSectionDefinition &&
           "Section's symbol's aux symbol must be a Section Definition!");
    Aux.Aux.SectionDefinition.Length = Sec->Header.SizeOfRawData;
    Aux.Aux.SectionDefinition.NumberOfRelocations =
        Sec->Header.NumberOfRelocations;
    Aux.Aux.SectionDefinition.NumberOfLinenumbers =
        Sec->Header.NumberOfLineNumbers;
  }

  Header.PointerToSymbolTable = Offset;
}

void WinCOFFWriter::reset() {
  memset(&Header, 0, sizeof(Header));
  Header.Machine = OWriter.TargetObjectWriter->getMachine();
  Sections.clear();
  Symbols.clear();
  Strings.clear();
  SectionMap.clear();
  SymbolMap.clear();
  WeakDefaults.clear();
}

void WinCOFFWriter::executePostLayoutBinding(MCAssembler &Asm) {
  // "Define" each section & symbol. This creates section & symbol
  // entries in the staging area.
  for (const auto &Section : Asm) {
    if ((Mode == NonDwoOnly && isDwoSection(Section)) ||
        (Mode == DwoOnly && !isDwoSection(Section)))
      continue;
    defineSection(Asm, static_cast<const MCSectionCOFF &>(Section));
  }

  if (Mode != DwoOnly)
    for (const MCSymbol &Symbol : Asm.symbols())
      // Define non-temporary or temporary static (private-linkage) symbols
      if (!Symbol.isTemporary() ||
          cast<MCSymbolCOFF>(Symbol).getClass() == COFF::IMAGE_SYM_CLASS_STATIC)
        defineSymbol(Asm, Symbol);
}

void WinCOFFWriter::recordRelocation(MCAssembler &Asm,
                                     const MCFragment *Fragment,
                                     const MCFixup &Fixup, MCValue Target,
                                     uint64_t &FixedValue) {
  assert(Target.getSymA() && "Relocation must reference a symbol!");

  const MCSymbol &A = Target.getSymA()->getSymbol();
  if (!A.isRegistered()) {
    Asm.getContext().reportError(Fixup.getLoc(), Twine("symbol '") +
                                                     A.getName() +
                                                     "' can not be undefined");
    return;
  }
  if (A.isTemporary() && A.isUndefined()) {
    Asm.getContext().reportError(Fixup.getLoc(), Twine("assembler label '") +
                                                     A.getName() +
                                                     "' can not be undefined");
    return;
  }

  MCSection *MCSec = Fragment->getParent();

  // Mark this symbol as requiring an entry in the symbol table.
  assert(SectionMap.contains(MCSec) &&
         "Section must already have been defined in executePostLayoutBinding!");

  COFFSection *Sec = SectionMap[MCSec];
  const MCSymbolRefExpr *SymB = Target.getSymB();

  if (SymB) {
    const MCSymbol *B = &SymB->getSymbol();
    if (!B->getFragment()) {
      Asm.getContext().reportError(
          Fixup.getLoc(),
          Twine("symbol '") + B->getName() +
              "' can not be undefined in a subtraction expression");
      return;
    }

    // Offset of the symbol in the section
    int64_t OffsetOfB = Asm.getSymbolOffset(*B);

    // Offset of the relocation in the section
    int64_t OffsetOfRelocation =
        Asm.getFragmentOffset(*Fragment) + Fixup.getOffset();

    FixedValue = (OffsetOfRelocation - OffsetOfB) + Target.getConstant();
  } else {
    FixedValue = Target.getConstant();
  }

  COFFRelocation Reloc;

  Reloc.Data.SymbolTableIndex = 0;
  Reloc.Data.VirtualAddress = Asm.getFragmentOffset(*Fragment);

  // Turn relocations for temporary symbols into section relocations.
  if (A.isTemporary() && !SymbolMap[&A]) {
    MCSection *TargetSection = &A.getSection();
    assert(
        SectionMap.contains(TargetSection) &&
        "Section must already have been defined in executePostLayoutBinding!");
    COFFSection *Section = SectionMap[TargetSection];
    Reloc.Symb = Section->Symbol;
    FixedValue += Asm.getSymbolOffset(A);
    // Technically, we should do the final adjustments of FixedValue (below)
    // before picking an offset symbol, otherwise we might choose one which
    // is slightly too far away. The relocations where it really matters
    // (arm64 adrp relocations) don't get any offset though.
    if (UseOffsetLabels && !Section->OffsetSymbols.empty()) {
      uint64_t LabelIndex = FixedValue >> OffsetLabelIntervalBits;
      if (LabelIndex > 0) {
        if (LabelIndex <= Section->OffsetSymbols.size())
          Reloc.Symb = Section->OffsetSymbols[LabelIndex - 1];
        else
          Reloc.Symb = Section->OffsetSymbols.back();
        FixedValue -= Reloc.Symb->Data.Value;
      }
    }
  } else {
    assert(
        SymbolMap.contains(&A) &&
        "Symbol must already have been defined in executePostLayoutBinding!");
    Reloc.Symb = SymbolMap[&A];
  }

  ++Reloc.Symb->Relocations;

  Reloc.Data.VirtualAddress += Fixup.getOffset();
  Reloc.Data.Type = OWriter.TargetObjectWriter->getRelocType(
      Asm.getContext(), Target, Fixup, SymB, Asm.getBackend());

  // The *_REL32 relocations are relative to the end of the relocation,
  // not to the start.
  if ((Header.Machine == COFF::IMAGE_FILE_MACHINE_AMD64 &&
       Reloc.Data.Type == COFF::IMAGE_REL_AMD64_REL32) ||
      (Header.Machine == COFF::IMAGE_FILE_MACHINE_I386 &&
       Reloc.Data.Type == COFF::IMAGE_REL_I386_REL32) ||
      (Header.Machine == COFF::IMAGE_FILE_MACHINE_ARMNT &&
       Reloc.Data.Type == COFF::IMAGE_REL_ARM_REL32) ||
      (COFF::isAnyArm64(Header.Machine) &&
       Reloc.Data.Type == COFF::IMAGE_REL_ARM64_REL32))
    FixedValue += 4;

  if (Header.Machine == COFF::IMAGE_FILE_MACHINE_ARMNT) {
    switch (Reloc.Data.Type) {
    case COFF::IMAGE_REL_ARM_ABSOLUTE:
    case COFF::IMAGE_REL_ARM_ADDR32:
    case COFF::IMAGE_REL_ARM_ADDR32NB:
    case COFF::IMAGE_REL_ARM_TOKEN:
    case COFF::IMAGE_REL_ARM_SECTION:
    case COFF::IMAGE_REL_ARM_SECREL:
      break;
    case COFF::IMAGE_REL_ARM_BRANCH11:
    case COFF::IMAGE_REL_ARM_BLX11:
    // IMAGE_REL_ARM_BRANCH11 and IMAGE_REL_ARM_BLX11 are only used for
    // pre-ARMv7, which implicitly rules it out of ARMNT (it would be valid
    // for Windows CE).
    case COFF::IMAGE_REL_ARM_BRANCH24:
    case COFF::IMAGE_REL_ARM_BLX24:
    case COFF::IMAGE_REL_ARM_MOV32A:
      // IMAGE_REL_ARM_BRANCH24, IMAGE_REL_ARM_BLX24, IMAGE_REL_ARM_MOV32A are
      // only used for ARM mode code, which is documented as being unsupported
      // by Windows on ARM.  Empirical proof indicates that masm is able to
      // generate the relocations however the rest of the MSVC toolchain is
      // unable to handle it.
      llvm_unreachable("unsupported relocation");
      break;
    case COFF::IMAGE_REL_ARM_MOV32T:
      break;
    case COFF::IMAGE_REL_ARM_BRANCH20T:
    case COFF::IMAGE_REL_ARM_BRANCH24T:
    case COFF::IMAGE_REL_ARM_BLX23T:
      // IMAGE_REL_BRANCH20T, IMAGE_REL_ARM_BRANCH24T, IMAGE_REL_ARM_BLX23T all
      // perform a 4 byte adjustment to the relocation.  Relative branches are
      // offset by 4 on ARM, however, because there is no RELA relocations, all
      // branches are offset by 4.
      FixedValue = FixedValue + 4;
      break;
    }
  }

  // The fixed value never makes sense for section indices, ignore it.
  if (Fixup.getKind() == FK_SecRel_2)
    FixedValue = 0;

  if (OWriter.TargetObjectWriter->recordRelocation(Fixup))
    Sec->Relocations.push_back(Reloc);
}

static std::time_t getTime() {
  std::time_t Now = time(nullptr);
  if (Now < 0 || !isUInt<32>(Now))
    return UINT32_MAX;
  return Now;
}

uint64_t WinCOFFWriter::writeObject(MCAssembler &Asm) {
  uint64_t StartOffset = W.OS.tell();

  if (Sections.size() > INT32_MAX)
    report_fatal_error(
        "PE COFF object files can't have more than 2147483647 sections");

  UseBigObj = Sections.size() > COFF::MaxNumberOfSections16;
  Header.NumberOfSections = Sections.size();
  Header.NumberOfSymbols = 0;

  setWeakDefaultNames();
  assignSectionNumbers();
  if (Mode != DwoOnly)
    createFileSymbols(Asm);

  for (auto &Symbol : Symbols) {
    // Update section number & offset for symbols that have them.
    if (Symbol->Section)
      Symbol->Data.SectionNumber = Symbol->Section->Number;
    Symbol->setIndex(Header.NumberOfSymbols++);
    // Update auxiliary symbol info.
    Symbol->Data.NumberOfAuxSymbols = Symbol->Aux.size();
    Header.NumberOfSymbols += Symbol->Data.NumberOfAuxSymbols;
  }

  // Build string table.
  for (const auto &S : Sections)
    if (S->Name.size() > COFF::NameSize)
      Strings.add(S->Name);
  for (const auto &S : Symbols)
    if (S->Name.size() > COFF::NameSize)
      Strings.add(S->Name);
  Strings.finalize();

  // Set names.
  for (const auto &S : Sections)
    SetSectionName(*S);
  for (auto &S : Symbols)
    SetSymbolName(*S);

  // Fixup weak external references.
  for (auto &Symbol : Symbols) {
    if (Symbol->Other) {
      assert(Symbol->getIndex() != -1);
      assert(Symbol->Aux.size() == 1 && "Symbol must contain one aux symbol!");
      assert(Symbol->Aux[0].AuxType == ATWeakExternal &&
             "Symbol's aux symbol must be a Weak External!");
      Symbol->Aux[0].Aux.WeakExternal.TagIndex = Symbol->Other->getIndex();
    }
  }

  // Fixup associative COMDAT sections.
  for (auto &Section : Sections) {
    if (Section->Symbol->Aux[0].Aux.SectionDefinition.Selection !=
        COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE)
      continue;

    const MCSectionCOFF &MCSec = *Section->MCSection;
    const MCSymbol *AssocMCSym = MCSec.getCOMDATSymbol();
    assert(AssocMCSym);

    // It's an error to try to associate with an undefined symbol or a symbol
    // without a section.
    if (!AssocMCSym->isInSection()) {
      Asm.getContext().reportError(
          SMLoc(), Twine("cannot make section ") + MCSec.getName() +
                       Twine(" associative with sectionless symbol ") +
                       AssocMCSym->getName());
      continue;
    }

    const auto *AssocMCSec = cast<MCSectionCOFF>(&AssocMCSym->getSection());
    assert(SectionMap.count(AssocMCSec));
    COFFSection *AssocSec = SectionMap[AssocMCSec];

    // Skip this section if the associated section is unused.
    if (AssocSec->Number == -1)
      continue;

    Section->Symbol->Aux[0].Aux.SectionDefinition.Number = AssocSec->Number;
  }

  // Create the contents of the .llvm_addrsig section.
  if (Mode != DwoOnly && OWriter.getEmitAddrsigSection()) {
    auto *Sec = Asm.getContext().getCOFFSection(
        ".llvm_addrsig", COFF::IMAGE_SCN_LNK_REMOVE);
    auto *Frag = cast<MCDataFragment>(Sec->curFragList()->Head);
    raw_svector_ostream OS(Frag->getContents());
    for (const MCSymbol *S : OWriter.AddrsigSyms) {
      if (!S->isRegistered())
        continue;
      if (!S->isTemporary()) {
        encodeULEB128(S->getIndex(), OS);
        continue;
      }

      MCSection *TargetSection = &S->getSection();
      assert(SectionMap.contains(TargetSection) &&
             "Section must already have been defined in "
             "executePostLayoutBinding!");
      encodeULEB128(SectionMap[TargetSection]->Symbol->getIndex(), OS);
    }
  }

  // Create the contents of the .llvm.call-graph-profile section.
  if (Mode != DwoOnly && !OWriter.getCGProfile().empty()) {
    auto *Sec = Asm.getContext().getCOFFSection(
        ".llvm.call-graph-profile", COFF::IMAGE_SCN_LNK_REMOVE);
    auto *Frag = cast<MCDataFragment>(Sec->curFragList()->Head);
    raw_svector_ostream OS(Frag->getContents());
    for (const auto &CGPE : OWriter.getCGProfile()) {
      uint32_t FromIndex = CGPE.From->getSymbol().getIndex();
      uint32_t ToIndex = CGPE.To->getSymbol().getIndex();
      support::endian::write(OS, FromIndex, W.Endian);
      support::endian::write(OS, ToIndex, W.Endian);
      support::endian::write(OS, CGPE.Count, W.Endian);
    }
  }

  assignFileOffsets(Asm);

  // MS LINK expects to be able to use this timestamp to implement their
  // /INCREMENTAL feature.
  if (OWriter.IncrementalLinkerCompatible) {
    Header.TimeDateStamp = getTime();
  } else {
    // Have deterministic output if /INCREMENTAL isn't needed. Also matches GNU.
    Header.TimeDateStamp = 0;
  }

  // Write it all to disk...
  WriteFileHeader(Header);
  writeSectionHeaders();

#ifndef NDEBUG
  sections::iterator I = Sections.begin();
  sections::iterator IE = Sections.end();
  auto J = Asm.begin();
  auto JE = Asm.end();
  for (; I != IE && J != JE; ++I, ++J) {
    while (J != JE && ((Mode == NonDwoOnly && isDwoSection(*J)) ||
                       (Mode == DwoOnly && !isDwoSection(*J))))
      ++J;
    assert(J != JE && (**I).MCSection == &*J && "Wrong bound MCSection");
  }
#endif

  // Write section contents.
  for (std::unique_ptr<COFFSection> &Sec : Sections)
    writeSection(Asm, *Sec);

  assert(W.OS.tell() == Header.PointerToSymbolTable &&
         "Header::PointerToSymbolTable is insane!");

  // Write a symbol table.
  for (auto &Symbol : Symbols)
    if (Symbol->getIndex() != -1)
      WriteSymbol(*Symbol);

  // Write a string table, which completes the entire COFF file.
  Strings.write(W.OS);

  return W.OS.tell() - StartOffset;
}

//------------------------------------------------------------------------------
// WinCOFFObjectWriter class implementation

////////////////////////////////////////////////////////////////////////////////
// MCObjectWriter interface implementations

void WinCOFFObjectWriter::reset() {
  IncrementalLinkerCompatible = false;
  ObjWriter->reset();
  if (DwoWriter)
    DwoWriter->reset();
  MCObjectWriter::reset();
}

bool WinCOFFObjectWriter::isSymbolRefDifferenceFullyResolvedImpl(
    const MCAssembler &Asm, const MCSymbol &SymA, const MCFragment &FB,
    bool InSet, bool IsPCRel) const {
  // Don't drop relocations between functions, even if they are in the same text
  // section. Multiple Visual C++ linker features depend on having the
  // relocations present. The /INCREMENTAL flag will cause these relocations to
  // point to thunks, and the /GUARD:CF flag assumes that it can use relocations
  // to approximate the set of all address taken functions. LLD's implementation
  // of /GUARD:CF also relies on the existance of these relocations.
  uint16_t Type = cast<MCSymbolCOFF>(SymA).getType();
  if ((Type >> COFF::SCT_COMPLEX_TYPE_SHIFT) == COFF::IMAGE_SYM_DTYPE_FUNCTION)
    return false;
  return &SymA.getSection() == FB.getParent();
}

void WinCOFFObjectWriter::executePostLayoutBinding(MCAssembler &Asm) {
  ObjWriter->executePostLayoutBinding(Asm);
  if (DwoWriter)
    DwoWriter->executePostLayoutBinding(Asm);
}

void WinCOFFObjectWriter::recordRelocation(MCAssembler &Asm,
                                           const MCFragment *Fragment,
                                           const MCFixup &Fixup, MCValue Target,
                                           uint64_t &FixedValue) {
  assert(!isDwoSection(*Fragment->getParent()) &&
         "No relocation in Dwo sections");
  ObjWriter->recordRelocation(Asm, Fragment, Fixup, Target, FixedValue);
}

uint64_t WinCOFFObjectWriter::writeObject(MCAssembler &Asm) {
  uint64_t TotalSize = ObjWriter->writeObject(Asm);
  if (DwoWriter)
    TotalSize += DwoWriter->writeObject(Asm);
  return TotalSize;
}

MCWinCOFFObjectTargetWriter::MCWinCOFFObjectTargetWriter(unsigned Machine_)
    : Machine(Machine_) {}

// Pin the vtable to this file.
void MCWinCOFFObjectTargetWriter::anchor() {}

//------------------------------------------------------------------------------
// WinCOFFObjectWriter factory function

std::unique_ptr<MCObjectWriter> llvm::createWinCOFFObjectWriter(
    std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW, raw_pwrite_stream &OS) {
  return std::make_unique<WinCOFFObjectWriter>(std::move(MOTW), OS);
}

std::unique_ptr<MCObjectWriter> llvm::createWinCOFFDwoObjectWriter(
    std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW, raw_pwrite_stream &OS,
    raw_pwrite_stream &DwoOS) {
  return std::make_unique<WinCOFFObjectWriter>(std::move(MOTW), OS, DwoOS);
}
