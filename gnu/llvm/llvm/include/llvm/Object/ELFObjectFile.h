//===- ELFObjectFile.h - ELF object file implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ELFObjectFile template class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ELFOBJECTFILE_H
#define LLVM_OBJECT_ELFOBJECTFILE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ELFAttributeParser.h"
#include "llvm/Support/ELFAttributes.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstdint>

namespace llvm {

template <typename T> class SmallVectorImpl;

namespace object {

constexpr int NumElfSymbolTypes = 16;
extern const llvm::EnumEntry<unsigned> ElfSymbolTypes[NumElfSymbolTypes];

class elf_symbol_iterator;

struct ELFPltEntry {
  StringRef Section;
  std::optional<DataRefImpl> Symbol;
  uint64_t Address;
};

class ELFObjectFileBase : public ObjectFile {
  friend class ELFRelocationRef;
  friend class ELFSectionRef;
  friend class ELFSymbolRef;

  SubtargetFeatures getMIPSFeatures() const;
  SubtargetFeatures getARMFeatures() const;
  SubtargetFeatures getHexagonFeatures() const;
  Expected<SubtargetFeatures> getRISCVFeatures() const;
  SubtargetFeatures getLoongArchFeatures() const;

  StringRef getAMDGPUCPUName() const;
  StringRef getNVPTXCPUName() const;

protected:
  ELFObjectFileBase(unsigned int Type, MemoryBufferRef Source);

  virtual uint64_t getSymbolSize(DataRefImpl Symb) const = 0;
  virtual uint8_t getSymbolBinding(DataRefImpl Symb) const = 0;
  virtual uint8_t getSymbolOther(DataRefImpl Symb) const = 0;
  virtual uint8_t getSymbolELFType(DataRefImpl Symb) const = 0;

  virtual uint32_t getSectionType(DataRefImpl Sec) const = 0;
  virtual uint64_t getSectionFlags(DataRefImpl Sec) const = 0;
  virtual uint64_t getSectionOffset(DataRefImpl Sec) const = 0;

  virtual Expected<int64_t> getRelocationAddend(DataRefImpl Rel) const = 0;
  virtual Error getBuildAttributes(ELFAttributeParser &Attributes) const = 0;

public:
  using elf_symbol_iterator_range = iterator_range<elf_symbol_iterator>;

  virtual elf_symbol_iterator_range getDynamicSymbolIterators() const = 0;

  /// Returns platform-specific object flags, if any.
  virtual unsigned getPlatformFlags() const = 0;

  elf_symbol_iterator_range symbols() const;

  static bool classof(const Binary *v) { return v->isELF(); }

  Expected<SubtargetFeatures> getFeatures() const override;

  std::optional<StringRef> tryGetCPUName() const override;

  void setARMSubArch(Triple &TheTriple) const override;

  virtual uint16_t getEType() const = 0;

  virtual uint16_t getEMachine() const = 0;

  virtual uint8_t getEIdentABIVersion() const = 0;

  std::vector<ELFPltEntry> getPltEntries() const;

  /// Returns a vector containing a symbol version for each dynamic symbol.
  /// Returns an empty vector if version sections do not exist.
  Expected<std::vector<VersionEntry>> readDynsymVersions() const;

  /// Returns a vector of all BB address maps in the object file. When
  /// `TextSectionIndex` is specified, only returns the BB address maps
  /// corresponding to the section with that index. When `PGOAnalyses`is
  /// specified (PGOAnalyses is not nullptr), the vector is cleared then filled
  /// with extra PGO data. `PGOAnalyses` will always be the same length as the
  /// return value when it is requested assuming no error occurs. Upon failure,
  /// `PGOAnalyses` will be emptied.
  Expected<std::vector<BBAddrMap>>
  readBBAddrMap(std::optional<unsigned> TextSectionIndex = std::nullopt,
                std::vector<PGOAnalysisMap> *PGOAnalyses = nullptr) const;

  StringRef getCrelDecodeProblem(SectionRef Sec) const;
};

class ELFSectionRef : public SectionRef {
public:
  ELFSectionRef(const SectionRef &B) : SectionRef(B) {
    assert(isa<ELFObjectFileBase>(SectionRef::getObject()));
  }

  const ELFObjectFileBase *getObject() const {
    return cast<ELFObjectFileBase>(SectionRef::getObject());
  }

  uint32_t getType() const {
    return getObject()->getSectionType(getRawDataRefImpl());
  }

  uint64_t getFlags() const {
    return getObject()->getSectionFlags(getRawDataRefImpl());
  }

  uint64_t getOffset() const {
    return getObject()->getSectionOffset(getRawDataRefImpl());
  }
};

class elf_section_iterator : public section_iterator {
public:
  elf_section_iterator(const section_iterator &B) : section_iterator(B) {
    assert(isa<ELFObjectFileBase>(B->getObject()));
  }

  const ELFSectionRef *operator->() const {
    return static_cast<const ELFSectionRef *>(section_iterator::operator->());
  }

  const ELFSectionRef &operator*() const {
    return static_cast<const ELFSectionRef &>(section_iterator::operator*());
  }
};

class ELFSymbolRef : public SymbolRef {
public:
  ELFSymbolRef(const SymbolRef &B) : SymbolRef(B) {
    assert(isa<ELFObjectFileBase>(SymbolRef::getObject()));
  }

  const ELFObjectFileBase *getObject() const {
    return cast<ELFObjectFileBase>(BasicSymbolRef::getObject());
  }

  uint64_t getSize() const {
    return getObject()->getSymbolSize(getRawDataRefImpl());
  }

  uint8_t getBinding() const {
    return getObject()->getSymbolBinding(getRawDataRefImpl());
  }

  uint8_t getOther() const {
    return getObject()->getSymbolOther(getRawDataRefImpl());
  }

  uint8_t getELFType() const {
    return getObject()->getSymbolELFType(getRawDataRefImpl());
  }

  StringRef getELFTypeName() const {
    uint8_t Type = getELFType();
    for (const auto &EE : ElfSymbolTypes) {
      if (EE.Value == Type) {
        return EE.AltName;
      }
    }
    return "";
  }
};

inline bool operator<(const ELFSymbolRef &A, const ELFSymbolRef &B) {
  const DataRefImpl &DRIA = A.getRawDataRefImpl();
  const DataRefImpl &DRIB = B.getRawDataRefImpl();
  if (DRIA.d.a == DRIB.d.a)
    return DRIA.d.b < DRIB.d.b;
  return DRIA.d.a < DRIB.d.a;
}

class elf_symbol_iterator : public symbol_iterator {
public:
  elf_symbol_iterator(const basic_symbol_iterator &B)
      : symbol_iterator(SymbolRef(B->getRawDataRefImpl(),
                                  cast<ELFObjectFileBase>(B->getObject()))) {}

  const ELFSymbolRef *operator->() const {
    return static_cast<const ELFSymbolRef *>(symbol_iterator::operator->());
  }

  const ELFSymbolRef &operator*() const {
    return static_cast<const ELFSymbolRef &>(symbol_iterator::operator*());
  }
};

class ELFRelocationRef : public RelocationRef {
public:
  ELFRelocationRef(const RelocationRef &B) : RelocationRef(B) {
    assert(isa<ELFObjectFileBase>(RelocationRef::getObject()));
  }

  const ELFObjectFileBase *getObject() const {
    return cast<ELFObjectFileBase>(RelocationRef::getObject());
  }

  Expected<int64_t> getAddend() const {
    return getObject()->getRelocationAddend(getRawDataRefImpl());
  }
};

class elf_relocation_iterator : public relocation_iterator {
public:
  elf_relocation_iterator(const relocation_iterator &B)
      : relocation_iterator(RelocationRef(
            B->getRawDataRefImpl(), cast<ELFObjectFileBase>(B->getObject()))) {}

  const ELFRelocationRef *operator->() const {
    return static_cast<const ELFRelocationRef *>(
        relocation_iterator::operator->());
  }

  const ELFRelocationRef &operator*() const {
    return static_cast<const ELFRelocationRef &>(
        relocation_iterator::operator*());
  }
};

inline ELFObjectFileBase::elf_symbol_iterator_range
ELFObjectFileBase::symbols() const {
  return elf_symbol_iterator_range(symbol_begin(), symbol_end());
}

template <class ELFT> class ELFObjectFile : public ELFObjectFileBase {
  uint16_t getEMachine() const override;
  uint16_t getEType() const override;
  uint8_t getEIdentABIVersion() const override;
  uint64_t getSymbolSize(DataRefImpl Sym) const override;

public:
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

  SectionRef toSectionRef(const Elf_Shdr *Sec) const {
    return SectionRef(toDRI(Sec), this);
  }

  ELFSymbolRef toSymbolRef(const Elf_Shdr *SymTable, unsigned SymbolNum) const {
    return ELFSymbolRef({toDRI(SymTable, SymbolNum), this});
  }

  bool IsContentValid() const { return ContentValid; }

private:
  ELFObjectFile(MemoryBufferRef Object, ELFFile<ELFT> EF,
                const Elf_Shdr *DotDynSymSec, const Elf_Shdr *DotSymtabSec,
                const Elf_Shdr *DotSymtabShndxSec);

  bool ContentValid = false;

protected:
  ELFFile<ELFT> EF;

  const Elf_Shdr *DotDynSymSec = nullptr; // Dynamic symbol table section.
  const Elf_Shdr *DotSymtabSec = nullptr; // Symbol table section.
  const Elf_Shdr *DotSymtabShndxSec = nullptr; // SHT_SYMTAB_SHNDX section.

  // Hold CREL relocations for SectionRef::relocations().
  mutable SmallVector<SmallVector<Elf_Crel, 0>, 0> Crels;
  mutable SmallVector<std::string, 0> CrelDecodeProblems;

  Error initContent() override;

  void moveSymbolNext(DataRefImpl &Symb) const override;
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  uint32_t getSymbolAlignment(DataRefImpl Symb) const override;
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;
  uint8_t getSymbolBinding(DataRefImpl Symb) const override;
  uint8_t getSymbolOther(DataRefImpl Symb) const override;
  uint8_t getSymbolELFType(DataRefImpl Symb) const override;
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  Expected<section_iterator> getSymbolSection(const Elf_Sym *Symb,
                                              const Elf_Shdr *SymTab) const;
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;

  void moveSectionNext(DataRefImpl &Sec) const override;
  Expected<StringRef> getSectionName(DataRefImpl Sec) const override;
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  uint64_t getSectionIndex(DataRefImpl Sec) const override;
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const override;
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  bool isSectionCompressed(DataRefImpl Sec) const override;
  bool isSectionText(DataRefImpl Sec) const override;
  bool isSectionData(DataRefImpl Sec) const override;
  bool isSectionBSS(DataRefImpl Sec) const override;
  bool isSectionVirtual(DataRefImpl Sec) const override;
  bool isBerkeleyText(DataRefImpl Sec) const override;
  bool isBerkeleyData(DataRefImpl Sec) const override;
  bool isDebugSection(DataRefImpl Sec) const override;
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;
  std::vector<SectionRef> dynamic_relocation_sections() const override;
  Expected<section_iterator>
  getRelocatedSection(DataRefImpl Sec) const override;

  void moveRelocationNext(DataRefImpl &Rel) const override;
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;

  uint32_t getSectionType(DataRefImpl Sec) const override;
  uint64_t getSectionFlags(DataRefImpl Sec) const override;
  uint64_t getSectionOffset(DataRefImpl Sec) const override;
  StringRef getRelocationTypeName(uint32_t Type) const;

  DataRefImpl toDRI(const Elf_Shdr *SymTable, unsigned SymbolNum) const {
    DataRefImpl DRI;
    if (!SymTable) {
      DRI.d.a = 0;
      DRI.d.b = 0;
      return DRI;
    }
    assert(SymTable->sh_type == ELF::SHT_SYMTAB ||
           SymTable->sh_type == ELF::SHT_DYNSYM);

    auto SectionsOrErr = EF.sections();
    if (!SectionsOrErr) {
      DRI.d.a = 0;
      DRI.d.b = 0;
      return DRI;
    }
    uintptr_t SHT = reinterpret_cast<uintptr_t>((*SectionsOrErr).begin());
    unsigned SymTableIndex =
        (reinterpret_cast<uintptr_t>(SymTable) - SHT) / sizeof(Elf_Shdr);

    DRI.d.a = SymTableIndex;
    DRI.d.b = SymbolNum;
    return DRI;
  }

  const Elf_Shdr *toELFShdrIter(DataRefImpl Sec) const {
    return reinterpret_cast<const Elf_Shdr *>(Sec.p);
  }

  DataRefImpl toDRI(const Elf_Shdr *Sec) const {
    DataRefImpl DRI;
    DRI.p = reinterpret_cast<uintptr_t>(Sec);
    return DRI;
  }

  DataRefImpl toDRI(const Elf_Dyn *Dyn) const {
    DataRefImpl DRI;
    DRI.p = reinterpret_cast<uintptr_t>(Dyn);
    return DRI;
  }

  bool isExportedToOtherDSO(const Elf_Sym *ESym) const {
    unsigned char Binding = ESym->getBinding();
    unsigned char Visibility = ESym->getVisibility();

    // A symbol is exported if its binding is either GLOBAL or WEAK, and its
    // visibility is either DEFAULT or PROTECTED. All other symbols are not
    // exported.
    return (
        (Binding == ELF::STB_GLOBAL || Binding == ELF::STB_WEAK ||
         Binding == ELF::STB_GNU_UNIQUE) &&
        (Visibility == ELF::STV_DEFAULT || Visibility == ELF::STV_PROTECTED));
  }

  Error getBuildAttributes(ELFAttributeParser &Attributes) const override {
    uint32_t Type;
    switch (getEMachine()) {
    case ELF::EM_ARM:
      Type = ELF::SHT_ARM_ATTRIBUTES;
      break;
    case ELF::EM_RISCV:
      Type = ELF::SHT_RISCV_ATTRIBUTES;
      break;
    case ELF::EM_HEXAGON:
      Type = ELF::SHT_HEXAGON_ATTRIBUTES;
      break;
    default:
      return Error::success();
    }

    auto SectionsOrErr = EF.sections();
    if (!SectionsOrErr)
      return SectionsOrErr.takeError();
    for (const Elf_Shdr &Sec : *SectionsOrErr) {
      if (Sec.sh_type != Type)
        continue;
      auto ErrorOrContents = EF.getSectionContents(Sec);
      if (!ErrorOrContents)
        return ErrorOrContents.takeError();

      auto Contents = ErrorOrContents.get();
      if (Contents[0] != ELFAttrs::Format_Version || Contents.size() == 1)
        return Error::success();

      if (Error E = Attributes.parse(Contents, ELFT::Endianness))
        return E;
      break;
    }
    return Error::success();
  }

  // This flag is used for classof, to distinguish ELFObjectFile from
  // its subclass. If more subclasses will be created, this flag will
  // have to become an enum.
  bool isDyldELFObject = false;

public:
  ELFObjectFile(ELFObjectFile<ELFT> &&Other);
  static Expected<ELFObjectFile<ELFT>> create(MemoryBufferRef Object,
                                              bool InitContent = true);

  const Elf_Rel *getRel(DataRefImpl Rel) const;
  const Elf_Rela *getRela(DataRefImpl Rela) const;
  Elf_Crel getCrel(DataRefImpl Crel) const;

  Expected<const Elf_Sym *> getSymbol(DataRefImpl Sym) const {
    return EF.template getEntry<Elf_Sym>(Sym.d.a, Sym.d.b);
  }

  /// Get the relocation section that contains \a Rel.
  const Elf_Shdr *getRelSection(DataRefImpl Rel) const {
    auto RelSecOrErr = EF.getSection(Rel.d.a);
    if (!RelSecOrErr)
      report_fatal_error(
          Twine(errorToErrorCode(RelSecOrErr.takeError()).message()));
    return *RelSecOrErr;
  }

  const Elf_Shdr *getSection(DataRefImpl Sec) const {
    return reinterpret_cast<const Elf_Shdr *>(Sec.p);
  }

  basic_symbol_iterator symbol_begin() const override;
  basic_symbol_iterator symbol_end() const override;

  bool is64Bit() const override { return getBytesInAddress() == 8; }

  elf_symbol_iterator dynamic_symbol_begin() const;
  elf_symbol_iterator dynamic_symbol_end() const;

  section_iterator section_begin() const override;
  section_iterator section_end() const override;

  Expected<int64_t> getRelocationAddend(DataRefImpl Rel) const override;

  uint8_t getBytesInAddress() const override;
  StringRef getFileFormatName() const override;
  Triple::ArchType getArch() const override;
  Triple::OSType getOS() const override;
  Expected<uint64_t> getStartAddress() const override;

  unsigned getPlatformFlags() const override { return EF.getHeader().e_flags; }

  const ELFFile<ELFT> &getELFFile() const { return EF; }

  bool isDyldType() const { return isDyldELFObject; }
  static bool classof(const Binary *v) {
    return v->getType() ==
           getELFType(ELFT::Endianness == llvm::endianness::little,
                      ELFT::Is64Bits);
  }

  elf_symbol_iterator_range getDynamicSymbolIterators() const override;

  bool isRelocatableObject() const override;

  void createFakeSections() { EF.createFakeSections(); }

  StringRef getCrelDecodeProblem(DataRefImpl Sec) const;
};

using ELF32LEObjectFile = ELFObjectFile<ELF32LE>;
using ELF64LEObjectFile = ELFObjectFile<ELF64LE>;
using ELF32BEObjectFile = ELFObjectFile<ELF32BE>;
using ELF64BEObjectFile = ELFObjectFile<ELF64BE>;

template <class ELFT>
void ELFObjectFile<ELFT>::moveSymbolNext(DataRefImpl &Sym) const {
  ++Sym.d.b;
}

template <class ELFT> Error ELFObjectFile<ELFT>::initContent() {
  auto SectionsOrErr = EF.sections();
  if (!SectionsOrErr)
    return SectionsOrErr.takeError();

  for (const Elf_Shdr &Sec : *SectionsOrErr) {
    switch (Sec.sh_type) {
    case ELF::SHT_DYNSYM: {
      if (!DotDynSymSec)
        DotDynSymSec = &Sec;
      break;
    }
    case ELF::SHT_SYMTAB: {
      if (!DotSymtabSec)
        DotSymtabSec = &Sec;
      break;
    }
    case ELF::SHT_SYMTAB_SHNDX: {
      if (!DotSymtabShndxSec)
        DotSymtabShndxSec = &Sec;
      break;
    }
    }
  }

  ContentValid = true;
  return Error::success();
}

template <class ELFT>
Expected<StringRef> ELFObjectFile<ELFT>::getSymbolName(DataRefImpl Sym) const {
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Sym);
  if (!SymOrErr)
    return SymOrErr.takeError();
  auto SymTabOrErr = EF.getSection(Sym.d.a);
  if (!SymTabOrErr)
    return SymTabOrErr.takeError();
  const Elf_Shdr *SymTableSec = *SymTabOrErr;
  auto StrTabOrErr = EF.getSection(SymTableSec->sh_link);
  if (!StrTabOrErr)
    return StrTabOrErr.takeError();
  const Elf_Shdr *StringTableSec = *StrTabOrErr;
  auto SymStrTabOrErr = EF.getStringTable(*StringTableSec);
  if (!SymStrTabOrErr)
    return SymStrTabOrErr.takeError();
  Expected<StringRef> Name = (*SymOrErr)->getName(*SymStrTabOrErr);
  if (Name && !Name->empty())
    return Name;

  // If the symbol name is empty use the section name.
  if ((*SymOrErr)->getType() == ELF::STT_SECTION) {
    Expected<section_iterator> SecOrErr = getSymbolSection(Sym);
    if (SecOrErr)
      return (*SecOrErr)->getName();
    return SecOrErr.takeError();
  }
  return Name;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionFlags(DataRefImpl Sec) const {
  return getSection(Sec)->sh_flags;
}

template <class ELFT>
uint32_t ELFObjectFile<ELFT>::getSectionType(DataRefImpl Sec) const {
  return getSection(Sec)->sh_type;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionOffset(DataRefImpl Sec) const {
  return getSection(Sec)->sh_offset;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSymbolValueImpl(DataRefImpl Symb) const {
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Symb);
  if (!SymOrErr)
    report_fatal_error(SymOrErr.takeError());

  uint64_t Ret = (*SymOrErr)->st_value;
  if ((*SymOrErr)->st_shndx == ELF::SHN_ABS)
    return Ret;

  const Elf_Ehdr &Header = EF.getHeader();
  // Clear the ARM/Thumb or microMIPS indicator flag.
  if ((Header.e_machine == ELF::EM_ARM || Header.e_machine == ELF::EM_MIPS) &&
      (*SymOrErr)->getType() == ELF::STT_FUNC)
    Ret &= ~1;

  return Ret;
}

template <class ELFT>
Expected<uint64_t>
ELFObjectFile<ELFT>::getSymbolAddress(DataRefImpl Symb) const {
  Expected<uint64_t> SymbolValueOrErr = getSymbolValue(Symb);
  if (!SymbolValueOrErr)
    // TODO: Test this error.
    return SymbolValueOrErr.takeError();

  uint64_t Result = *SymbolValueOrErr;
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Symb);
  if (!SymOrErr)
    return SymOrErr.takeError();

  switch ((*SymOrErr)->st_shndx) {
  case ELF::SHN_COMMON:
  case ELF::SHN_UNDEF:
  case ELF::SHN_ABS:
    return Result;
  }

  auto SymTabOrErr = EF.getSection(Symb.d.a);
  if (!SymTabOrErr)
    return SymTabOrErr.takeError();

  if (EF.getHeader().e_type == ELF::ET_REL) {
    ArrayRef<Elf_Word> ShndxTable;
    if (DotSymtabShndxSec) {
      // TODO: Test this error.
      if (Expected<ArrayRef<Elf_Word>> ShndxTableOrErr =
              EF.getSHNDXTable(*DotSymtabShndxSec))
        ShndxTable = *ShndxTableOrErr;
      else
        return ShndxTableOrErr.takeError();
    }

    Expected<const Elf_Shdr *> SectionOrErr =
        EF.getSection(**SymOrErr, *SymTabOrErr, ShndxTable);
    if (!SectionOrErr)
      return SectionOrErr.takeError();
    const Elf_Shdr *Section = *SectionOrErr;
    if (Section)
      Result += Section->sh_addr;
  }

  return Result;
}

template <class ELFT>
uint32_t ELFObjectFile<ELFT>::getSymbolAlignment(DataRefImpl Symb) const {
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Symb);
  if (!SymOrErr)
    report_fatal_error(SymOrErr.takeError());
  if ((*SymOrErr)->st_shndx == ELF::SHN_COMMON)
    return (*SymOrErr)->st_value;
  return 0;
}

template <class ELFT>
uint16_t ELFObjectFile<ELFT>::getEMachine() const {
  return EF.getHeader().e_machine;
}

template <class ELFT> uint16_t ELFObjectFile<ELFT>::getEType() const {
  return EF.getHeader().e_type;
}

template <class ELFT> uint8_t ELFObjectFile<ELFT>::getEIdentABIVersion() const {
  return EF.getHeader().e_ident[ELF::EI_ABIVERSION];
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSymbolSize(DataRefImpl Sym) const {
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Sym);
  if (!SymOrErr)
    report_fatal_error(SymOrErr.takeError());
  return (*SymOrErr)->st_size;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getCommonSymbolSizeImpl(DataRefImpl Symb) const {
  return getSymbolSize(Symb);
}

template <class ELFT>
uint8_t ELFObjectFile<ELFT>::getSymbolBinding(DataRefImpl Symb) const {
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Symb);
  if (!SymOrErr)
    report_fatal_error(SymOrErr.takeError());
  return (*SymOrErr)->getBinding();
}

template <class ELFT>
uint8_t ELFObjectFile<ELFT>::getSymbolOther(DataRefImpl Symb) const {
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Symb);
  if (!SymOrErr)
    report_fatal_error(SymOrErr.takeError());
  return (*SymOrErr)->st_other;
}

template <class ELFT>
uint8_t ELFObjectFile<ELFT>::getSymbolELFType(DataRefImpl Symb) const {
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Symb);
  if (!SymOrErr)
    report_fatal_error(SymOrErr.takeError());
  return (*SymOrErr)->getType();
}

template <class ELFT>
Expected<SymbolRef::Type>
ELFObjectFile<ELFT>::getSymbolType(DataRefImpl Symb) const {
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Symb);
  if (!SymOrErr)
    return SymOrErr.takeError();

  switch ((*SymOrErr)->getType()) {
  case ELF::STT_NOTYPE:
    return SymbolRef::ST_Unknown;
  case ELF::STT_SECTION:
    return SymbolRef::ST_Debug;
  case ELF::STT_FILE:
    return SymbolRef::ST_File;
  case ELF::STT_FUNC:
    return SymbolRef::ST_Function;
  case ELF::STT_OBJECT:
  case ELF::STT_COMMON:
    return SymbolRef::ST_Data;
  case ELF::STT_TLS:
  default:
    return SymbolRef::ST_Other;
  }
}

template <class ELFT>
Expected<uint32_t> ELFObjectFile<ELFT>::getSymbolFlags(DataRefImpl Sym) const {
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Sym);
  if (!SymOrErr)
    return SymOrErr.takeError();

  const Elf_Sym *ESym = *SymOrErr;
  uint32_t Result = SymbolRef::SF_None;

  if (ESym->getBinding() != ELF::STB_LOCAL)
    Result |= SymbolRef::SF_Global;

  if (ESym->getBinding() == ELF::STB_WEAK)
    Result |= SymbolRef::SF_Weak;

  if (ESym->st_shndx == ELF::SHN_ABS)
    Result |= SymbolRef::SF_Absolute;

  if (ESym->getType() == ELF::STT_FILE || ESym->getType() == ELF::STT_SECTION)
    Result |= SymbolRef::SF_FormatSpecific;

  if (Expected<typename ELFT::SymRange> SymbolsOrErr =
          EF.symbols(DotSymtabSec)) {
    // Set the SF_FormatSpecific flag for the 0-index null symbol.
    if (ESym == SymbolsOrErr->begin())
      Result |= SymbolRef::SF_FormatSpecific;
  } else
    // TODO: Test this error.
    return SymbolsOrErr.takeError();

  if (Expected<typename ELFT::SymRange> SymbolsOrErr =
          EF.symbols(DotDynSymSec)) {
    // Set the SF_FormatSpecific flag for the 0-index null symbol.
    if (ESym == SymbolsOrErr->begin())
      Result |= SymbolRef::SF_FormatSpecific;
  } else
    // TODO: Test this error.
    return SymbolsOrErr.takeError();

  if (EF.getHeader().e_machine == ELF::EM_AARCH64) {
    if (Expected<StringRef> NameOrErr = getSymbolName(Sym)) {
      StringRef Name = *NameOrErr;
      if (Name.starts_with("$d") || Name.starts_with("$x"))
        Result |= SymbolRef::SF_FormatSpecific;
    } else {
      // TODO: Actually report errors helpfully.
      consumeError(NameOrErr.takeError());
    }
  } else if (EF.getHeader().e_machine == ELF::EM_ARM) {
    if (Expected<StringRef> NameOrErr = getSymbolName(Sym)) {
      StringRef Name = *NameOrErr;
      // TODO Investigate why empty name symbols need to be marked.
      if (Name.empty() || Name.starts_with("$d") || Name.starts_with("$t") ||
          Name.starts_with("$a"))
        Result |= SymbolRef::SF_FormatSpecific;
    } else {
      // TODO: Actually report errors helpfully.
      consumeError(NameOrErr.takeError());
    }
    if (ESym->getType() == ELF::STT_FUNC && (ESym->st_value & 1) == 1)
      Result |= SymbolRef::SF_Thumb;
  } else if (EF.getHeader().e_machine == ELF::EM_CSKY) {
    if (Expected<StringRef> NameOrErr = getSymbolName(Sym)) {
      StringRef Name = *NameOrErr;
      if (Name.starts_with("$d") || Name.starts_with("$t"))
        Result |= SymbolRef::SF_FormatSpecific;
    } else {
      // TODO: Actually report errors helpfully.
      consumeError(NameOrErr.takeError());
    }
  } else if (EF.getHeader().e_machine == ELF::EM_RISCV) {
    if (Expected<StringRef> NameOrErr = getSymbolName(Sym)) {
      StringRef Name = *NameOrErr;
      // Mark fake labels (used for label differences) and mapping symbols.
      if (Name == ".L0 " || Name.starts_with("$d") || Name.starts_with("$x"))
        Result |= SymbolRef::SF_FormatSpecific;
    } else {
      // TODO: Actually report errors helpfully.
      consumeError(NameOrErr.takeError());
    }
  }

  if (ESym->st_shndx == ELF::SHN_UNDEF)
    Result |= SymbolRef::SF_Undefined;

  if (ESym->getType() == ELF::STT_COMMON || ESym->st_shndx == ELF::SHN_COMMON)
    Result |= SymbolRef::SF_Common;

  if (isExportedToOtherDSO(ESym))
    Result |= SymbolRef::SF_Exported;

  if (ESym->getType() == ELF::STT_GNU_IFUNC)
    Result |= SymbolRef::SF_Indirect;

  if (ESym->getVisibility() == ELF::STV_HIDDEN)
    Result |= SymbolRef::SF_Hidden;

  return Result;
}

template <class ELFT>
Expected<section_iterator>
ELFObjectFile<ELFT>::getSymbolSection(const Elf_Sym *ESym,
                                      const Elf_Shdr *SymTab) const {
  ArrayRef<Elf_Word> ShndxTable;
  if (DotSymtabShndxSec) {
    // TODO: Test this error.
    Expected<ArrayRef<Elf_Word>> ShndxTableOrErr =
        EF.getSHNDXTable(*DotSymtabShndxSec);
    if (!ShndxTableOrErr)
      return ShndxTableOrErr.takeError();
    ShndxTable = *ShndxTableOrErr;
  }

  auto ESecOrErr = EF.getSection(*ESym, SymTab, ShndxTable);
  if (!ESecOrErr)
    return ESecOrErr.takeError();

  const Elf_Shdr *ESec = *ESecOrErr;
  if (!ESec)
    return section_end();

  DataRefImpl Sec;
  Sec.p = reinterpret_cast<intptr_t>(ESec);
  return section_iterator(SectionRef(Sec, this));
}

template <class ELFT>
Expected<section_iterator>
ELFObjectFile<ELFT>::getSymbolSection(DataRefImpl Symb) const {
  Expected<const Elf_Sym *> SymOrErr = getSymbol(Symb);
  if (!SymOrErr)
    return SymOrErr.takeError();

  auto SymTabOrErr = EF.getSection(Symb.d.a);
  if (!SymTabOrErr)
    return SymTabOrErr.takeError();
  return getSymbolSection(*SymOrErr, *SymTabOrErr);
}

template <class ELFT>
void ELFObjectFile<ELFT>::moveSectionNext(DataRefImpl &Sec) const {
  const Elf_Shdr *ESec = getSection(Sec);
  Sec = toDRI(++ESec);
}

template <class ELFT>
Expected<StringRef> ELFObjectFile<ELFT>::getSectionName(DataRefImpl Sec) const {
  return EF.getSectionName(*getSection(Sec));
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionAddress(DataRefImpl Sec) const {
  return getSection(Sec)->sh_addr;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionIndex(DataRefImpl Sec) const {
  auto SectionsOrErr = EF.sections();
  handleAllErrors(std::move(SectionsOrErr.takeError()),
                  [](const ErrorInfoBase &) {
                    llvm_unreachable("unable to get section index");
                  });
  const Elf_Shdr *First = SectionsOrErr->begin();
  return getSection(Sec) - First;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionSize(DataRefImpl Sec) const {
  return getSection(Sec)->sh_size;
}

template <class ELFT>
Expected<ArrayRef<uint8_t>>
ELFObjectFile<ELFT>::getSectionContents(DataRefImpl Sec) const {
  const Elf_Shdr *EShdr = getSection(Sec);
  if (EShdr->sh_type == ELF::SHT_NOBITS)
    return ArrayRef((const uint8_t *)base(), (size_t)0);
  if (Error E =
          checkOffset(getMemoryBufferRef(),
                      (uintptr_t)base() + EShdr->sh_offset, EShdr->sh_size))
    return std::move(E);
  return ArrayRef((const uint8_t *)base() + EShdr->sh_offset, EShdr->sh_size);
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getSectionAlignment(DataRefImpl Sec) const {
  return getSection(Sec)->sh_addralign;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionCompressed(DataRefImpl Sec) const {
  return getSection(Sec)->sh_flags & ELF::SHF_COMPRESSED;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionText(DataRefImpl Sec) const {
  return getSection(Sec)->sh_flags & ELF::SHF_EXECINSTR;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionData(DataRefImpl Sec) const {
  const Elf_Shdr *EShdr = getSection(Sec);
  return EShdr->sh_type == ELF::SHT_PROGBITS &&
         EShdr->sh_flags & ELF::SHF_ALLOC &&
         !(EShdr->sh_flags & ELF::SHF_EXECINSTR);
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionBSS(DataRefImpl Sec) const {
  const Elf_Shdr *EShdr = getSection(Sec);
  return EShdr->sh_flags & (ELF::SHF_ALLOC | ELF::SHF_WRITE) &&
         EShdr->sh_type == ELF::SHT_NOBITS;
}

template <class ELFT>
std::vector<SectionRef>
ELFObjectFile<ELFT>::dynamic_relocation_sections() const {
  std::vector<SectionRef> Res;
  std::vector<uintptr_t> Offsets;

  auto SectionsOrErr = EF.sections();
  if (!SectionsOrErr)
    return Res;

  for (const Elf_Shdr &Sec : *SectionsOrErr) {
    if (Sec.sh_type != ELF::SHT_DYNAMIC)
      continue;
    Elf_Dyn *Dynamic =
        reinterpret_cast<Elf_Dyn *>((uintptr_t)base() + Sec.sh_offset);
    for (; Dynamic->d_tag != ELF::DT_NULL; Dynamic++) {
      if (Dynamic->d_tag == ELF::DT_REL || Dynamic->d_tag == ELF::DT_RELA ||
          Dynamic->d_tag == ELF::DT_JMPREL) {
        Offsets.push_back(Dynamic->d_un.d_val);
      }
    }
  }
  for (const Elf_Shdr &Sec : *SectionsOrErr) {
    if (is_contained(Offsets, Sec.sh_addr))
      Res.emplace_back(toDRI(&Sec), this);
  }
  return Res;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isSectionVirtual(DataRefImpl Sec) const {
  return getSection(Sec)->sh_type == ELF::SHT_NOBITS;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isBerkeleyText(DataRefImpl Sec) const {
  return getSection(Sec)->sh_flags & ELF::SHF_ALLOC &&
         (getSection(Sec)->sh_flags & ELF::SHF_EXECINSTR ||
          !(getSection(Sec)->sh_flags & ELF::SHF_WRITE));
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isBerkeleyData(DataRefImpl Sec) const {
  const Elf_Shdr *EShdr = getSection(Sec);
  return !isBerkeleyText(Sec) && EShdr->sh_type != ELF::SHT_NOBITS &&
         EShdr->sh_flags & ELF::SHF_ALLOC;
}

template <class ELFT>
bool ELFObjectFile<ELFT>::isDebugSection(DataRefImpl Sec) const {
  Expected<StringRef> SectionNameOrErr = getSectionName(Sec);
  if (!SectionNameOrErr) {
    // TODO: Report the error message properly.
    consumeError(SectionNameOrErr.takeError());
    return false;
  }
  StringRef SectionName = SectionNameOrErr.get();
  return SectionName.starts_with(".debug") ||
         SectionName.starts_with(".zdebug") || SectionName == ".gdb_index";
}

template <class ELFT>
relocation_iterator
ELFObjectFile<ELFT>::section_rel_begin(DataRefImpl Sec) const {
  DataRefImpl RelData;
  auto SectionsOrErr = EF.sections();
  if (!SectionsOrErr)
    return relocation_iterator(RelocationRef());
  uintptr_t SHT = reinterpret_cast<uintptr_t>((*SectionsOrErr).begin());
  RelData.d.a = (Sec.p - SHT) / EF.getHeader().e_shentsize;
  RelData.d.b = 0;
  if (reinterpret_cast<const Elf_Shdr *>(Sec.p)->sh_type == ELF::SHT_CREL) {
    if (RelData.d.a + 1 > Crels.size())
      Crels.resize(RelData.d.a + 1);
    auto &Crel = Crels[RelData.d.a];
    if (Crel.empty()) {
      ArrayRef<uint8_t> Content = cantFail(getSectionContents(Sec));
      size_t I = 0;
      Error Err = decodeCrel<ELFT::Is64Bits>(
          Content, [&](uint64_t Count, bool) { Crel.resize(Count); },
          [&](Elf_Crel Crel) { Crels[RelData.d.a][I++] = Crel; });
      if (Err) {
        Crel.assign(1, Elf_Crel{0, 0, 0, 0});
        if (RelData.d.a + 1 > CrelDecodeProblems.size())
          CrelDecodeProblems.resize(RelData.d.a + 1);
        CrelDecodeProblems[RelData.d.a] = toString(std::move(Err));
      }
    }
  }
  return relocation_iterator(RelocationRef(RelData, this));
}

template <class ELFT>
relocation_iterator
ELFObjectFile<ELFT>::section_rel_end(DataRefImpl Sec) const {
  const Elf_Shdr *S = reinterpret_cast<const Elf_Shdr *>(Sec.p);
  relocation_iterator Begin = section_rel_begin(Sec);
  DataRefImpl RelData = Begin->getRawDataRefImpl();
  if (S->sh_type == ELF::SHT_CREL) {
    RelData.d.b = Crels[RelData.d.a].size();
    return relocation_iterator(RelocationRef(RelData, this));
  }
  if (S->sh_type != ELF::SHT_RELA && S->sh_type != ELF::SHT_REL)
    return Begin;
  const Elf_Shdr *RelSec = getRelSection(RelData);

  // Error check sh_link here so that getRelocationSymbol can just use it.
  auto SymSecOrErr = EF.getSection(RelSec->sh_link);
  if (!SymSecOrErr)
    report_fatal_error(
        Twine(errorToErrorCode(SymSecOrErr.takeError()).message()));

  RelData.d.b += S->sh_size / S->sh_entsize;
  return relocation_iterator(RelocationRef(RelData, this));
}

template <class ELFT>
Expected<section_iterator>
ELFObjectFile<ELFT>::getRelocatedSection(DataRefImpl Sec) const {
  const Elf_Shdr *EShdr = getSection(Sec);
  uintX_t Type = EShdr->sh_type;
  if (Type != ELF::SHT_REL && Type != ELF::SHT_RELA && Type != ELF::SHT_CREL)
    return section_end();

  Expected<const Elf_Shdr *> SecOrErr = EF.getSection(EShdr->sh_info);
  if (!SecOrErr)
    return SecOrErr.takeError();
  return section_iterator(SectionRef(toDRI(*SecOrErr), this));
}

// Relocations
template <class ELFT>
void ELFObjectFile<ELFT>::moveRelocationNext(DataRefImpl &Rel) const {
  ++Rel.d.b;
}

template <class ELFT>
symbol_iterator
ELFObjectFile<ELFT>::getRelocationSymbol(DataRefImpl Rel) const {
  uint32_t symbolIdx;
  const Elf_Shdr *sec = getRelSection(Rel);
  if (sec->sh_type == ELF::SHT_CREL)
    symbolIdx = getCrel(Rel).r_symidx;
  else if (sec->sh_type == ELF::SHT_REL)
    symbolIdx = getRel(Rel)->getSymbol(EF.isMips64EL());
  else
    symbolIdx = getRela(Rel)->getSymbol(EF.isMips64EL());
  if (!symbolIdx)
    return symbol_end();

  // FIXME: error check symbolIdx
  DataRefImpl SymbolData;
  SymbolData.d.a = sec->sh_link;
  SymbolData.d.b = symbolIdx;
  return symbol_iterator(SymbolRef(SymbolData, this));
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getRelocationOffset(DataRefImpl Rel) const {
  const Elf_Shdr *sec = getRelSection(Rel);
  if (sec->sh_type == ELF::SHT_CREL)
    return getCrel(Rel).r_offset;
  if (sec->sh_type == ELF::SHT_REL)
    return getRel(Rel)->r_offset;

  return getRela(Rel)->r_offset;
}

template <class ELFT>
uint64_t ELFObjectFile<ELFT>::getRelocationType(DataRefImpl Rel) const {
  const Elf_Shdr *sec = getRelSection(Rel);
  if (sec->sh_type == ELF::SHT_CREL)
    return getCrel(Rel).r_type;
  if (sec->sh_type == ELF::SHT_REL)
    return getRel(Rel)->getType(EF.isMips64EL());
  else
    return getRela(Rel)->getType(EF.isMips64EL());
}

template <class ELFT>
StringRef ELFObjectFile<ELFT>::getRelocationTypeName(uint32_t Type) const {
  return getELFRelocationTypeName(EF.getHeader().e_machine, Type);
}

template <class ELFT>
void ELFObjectFile<ELFT>::getRelocationTypeName(
    DataRefImpl Rel, SmallVectorImpl<char> &Result) const {
  uint32_t type = getRelocationType(Rel);
  EF.getRelocationTypeName(type, Result);
}

template <class ELFT>
Expected<int64_t>
ELFObjectFile<ELFT>::getRelocationAddend(DataRefImpl Rel) const {
  if (getRelSection(Rel)->sh_type == ELF::SHT_RELA)
    return (int64_t)getRela(Rel)->r_addend;
  if (getRelSection(Rel)->sh_type == ELF::SHT_CREL)
    return (int64_t)getCrel(Rel).r_addend;
  return createError("Relocation section does not have addends");
}

template <class ELFT>
const typename ELFObjectFile<ELFT>::Elf_Rel *
ELFObjectFile<ELFT>::getRel(DataRefImpl Rel) const {
  assert(getRelSection(Rel)->sh_type == ELF::SHT_REL);
  auto Ret = EF.template getEntry<Elf_Rel>(Rel.d.a, Rel.d.b);
  if (!Ret)
    report_fatal_error(Twine(errorToErrorCode(Ret.takeError()).message()));
  return *Ret;
}

template <class ELFT>
const typename ELFObjectFile<ELFT>::Elf_Rela *
ELFObjectFile<ELFT>::getRela(DataRefImpl Rela) const {
  assert(getRelSection(Rela)->sh_type == ELF::SHT_RELA);
  auto Ret = EF.template getEntry<Elf_Rela>(Rela.d.a, Rela.d.b);
  if (!Ret)
    report_fatal_error(Twine(errorToErrorCode(Ret.takeError()).message()));
  return *Ret;
}

template <class ELFT>
typename ELFObjectFile<ELFT>::Elf_Crel
ELFObjectFile<ELFT>::getCrel(DataRefImpl Crel) const {
  assert(getRelSection(Crel)->sh_type == ELF::SHT_CREL);
  assert(Crel.d.a < Crels.size());
  return Crels[Crel.d.a][Crel.d.b];
}

template <class ELFT>
Expected<ELFObjectFile<ELFT>>
ELFObjectFile<ELFT>::create(MemoryBufferRef Object, bool InitContent) {
  auto EFOrErr = ELFFile<ELFT>::create(Object.getBuffer());
  if (Error E = EFOrErr.takeError())
    return std::move(E);

  ELFObjectFile<ELFT> Obj = {Object, std::move(*EFOrErr), nullptr, nullptr,
                             nullptr};
  if (InitContent)
    if (Error E = Obj.initContent())
      return std::move(E);
  return std::move(Obj);
}

template <class ELFT>
ELFObjectFile<ELFT>::ELFObjectFile(MemoryBufferRef Object, ELFFile<ELFT> EF,
                                   const Elf_Shdr *DotDynSymSec,
                                   const Elf_Shdr *DotSymtabSec,
                                   const Elf_Shdr *DotSymtabShndx)
    : ELFObjectFileBase(getELFType(ELFT::Endianness == llvm::endianness::little,
                                   ELFT::Is64Bits),
                        Object),
      EF(EF), DotDynSymSec(DotDynSymSec), DotSymtabSec(DotSymtabSec),
      DotSymtabShndxSec(DotSymtabShndx) {}

template <class ELFT>
ELFObjectFile<ELFT>::ELFObjectFile(ELFObjectFile<ELFT> &&Other)
    : ELFObjectFile(Other.Data, Other.EF, Other.DotDynSymSec,
                    Other.DotSymtabSec, Other.DotSymtabShndxSec) {}

template <class ELFT>
basic_symbol_iterator ELFObjectFile<ELFT>::symbol_begin() const {
  DataRefImpl Sym =
      toDRI(DotSymtabSec,
            DotSymtabSec && DotSymtabSec->sh_size >= sizeof(Elf_Sym) ? 1 : 0);
  return basic_symbol_iterator(SymbolRef(Sym, this));
}

template <class ELFT>
basic_symbol_iterator ELFObjectFile<ELFT>::symbol_end() const {
  const Elf_Shdr *SymTab = DotSymtabSec;
  if (!SymTab)
    return symbol_begin();
  DataRefImpl Sym = toDRI(SymTab, SymTab->sh_size / sizeof(Elf_Sym));
  return basic_symbol_iterator(SymbolRef(Sym, this));
}

template <class ELFT>
elf_symbol_iterator ELFObjectFile<ELFT>::dynamic_symbol_begin() const {
  if (!DotDynSymSec || DotDynSymSec->sh_size < sizeof(Elf_Sym))
    // Ignore errors here where the dynsym is empty or sh_size less than the
    // size of one symbol. These should be handled elsewhere.
    return symbol_iterator(SymbolRef(toDRI(DotDynSymSec, 0), this));
  // Skip 0-index NULL symbol.
  return symbol_iterator(SymbolRef(toDRI(DotDynSymSec, 1), this));
}

template <class ELFT>
elf_symbol_iterator ELFObjectFile<ELFT>::dynamic_symbol_end() const {
  const Elf_Shdr *SymTab = DotDynSymSec;
  if (!SymTab)
    return dynamic_symbol_begin();
  DataRefImpl Sym = toDRI(SymTab, SymTab->sh_size / sizeof(Elf_Sym));
  return basic_symbol_iterator(SymbolRef(Sym, this));
}

template <class ELFT>
section_iterator ELFObjectFile<ELFT>::section_begin() const {
  auto SectionsOrErr = EF.sections();
  if (!SectionsOrErr)
    return section_iterator(SectionRef());
  return section_iterator(SectionRef(toDRI((*SectionsOrErr).begin()), this));
}

template <class ELFT>
section_iterator ELFObjectFile<ELFT>::section_end() const {
  auto SectionsOrErr = EF.sections();
  if (!SectionsOrErr)
    return section_iterator(SectionRef());
  return section_iterator(SectionRef(toDRI((*SectionsOrErr).end()), this));
}

template <class ELFT>
uint8_t ELFObjectFile<ELFT>::getBytesInAddress() const {
  return ELFT::Is64Bits ? 8 : 4;
}

template <class ELFT>
StringRef ELFObjectFile<ELFT>::getFileFormatName() const {
  constexpr bool IsLittleEndian = ELFT::Endianness == llvm::endianness::little;
  switch (EF.getHeader().e_ident[ELF::EI_CLASS]) {
  case ELF::ELFCLASS32:
    switch (EF.getHeader().e_machine) {
    case ELF::EM_68K:
      return "elf32-m68k";
    case ELF::EM_386:
      return "elf32-i386";
    case ELF::EM_IAMCU:
      return "elf32-iamcu";
    case ELF::EM_X86_64:
      return "elf32-x86-64";
    case ELF::EM_ARM:
      return (IsLittleEndian ? "elf32-littlearm" : "elf32-bigarm");
    case ELF::EM_AVR:
      return "elf32-avr";
    case ELF::EM_HEXAGON:
      return "elf32-hexagon";
    case ELF::EM_LANAI:
      return "elf32-lanai";
    case ELF::EM_MIPS:
      return "elf32-mips";
    case ELF::EM_MSP430:
      return "elf32-msp430";
    case ELF::EM_PPC:
      return (IsLittleEndian ? "elf32-powerpcle" : "elf32-powerpc");
    case ELF::EM_RISCV:
      return "elf32-littleriscv";
    case ELF::EM_CSKY:
      return "elf32-csky";
    case ELF::EM_SPARC:
    case ELF::EM_SPARC32PLUS:
      return "elf32-sparc";
    case ELF::EM_AMDGPU:
      return "elf32-amdgpu";
    case ELF::EM_LOONGARCH:
      return "elf32-loongarch";
    case ELF::EM_XTENSA:
      return "elf32-xtensa";
    default:
      return "elf32-unknown";
    }
  case ELF::ELFCLASS64:
    switch (EF.getHeader().e_machine) {
    case ELF::EM_386:
      return "elf64-i386";
    case ELF::EM_X86_64:
      return "elf64-x86-64";
    case ELF::EM_AARCH64:
      return (IsLittleEndian ? "elf64-littleaarch64" : "elf64-bigaarch64");
    case ELF::EM_PPC64:
      return (IsLittleEndian ? "elf64-powerpcle" : "elf64-powerpc");
    case ELF::EM_RISCV:
      return "elf64-littleriscv";
    case ELF::EM_S390:
      return "elf64-s390";
    case ELF::EM_SPARCV9:
      return "elf64-sparc";
    case ELF::EM_MIPS:
      return "elf64-mips";
    case ELF::EM_AMDGPU:
      return "elf64-amdgpu";
    case ELF::EM_BPF:
      return "elf64-bpf";
    case ELF::EM_VE:
      return "elf64-ve";
    case ELF::EM_LOONGARCH:
      return "elf64-loongarch";
    default:
      return "elf64-unknown";
    }
  default:
    // FIXME: Proper error handling.
    report_fatal_error("Invalid ELFCLASS!");
  }
}

template <class ELFT> Triple::ArchType ELFObjectFile<ELFT>::getArch() const {
  bool IsLittleEndian = ELFT::Endianness == llvm::endianness::little;
  switch (EF.getHeader().e_machine) {
  case ELF::EM_68K:
    return Triple::m68k;
  case ELF::EM_386:
  case ELF::EM_IAMCU:
    return Triple::x86;
  case ELF::EM_X86_64:
    return Triple::x86_64;
  case ELF::EM_AARCH64:
    return IsLittleEndian ? Triple::aarch64 : Triple::aarch64_be;
  case ELF::EM_ARM:
    return Triple::arm;
  case ELF::EM_AVR:
    return Triple::avr;
  case ELF::EM_HEXAGON:
    return Triple::hexagon;
  case ELF::EM_LANAI:
    return Triple::lanai;
  case ELF::EM_MIPS:
    switch (EF.getHeader().e_ident[ELF::EI_CLASS]) {
    case ELF::ELFCLASS32:
      return IsLittleEndian ? Triple::mipsel : Triple::mips;
    case ELF::ELFCLASS64:
      return IsLittleEndian ? Triple::mips64el : Triple::mips64;
    default:
      report_fatal_error("Invalid ELFCLASS!");
    }
  case ELF::EM_MSP430:
    return Triple::msp430;
  case ELF::EM_PPC:
    return IsLittleEndian ? Triple::ppcle : Triple::ppc;
  case ELF::EM_PPC64:
    return IsLittleEndian ? Triple::ppc64le : Triple::ppc64;
  case ELF::EM_RISCV:
    switch (EF.getHeader().e_ident[ELF::EI_CLASS]) {
    case ELF::ELFCLASS32:
      return Triple::riscv32;
    case ELF::ELFCLASS64:
      return Triple::riscv64;
    default:
      report_fatal_error("Invalid ELFCLASS!");
    }
  case ELF::EM_S390:
    return Triple::systemz;

  case ELF::EM_SPARC:
  case ELF::EM_SPARC32PLUS:
    return IsLittleEndian ? Triple::sparcel : Triple::sparc;
  case ELF::EM_SPARCV9:
    return Triple::sparcv9;

  case ELF::EM_AMDGPU: {
    if (!IsLittleEndian)
      return Triple::UnknownArch;

    unsigned MACH = EF.getHeader().e_flags & ELF::EF_AMDGPU_MACH;
    if (MACH >= ELF::EF_AMDGPU_MACH_R600_FIRST &&
        MACH <= ELF::EF_AMDGPU_MACH_R600_LAST)
      return Triple::r600;
    if (MACH >= ELF::EF_AMDGPU_MACH_AMDGCN_FIRST &&
        MACH <= ELF::EF_AMDGPU_MACH_AMDGCN_LAST)
      return Triple::amdgcn;

    return Triple::UnknownArch;
  }

  case ELF::EM_CUDA: {
    if (EF.getHeader().e_ident[ELF::EI_CLASS] == ELF::ELFCLASS32)
      return Triple::nvptx;
    return Triple::nvptx64;
  }

  case ELF::EM_BPF:
    return IsLittleEndian ? Triple::bpfel : Triple::bpfeb;

  case ELF::EM_VE:
    return Triple::ve;
  case ELF::EM_CSKY:
    return Triple::csky;

  case ELF::EM_LOONGARCH:
    switch (EF.getHeader().e_ident[ELF::EI_CLASS]) {
    case ELF::ELFCLASS32:
      return Triple::loongarch32;
    case ELF::ELFCLASS64:
      return Triple::loongarch64;
    default:
      report_fatal_error("Invalid ELFCLASS!");
    }

  case ELF::EM_XTENSA:
    return Triple::xtensa;

  default:
    return Triple::UnknownArch;
  }
}

template <class ELFT> Triple::OSType ELFObjectFile<ELFT>::getOS() const {
  switch (EF.getHeader().e_ident[ELF::EI_OSABI]) {
  case ELF::ELFOSABI_NETBSD:
    return Triple::NetBSD;
  case ELF::ELFOSABI_LINUX:
    return Triple::Linux;
  case ELF::ELFOSABI_HURD:
    return Triple::Hurd;
  case ELF::ELFOSABI_SOLARIS:
    return Triple::Solaris;
  case ELF::ELFOSABI_AIX:
    return Triple::AIX;
  case ELF::ELFOSABI_FREEBSD:
    return Triple::FreeBSD;
  case ELF::ELFOSABI_OPENBSD:
    return Triple::OpenBSD;
  case ELF::ELFOSABI_CUDA:
    return Triple::CUDA;
  case ELF::ELFOSABI_AMDGPU_HSA:
    return Triple::AMDHSA;
  case ELF::ELFOSABI_AMDGPU_PAL:
    return Triple::AMDPAL;
  case ELF::ELFOSABI_AMDGPU_MESA3D:
    return Triple::Mesa3D;
  default:
    return Triple::UnknownOS;
  }
}

template <class ELFT>
Expected<uint64_t> ELFObjectFile<ELFT>::getStartAddress() const {
  return EF.getHeader().e_entry;
}

template <class ELFT>
ELFObjectFileBase::elf_symbol_iterator_range
ELFObjectFile<ELFT>::getDynamicSymbolIterators() const {
  return make_range(dynamic_symbol_begin(), dynamic_symbol_end());
}

template <class ELFT> bool ELFObjectFile<ELFT>::isRelocatableObject() const {
  return EF.getHeader().e_type == ELF::ET_REL;
}

template <class ELFT>
StringRef ELFObjectFile<ELFT>::getCrelDecodeProblem(DataRefImpl Sec) const {
  uintptr_t SHT = reinterpret_cast<uintptr_t>(cantFail(EF.sections()).begin());
  auto I = (Sec.p - SHT) / EF.getHeader().e_shentsize;
  if (I < CrelDecodeProblems.size())
    return CrelDecodeProblems[I];
  return "";
}

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_ELFOBJECTFILE_H
