//===- ELFDumper.cpp - ELF-specific dumper --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the ELF-specific dumper for llvm-readobj.
///
//===----------------------------------------------------------------------===//

#include "ARMEHABIPrinter.h"
#include "DwarfCFIEHPrinter.h"
#include "ObjDumper.h"
#include "StackMapPrinter.h"
#include "llvm-readobj.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/AMDGPUMetadataVerifier.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MsgPackDocument.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/RelocationResolver.h"
#include "llvm/Object/StackMapParser.h"
#include "llvm/Support/AMDGPUMetadata.h"
#include "llvm/Support/ARMAttributeParser.h"
#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/HexagonAttributeParser.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MSP430AttributeParser.h"
#include "llvm/Support/MSP430Attributes.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MipsABIFlags.h"
#include "llvm/Support/RISCVAttributeParser.h"
#include "llvm/Support/RISCVAttributes.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/SystemZ/zOSSupport.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support;
using namespace ELF;

#define LLVM_READOBJ_ENUM_CASE(ns, enum)                                       \
  case ns::enum:                                                               \
    return #enum;

#define ENUM_ENT(enum, altName)                                                \
  { #enum, altName, ELF::enum }

#define ENUM_ENT_1(enum)                                                       \
  { #enum, #enum, ELF::enum }

namespace {

template <class ELFT> struct RelSymbol {
  RelSymbol(const typename ELFT::Sym *S, StringRef N)
      : Sym(S), Name(N.str()) {}
  const typename ELFT::Sym *Sym;
  std::string Name;
};

/// Represents a contiguous uniform range in the file. We cannot just create a
/// range directly because when creating one of these from the .dynamic table
/// the size, entity size and virtual address are different entries in arbitrary
/// order (DT_REL, DT_RELSZ, DT_RELENT for example).
struct DynRegionInfo {
  DynRegionInfo(const Binary &Owner, const ObjDumper &D)
      : Obj(&Owner), Dumper(&D) {}
  DynRegionInfo(const Binary &Owner, const ObjDumper &D, const uint8_t *A,
                uint64_t S, uint64_t ES)
      : Addr(A), Size(S), EntSize(ES), Obj(&Owner), Dumper(&D) {}

  /// Address in current address space.
  const uint8_t *Addr = nullptr;
  /// Size in bytes of the region.
  uint64_t Size = 0;
  /// Size of each entity in the region.
  uint64_t EntSize = 0;

  /// Owner object. Used for error reporting.
  const Binary *Obj;
  /// Dumper used for error reporting.
  const ObjDumper *Dumper;
  /// Error prefix. Used for error reporting to provide more information.
  std::string Context;
  /// Region size name. Used for error reporting.
  StringRef SizePrintName = "size";
  /// Entry size name. Used for error reporting. If this field is empty, errors
  /// will not mention the entry size.
  StringRef EntSizePrintName = "entry size";

  template <typename Type> ArrayRef<Type> getAsArrayRef() const {
    const Type *Start = reinterpret_cast<const Type *>(Addr);
    if (!Start)
      return {Start, Start};

    const uint64_t Offset =
        Addr - (const uint8_t *)Obj->getMemoryBufferRef().getBufferStart();
    const uint64_t ObjSize = Obj->getMemoryBufferRef().getBufferSize();

    if (Size > ObjSize - Offset) {
      Dumper->reportUniqueWarning(
          "unable to read data at 0x" + Twine::utohexstr(Offset) +
          " of size 0x" + Twine::utohexstr(Size) + " (" + SizePrintName +
          "): it goes past the end of the file of size 0x" +
          Twine::utohexstr(ObjSize));
      return {Start, Start};
    }

    if (EntSize == sizeof(Type) && (Size % EntSize == 0))
      return {Start, Start + (Size / EntSize)};

    std::string Msg;
    if (!Context.empty())
      Msg += Context + " has ";

    Msg += ("invalid " + SizePrintName + " (0x" + Twine::utohexstr(Size) + ")")
               .str();
    if (!EntSizePrintName.empty())
      Msg +=
          (" or " + EntSizePrintName + " (0x" + Twine::utohexstr(EntSize) + ")")
              .str();

    Dumper->reportUniqueWarning(Msg);
    return {Start, Start};
  }
};

struct GroupMember {
  StringRef Name;
  uint64_t Index;
};

struct GroupSection {
  StringRef Name;
  std::string Signature;
  uint64_t ShName;
  uint64_t Index;
  uint32_t Link;
  uint32_t Info;
  uint32_t Type;
  std::vector<GroupMember> Members;
};

namespace {

struct NoteType {
  uint32_t ID;
  StringRef Name;
};

} // namespace

template <class ELFT> class Relocation {
public:
  Relocation(const typename ELFT::Rel &R, bool IsMips64EL)
      : Type(R.getType(IsMips64EL)), Symbol(R.getSymbol(IsMips64EL)),
        Offset(R.r_offset), Info(R.r_info) {}

  Relocation(const typename ELFT::Rela &R, bool IsMips64EL)
      : Relocation((const typename ELFT::Rel &)R, IsMips64EL) {
    Addend = R.r_addend;
  }

  uint32_t Type;
  uint32_t Symbol;
  typename ELFT::uint Offset;
  typename ELFT::uint Info;
  std::optional<int64_t> Addend;
};

template <class ELFT> class MipsGOTParser;

template <typename ELFT> class ELFDumper : public ObjDumper {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

public:
  ELFDumper(const object::ELFObjectFile<ELFT> &ObjF, ScopedPrinter &Writer);

  void printUnwindInfo() override;
  void printNeededLibraries() override;
  void printHashTable() override;
  void printGnuHashTable() override;
  void printLoadName() override;
  void printVersionInfo() override;
  void printArchSpecificInfo() override;
  void printStackMap() const override;
  void printMemtag() override;
  ArrayRef<uint8_t> getMemtagGlobalsSectionContents(uint64_t ExpectedAddr);

  // Hash histogram shows statistics of how efficient the hash was for the
  // dynamic symbol table. The table shows the number of hash buckets for
  // different lengths of chains as an absolute number and percentage of the
  // total buckets, and the cumulative coverage of symbols for each set of
  // buckets.
  void printHashHistograms() override;

  const object::ELFObjectFile<ELFT> &getElfObject() const { return ObjF; };

  std::string describe(const Elf_Shdr &Sec) const;

  unsigned getHashTableEntSize() const {
    // EM_S390 and ELF::EM_ALPHA platforms use 8-bytes entries in SHT_HASH
    // sections. This violates the ELF specification.
    if (Obj.getHeader().e_machine == ELF::EM_S390 ||
        Obj.getHeader().e_machine == ELF::EM_ALPHA)
      return 8;
    return 4;
  }

  std::vector<EnumEntry<unsigned>>
  getOtherFlagsFromSymbol(const Elf_Ehdr &Header, const Elf_Sym &Symbol) const;

  Elf_Dyn_Range dynamic_table() const {
    // A valid .dynamic section contains an array of entries terminated
    // with a DT_NULL entry. However, sometimes the section content may
    // continue past the DT_NULL entry, so to dump the section correctly,
    // we first find the end of the entries by iterating over them.
    Elf_Dyn_Range Table = DynamicTable.template getAsArrayRef<Elf_Dyn>();

    size_t Size = 0;
    while (Size < Table.size())
      if (Table[Size++].getTag() == DT_NULL)
        break;

    return Table.slice(0, Size);
  }

  Elf_Sym_Range dynamic_symbols() const {
    if (!DynSymRegion)
      return Elf_Sym_Range();
    return DynSymRegion->template getAsArrayRef<Elf_Sym>();
  }

  const Elf_Shdr *findSectionByName(StringRef Name) const;

  StringRef getDynamicStringTable() const { return DynamicStringTable; }

protected:
  virtual void printVersionSymbolSection(const Elf_Shdr *Sec) = 0;
  virtual void printVersionDefinitionSection(const Elf_Shdr *Sec) = 0;
  virtual void printVersionDependencySection(const Elf_Shdr *Sec) = 0;

  void
  printDependentLibsHelper(function_ref<void(const Elf_Shdr &)> OnSectionStart,
                           function_ref<void(StringRef, uint64_t)> OnLibEntry);

  virtual void printRelRelaReloc(const Relocation<ELFT> &R,
                                 const RelSymbol<ELFT> &RelSym) = 0;
  virtual void printDynamicRelocHeader(unsigned Type, StringRef Name,
                                       const DynRegionInfo &Reg) {}
  void printReloc(const Relocation<ELFT> &R, unsigned RelIndex,
                  const Elf_Shdr &Sec, const Elf_Shdr *SymTab);
  void printDynamicReloc(const Relocation<ELFT> &R);
  void printDynamicRelocationsHelper();
  void printRelocationsHelper(const Elf_Shdr &Sec);
  void forEachRelocationDo(
      const Elf_Shdr &Sec,
      llvm::function_ref<void(const Relocation<ELFT> &, unsigned,
                              const Elf_Shdr &, const Elf_Shdr *)>
          RelRelaFn);

  virtual void printSymtabMessage(const Elf_Shdr *Symtab, size_t Offset,
                                  bool NonVisibilityBitsUsed,
                                  bool ExtraSymInfo) const {};
  virtual void printSymbol(const Elf_Sym &Symbol, unsigned SymIndex,
                           DataRegion<Elf_Word> ShndxTable,
                           std::optional<StringRef> StrTable, bool IsDynamic,
                           bool NonVisibilityBitsUsed,
                           bool ExtraSymInfo) const = 0;

  virtual void printMipsABIFlags() = 0;
  virtual void printMipsGOT(const MipsGOTParser<ELFT> &Parser) = 0;
  virtual void printMipsPLT(const MipsGOTParser<ELFT> &Parser) = 0;

  virtual void printMemtag(
      const ArrayRef<std::pair<std::string, std::string>> DynamicEntries,
      const ArrayRef<uint8_t> AndroidNoteDesc,
      const ArrayRef<std::pair<uint64_t, uint64_t>> Descriptors) = 0;

  virtual void printHashHistogram(const Elf_Hash &HashTable) const;
  virtual void printGnuHashHistogram(const Elf_GnuHash &GnuHashTable) const;
  virtual void printHashHistogramStats(size_t NBucket, size_t MaxChain,
                                       size_t TotalSyms, ArrayRef<size_t> Count,
                                       bool IsGnu) const = 0;

  Expected<ArrayRef<Elf_Versym>>
  getVersionTable(const Elf_Shdr &Sec, ArrayRef<Elf_Sym> *SymTab,
                  StringRef *StrTab, const Elf_Shdr **SymTabSec) const;
  StringRef getPrintableSectionName(const Elf_Shdr &Sec) const;

  std::vector<GroupSection> getGroups();

  // Returns the function symbol index for the given address. Matches the
  // symbol's section with FunctionSec when specified.
  // Returns std::nullopt if no function symbol can be found for the address or
  // in case it is not defined in the specified section.
  SmallVector<uint32_t> getSymbolIndexesForFunctionAddress(
      uint64_t SymValue, std::optional<const Elf_Shdr *> FunctionSec);
  bool printFunctionStackSize(uint64_t SymValue,
                              std::optional<const Elf_Shdr *> FunctionSec,
                              const Elf_Shdr &StackSizeSec, DataExtractor Data,
                              uint64_t *Offset);
  void printStackSize(const Relocation<ELFT> &R, const Elf_Shdr &RelocSec,
                      unsigned Ndx, const Elf_Shdr *SymTab,
                      const Elf_Shdr *FunctionSec, const Elf_Shdr &StackSizeSec,
                      const RelocationResolver &Resolver, DataExtractor Data);
  virtual void printStackSizeEntry(uint64_t Size,
                                   ArrayRef<std::string> FuncNames) = 0;

  void printRelocatableStackSizes(std::function<void()> PrintHeader);
  void printNonRelocatableStackSizes(std::function<void()> PrintHeader);

  const object::ELFObjectFile<ELFT> &ObjF;
  const ELFFile<ELFT> &Obj;
  StringRef FileName;

  Expected<DynRegionInfo> createDRI(uint64_t Offset, uint64_t Size,
                                    uint64_t EntSize) {
    if (Offset + Size < Offset || Offset + Size > Obj.getBufSize())
      return createError("offset (0x" + Twine::utohexstr(Offset) +
                         ") + size (0x" + Twine::utohexstr(Size) +
                         ") is greater than the file size (0x" +
                         Twine::utohexstr(Obj.getBufSize()) + ")");
    return DynRegionInfo(ObjF, *this, Obj.base() + Offset, Size, EntSize);
  }

  void printAttributes(unsigned, std::unique_ptr<ELFAttributeParser>,
                       llvm::endianness);
  void printMipsReginfo();
  void printMipsOptions();

  std::pair<const Elf_Phdr *, const Elf_Shdr *> findDynamic();
  void loadDynamicTable();
  void parseDynamicTable();

  Expected<StringRef> getSymbolVersion(const Elf_Sym &Sym,
                                       bool &IsDefault) const;
  Expected<SmallVector<std::optional<VersionEntry>, 0> *> getVersionMap() const;

  DynRegionInfo DynRelRegion;
  DynRegionInfo DynRelaRegion;
  DynRegionInfo DynCrelRegion;
  DynRegionInfo DynRelrRegion;
  DynRegionInfo DynPLTRelRegion;
  std::optional<DynRegionInfo> DynSymRegion;
  DynRegionInfo DynSymTabShndxRegion;
  DynRegionInfo DynamicTable;
  StringRef DynamicStringTable;
  const Elf_Hash *HashTable = nullptr;
  const Elf_GnuHash *GnuHashTable = nullptr;
  const Elf_Shdr *DotSymtabSec = nullptr;
  const Elf_Shdr *DotDynsymSec = nullptr;
  const Elf_Shdr *DotAddrsigSec = nullptr;
  DenseMap<const Elf_Shdr *, ArrayRef<Elf_Word>> ShndxTables;
  std::optional<uint64_t> SONameOffset;
  std::optional<DenseMap<uint64_t, std::vector<uint32_t>>> AddressToIndexMap;

  const Elf_Shdr *SymbolVersionSection = nullptr;   // .gnu.version
  const Elf_Shdr *SymbolVersionNeedSection = nullptr; // .gnu.version_r
  const Elf_Shdr *SymbolVersionDefSection = nullptr; // .gnu.version_d

  std::string getFullSymbolName(const Elf_Sym &Symbol, unsigned SymIndex,
                                DataRegion<Elf_Word> ShndxTable,
                                std::optional<StringRef> StrTable,
                                bool IsDynamic) const;
  Expected<unsigned>
  getSymbolSectionIndex(const Elf_Sym &Symbol, unsigned SymIndex,
                        DataRegion<Elf_Word> ShndxTable) const;
  Expected<StringRef> getSymbolSectionName(const Elf_Sym &Symbol,
                                           unsigned SectionIndex) const;
  std::string getStaticSymbolName(uint32_t Index) const;
  StringRef getDynamicString(uint64_t Value) const;

  std::pair<Elf_Sym_Range, std::optional<StringRef>> getSymtabAndStrtab() const;
  void printSymbolsHelper(bool IsDynamic, bool ExtraSymInfo) const;
  std::string getDynamicEntry(uint64_t Type, uint64_t Value) const;

  Expected<RelSymbol<ELFT>> getRelocationTarget(const Relocation<ELFT> &R,
                                                const Elf_Shdr *SymTab) const;

  ArrayRef<Elf_Word> getShndxTable(const Elf_Shdr *Symtab) const;

private:
  mutable SmallVector<std::optional<VersionEntry>, 0> VersionMap;
};

template <class ELFT>
std::string ELFDumper<ELFT>::describe(const Elf_Shdr &Sec) const {
  return ::describe(Obj, Sec);
}

namespace {

template <class ELFT> struct SymtabLink {
  typename ELFT::SymRange Symbols;
  StringRef StringTable;
  const typename ELFT::Shdr *SymTab;
};

// Returns the linked symbol table, symbols and associated string table for a
// given section.
template <class ELFT>
Expected<SymtabLink<ELFT>> getLinkAsSymtab(const ELFFile<ELFT> &Obj,
                                           const typename ELFT::Shdr &Sec,
                                           unsigned ExpectedType) {
  Expected<const typename ELFT::Shdr *> SymtabOrErr =
      Obj.getSection(Sec.sh_link);
  if (!SymtabOrErr)
    return createError("invalid section linked to " + describe(Obj, Sec) +
                       ": " + toString(SymtabOrErr.takeError()));

  if ((*SymtabOrErr)->sh_type != ExpectedType)
    return createError(
        "invalid section linked to " + describe(Obj, Sec) + ": expected " +
        object::getELFSectionTypeName(Obj.getHeader().e_machine, ExpectedType) +
        ", but got " +
        object::getELFSectionTypeName(Obj.getHeader().e_machine,
                                      (*SymtabOrErr)->sh_type));

  Expected<StringRef> StrTabOrErr = Obj.getLinkAsStrtab(**SymtabOrErr);
  if (!StrTabOrErr)
    return createError(
        "can't get a string table for the symbol table linked to " +
        describe(Obj, Sec) + ": " + toString(StrTabOrErr.takeError()));

  Expected<typename ELFT::SymRange> SymsOrErr = Obj.symbols(*SymtabOrErr);
  if (!SymsOrErr)
    return createError("unable to read symbols from the " + describe(Obj, Sec) +
                       ": " + toString(SymsOrErr.takeError()));

  return SymtabLink<ELFT>{*SymsOrErr, *StrTabOrErr, *SymtabOrErr};
}

} // namespace

template <class ELFT>
Expected<ArrayRef<typename ELFT::Versym>>
ELFDumper<ELFT>::getVersionTable(const Elf_Shdr &Sec, ArrayRef<Elf_Sym> *SymTab,
                                 StringRef *StrTab,
                                 const Elf_Shdr **SymTabSec) const {
  assert((!SymTab && !StrTab && !SymTabSec) || (SymTab && StrTab && SymTabSec));
  if (reinterpret_cast<uintptr_t>(Obj.base() + Sec.sh_offset) %
          sizeof(uint16_t) !=
      0)
    return createError("the " + describe(Sec) + " is misaligned");

  Expected<ArrayRef<Elf_Versym>> VersionsOrErr =
      Obj.template getSectionContentsAsArray<Elf_Versym>(Sec);
  if (!VersionsOrErr)
    return createError("cannot read content of " + describe(Sec) + ": " +
                       toString(VersionsOrErr.takeError()));

  Expected<SymtabLink<ELFT>> SymTabOrErr =
      getLinkAsSymtab(Obj, Sec, SHT_DYNSYM);
  if (!SymTabOrErr) {
    reportUniqueWarning(SymTabOrErr.takeError());
    return *VersionsOrErr;
  }

  if (SymTabOrErr->Symbols.size() != VersionsOrErr->size())
    reportUniqueWarning(describe(Sec) + ": the number of entries (" +
                        Twine(VersionsOrErr->size()) +
                        ") does not match the number of symbols (" +
                        Twine(SymTabOrErr->Symbols.size()) +
                        ") in the symbol table with index " +
                        Twine(Sec.sh_link));

  if (SymTab) {
    *SymTab = SymTabOrErr->Symbols;
    *StrTab = SymTabOrErr->StringTable;
    *SymTabSec = SymTabOrErr->SymTab;
  }
  return *VersionsOrErr;
}

template <class ELFT>
std::pair<typename ELFDumper<ELFT>::Elf_Sym_Range, std::optional<StringRef>>
ELFDumper<ELFT>::getSymtabAndStrtab() const {
  assert(DotSymtabSec);
  Elf_Sym_Range Syms(nullptr, nullptr);
  std::optional<StringRef> StrTable;
  if (Expected<StringRef> StrTableOrErr =
          Obj.getStringTableForSymtab(*DotSymtabSec))
    StrTable = *StrTableOrErr;
  else
    reportUniqueWarning(
        "unable to get the string table for the SHT_SYMTAB section: " +
        toString(StrTableOrErr.takeError()));

  if (Expected<Elf_Sym_Range> SymsOrErr = Obj.symbols(DotSymtabSec))
    Syms = *SymsOrErr;
  else
    reportUniqueWarning("unable to read symbols from the SHT_SYMTAB section: " +
                        toString(SymsOrErr.takeError()));
  return {Syms, StrTable};
}

template <class ELFT>
void ELFDumper<ELFT>::printSymbolsHelper(bool IsDynamic,
                                         bool ExtraSymInfo) const {
  std::optional<StringRef> StrTable;
  size_t Entries = 0;
  Elf_Sym_Range Syms(nullptr, nullptr);
  const Elf_Shdr *SymtabSec = IsDynamic ? DotDynsymSec : DotSymtabSec;

  if (IsDynamic) {
    StrTable = DynamicStringTable;
    Syms = dynamic_symbols();
    Entries = Syms.size();
  } else if (DotSymtabSec) {
    std::tie(Syms, StrTable) = getSymtabAndStrtab();
    Entries = DotSymtabSec->getEntityCount();
  }
  if (Syms.empty())
    return;

  // The st_other field has 2 logical parts. The first two bits hold the symbol
  // visibility (STV_*) and the remainder hold other platform-specific values.
  bool NonVisibilityBitsUsed =
      llvm::any_of(Syms, [](const Elf_Sym &S) { return S.st_other & ~0x3; });

  DataRegion<Elf_Word> ShndxTable =
      IsDynamic ? DataRegion<Elf_Word>(
                      (const Elf_Word *)this->DynSymTabShndxRegion.Addr,
                      this->getElfObject().getELFFile().end())
                : DataRegion<Elf_Word>(this->getShndxTable(SymtabSec));

  printSymtabMessage(SymtabSec, Entries, NonVisibilityBitsUsed, ExtraSymInfo);
  for (const Elf_Sym &Sym : Syms)
    printSymbol(Sym, &Sym - Syms.begin(), ShndxTable, StrTable, IsDynamic,
                NonVisibilityBitsUsed, ExtraSymInfo);
}

template <typename ELFT> class GNUELFDumper : public ELFDumper<ELFT> {
  formatted_raw_ostream &OS;

public:
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

  GNUELFDumper(const object::ELFObjectFile<ELFT> &ObjF, ScopedPrinter &Writer)
      : ELFDumper<ELFT>(ObjF, Writer),
        OS(static_cast<formatted_raw_ostream &>(Writer.getOStream())) {
    assert(&this->W.getOStream() == &llvm::fouts());
  }

  void printFileSummary(StringRef FileStr, ObjectFile &Obj,
                        ArrayRef<std::string> InputFilenames,
                        const Archive *A) override;
  void printFileHeaders() override;
  void printGroupSections() override;
  void printRelocations() override;
  void printSectionHeaders() override;
  void printSymbols(bool PrintSymbols, bool PrintDynamicSymbols,
                    bool ExtraSymInfo) override;
  void printHashSymbols() override;
  void printSectionDetails() override;
  void printDependentLibs() override;
  void printDynamicTable() override;
  void printDynamicRelocations() override;
  void printSymtabMessage(const Elf_Shdr *Symtab, size_t Offset,
                          bool NonVisibilityBitsUsed,
                          bool ExtraSymInfo) const override;
  void printProgramHeaders(bool PrintProgramHeaders,
                           cl::boolOrDefault PrintSectionMapping) override;
  void printVersionSymbolSection(const Elf_Shdr *Sec) override;
  void printVersionDefinitionSection(const Elf_Shdr *Sec) override;
  void printVersionDependencySection(const Elf_Shdr *Sec) override;
  void printCGProfile() override;
  void printBBAddrMaps(bool PrettyPGOAnalysis) override;
  void printAddrsig() override;
  void printNotes() override;
  void printELFLinkerOptions() override;
  void printStackSizes() override;
  void printMemtag(
      const ArrayRef<std::pair<std::string, std::string>> DynamicEntries,
      const ArrayRef<uint8_t> AndroidNoteDesc,
      const ArrayRef<std::pair<uint64_t, uint64_t>> Descriptors) override;
  void printHashHistogramStats(size_t NBucket, size_t MaxChain,
                               size_t TotalSyms, ArrayRef<size_t> Count,
                               bool IsGnu) const override;

private:
  void printHashTableSymbols(const Elf_Hash &HashTable);
  void printGnuHashTableSymbols(const Elf_GnuHash &GnuHashTable);

  struct Field {
    std::string Str;
    unsigned Column;

    Field(StringRef S, unsigned Col) : Str(std::string(S)), Column(Col) {}
    Field(unsigned Col) : Column(Col) {}
  };

  template <typename T, typename TEnum>
  std::string printFlags(T Value, ArrayRef<EnumEntry<TEnum>> EnumValues,
                         TEnum EnumMask1 = {}, TEnum EnumMask2 = {},
                         TEnum EnumMask3 = {}) const {
    std::string Str;
    for (const EnumEntry<TEnum> &Flag : EnumValues) {
      if (Flag.Value == 0)
        continue;

      TEnum EnumMask{};
      if (Flag.Value & EnumMask1)
        EnumMask = EnumMask1;
      else if (Flag.Value & EnumMask2)
        EnumMask = EnumMask2;
      else if (Flag.Value & EnumMask3)
        EnumMask = EnumMask3;
      bool IsEnum = (Flag.Value & EnumMask) != 0;
      if ((!IsEnum && (Value & Flag.Value) == Flag.Value) ||
          (IsEnum && (Value & EnumMask) == Flag.Value)) {
        if (!Str.empty())
          Str += ", ";
        Str += Flag.AltName;
      }
    }
    return Str;
  }

  formatted_raw_ostream &printField(struct Field F) const {
    if (F.Column != 0)
      OS.PadToColumn(F.Column);
    OS << F.Str;
    OS.flush();
    return OS;
  }
  void printHashedSymbol(const Elf_Sym *Sym, unsigned SymIndex,
                         DataRegion<Elf_Word> ShndxTable, StringRef StrTable,
                         uint32_t Bucket);
  void printRelr(const Elf_Shdr &Sec);
  void printRelRelaReloc(const Relocation<ELFT> &R,
                         const RelSymbol<ELFT> &RelSym) override;
  void printSymbol(const Elf_Sym &Symbol, unsigned SymIndex,
                   DataRegion<Elf_Word> ShndxTable,
                   std::optional<StringRef> StrTable, bool IsDynamic,
                   bool NonVisibilityBitsUsed,
                   bool ExtraSymInfo) const override;
  void printDynamicRelocHeader(unsigned Type, StringRef Name,
                               const DynRegionInfo &Reg) override;

  std::string getSymbolSectionNdx(const Elf_Sym &Symbol, unsigned SymIndex,
                                  DataRegion<Elf_Word> ShndxTable,
                                  bool ExtraSymInfo = false) const;
  void printProgramHeaders() override;
  void printSectionMapping() override;
  void printGNUVersionSectionProlog(const typename ELFT::Shdr &Sec,
                                    const Twine &Label, unsigned EntriesNum);

  void printStackSizeEntry(uint64_t Size,
                           ArrayRef<std::string> FuncNames) override;

  void printMipsGOT(const MipsGOTParser<ELFT> &Parser) override;
  void printMipsPLT(const MipsGOTParser<ELFT> &Parser) override;
  void printMipsABIFlags() override;
};

template <typename ELFT> class LLVMELFDumper : public ELFDumper<ELFT> {
public:
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

  LLVMELFDumper(const object::ELFObjectFile<ELFT> &ObjF, ScopedPrinter &Writer)
      : ELFDumper<ELFT>(ObjF, Writer), W(Writer) {}

  void printFileHeaders() override;
  void printGroupSections() override;
  void printRelocations() override;
  void printSectionHeaders() override;
  void printSymbols(bool PrintSymbols, bool PrintDynamicSymbols,
                    bool ExtraSymInfo) override;
  void printDependentLibs() override;
  void printDynamicTable() override;
  void printDynamicRelocations() override;
  void printProgramHeaders(bool PrintProgramHeaders,
                           cl::boolOrDefault PrintSectionMapping) override;
  void printVersionSymbolSection(const Elf_Shdr *Sec) override;
  void printVersionDefinitionSection(const Elf_Shdr *Sec) override;
  void printVersionDependencySection(const Elf_Shdr *Sec) override;
  void printCGProfile() override;
  void printBBAddrMaps(bool PrettyPGOAnalysis) override;
  void printAddrsig() override;
  void printNotes() override;
  void printELFLinkerOptions() override;
  void printStackSizes() override;
  void printMemtag(
      const ArrayRef<std::pair<std::string, std::string>> DynamicEntries,
      const ArrayRef<uint8_t> AndroidNoteDesc,
      const ArrayRef<std::pair<uint64_t, uint64_t>> Descriptors) override;
  void printSymbolSection(const Elf_Sym &Symbol, unsigned SymIndex,
                          DataRegion<Elf_Word> ShndxTable) const;
  void printHashHistogramStats(size_t NBucket, size_t MaxChain,
                               size_t TotalSyms, ArrayRef<size_t> Count,
                               bool IsGnu) const override;

private:
  void printRelRelaReloc(const Relocation<ELFT> &R,
                         const RelSymbol<ELFT> &RelSym) override;

  void printSymbol(const Elf_Sym &Symbol, unsigned SymIndex,
                   DataRegion<Elf_Word> ShndxTable,
                   std::optional<StringRef> StrTable, bool IsDynamic,
                   bool /*NonVisibilityBitsUsed*/,
                   bool /*ExtraSymInfo*/) const override;
  void printProgramHeaders() override;
  void printSectionMapping() override {}
  void printStackSizeEntry(uint64_t Size,
                           ArrayRef<std::string> FuncNames) override;

  void printMipsGOT(const MipsGOTParser<ELFT> &Parser) override;
  void printMipsPLT(const MipsGOTParser<ELFT> &Parser) override;
  void printMipsABIFlags() override;
  virtual void printZeroSymbolOtherField(const Elf_Sym &Symbol) const;

protected:
  virtual std::string getGroupSectionHeaderName() const;
  void printSymbolOtherField(const Elf_Sym &Symbol) const;
  virtual void printExpandedRelRelaReloc(const Relocation<ELFT> &R,
                                         StringRef SymbolName,
                                         StringRef RelocName);
  virtual void printDefaultRelRelaReloc(const Relocation<ELFT> &R,
                                        StringRef SymbolName,
                                        StringRef RelocName);
  virtual void printRelocationSectionInfo(const Elf_Shdr &Sec, StringRef Name,
                                          const unsigned SecNdx);
  virtual void printSectionGroupMembers(StringRef Name, uint64_t Idx) const;
  virtual void printEmptyGroupMessage() const;

  ScopedPrinter &W;
};

// JSONELFDumper shares most of the same implementation as LLVMELFDumper except
// it uses a JSONScopedPrinter.
template <typename ELFT> class JSONELFDumper : public LLVMELFDumper<ELFT> {
public:
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

  JSONELFDumper(const object::ELFObjectFile<ELFT> &ObjF, ScopedPrinter &Writer)
      : LLVMELFDumper<ELFT>(ObjF, Writer) {}

  std::string getGroupSectionHeaderName() const override;

  void printFileSummary(StringRef FileStr, ObjectFile &Obj,
                        ArrayRef<std::string> InputFilenames,
                        const Archive *A) override;
  virtual void printZeroSymbolOtherField(const Elf_Sym &Symbol) const override;

  void printDefaultRelRelaReloc(const Relocation<ELFT> &R,
                                StringRef SymbolName,
                                StringRef RelocName) override;

  void printRelocationSectionInfo(const Elf_Shdr &Sec, StringRef Name,
                                  const unsigned SecNdx) override;

  void printSectionGroupMembers(StringRef Name, uint64_t Idx) const override;

  void printEmptyGroupMessage() const override;

  void printDynamicTable() override;

private:
  void printAuxillaryDynamicTableEntryInfo(const Elf_Dyn &Entry);

  std::unique_ptr<DictScope> FileScope;
};

} // end anonymous namespace

namespace llvm {

template <class ELFT>
static std::unique_ptr<ObjDumper>
createELFDumper(const ELFObjectFile<ELFT> &Obj, ScopedPrinter &Writer) {
  if (opts::Output == opts::GNU)
    return std::make_unique<GNUELFDumper<ELFT>>(Obj, Writer);
  else if (opts::Output == opts::JSON)
    return std::make_unique<JSONELFDumper<ELFT>>(Obj, Writer);
  return std::make_unique<LLVMELFDumper<ELFT>>(Obj, Writer);
}

std::unique_ptr<ObjDumper> createELFDumper(const object::ELFObjectFileBase &Obj,
                                           ScopedPrinter &Writer) {
  // Little-endian 32-bit
  if (const ELF32LEObjectFile *ELFObj = dyn_cast<ELF32LEObjectFile>(&Obj))
    return createELFDumper(*ELFObj, Writer);

  // Big-endian 32-bit
  if (const ELF32BEObjectFile *ELFObj = dyn_cast<ELF32BEObjectFile>(&Obj))
    return createELFDumper(*ELFObj, Writer);

  // Little-endian 64-bit
  if (const ELF64LEObjectFile *ELFObj = dyn_cast<ELF64LEObjectFile>(&Obj))
    return createELFDumper(*ELFObj, Writer);

  // Big-endian 64-bit
  return createELFDumper(*cast<ELF64BEObjectFile>(&Obj), Writer);
}

} // end namespace llvm

template <class ELFT>
Expected<SmallVector<std::optional<VersionEntry>, 0> *>
ELFDumper<ELFT>::getVersionMap() const {
  // If the VersionMap has already been loaded or if there is no dynamic symtab
  // or version table, there is nothing to do.
  if (!VersionMap.empty() || !DynSymRegion || !SymbolVersionSection)
    return &VersionMap;

  Expected<SmallVector<std::optional<VersionEntry>, 0>> MapOrErr =
      Obj.loadVersionMap(SymbolVersionNeedSection, SymbolVersionDefSection);
  if (MapOrErr)
    VersionMap = *MapOrErr;
  else
    return MapOrErr.takeError();

  return &VersionMap;
}

template <typename ELFT>
Expected<StringRef> ELFDumper<ELFT>::getSymbolVersion(const Elf_Sym &Sym,
                                                      bool &IsDefault) const {
  // This is a dynamic symbol. Look in the GNU symbol version table.
  if (!SymbolVersionSection) {
    // No version table.
    IsDefault = false;
    return "";
  }

  assert(DynSymRegion && "DynSymRegion has not been initialised");
  // Determine the position in the symbol table of this entry.
  size_t EntryIndex = (reinterpret_cast<uintptr_t>(&Sym) -
                       reinterpret_cast<uintptr_t>(DynSymRegion->Addr)) /
                      sizeof(Elf_Sym);

  // Get the corresponding version index entry.
  Expected<const Elf_Versym *> EntryOrErr =
      Obj.template getEntry<Elf_Versym>(*SymbolVersionSection, EntryIndex);
  if (!EntryOrErr)
    return EntryOrErr.takeError();

  unsigned Version = (*EntryOrErr)->vs_index;
  if (Version == VER_NDX_LOCAL || Version == VER_NDX_GLOBAL) {
    IsDefault = false;
    return "";
  }

  Expected<SmallVector<std::optional<VersionEntry>, 0> *> MapOrErr =
      getVersionMap();
  if (!MapOrErr)
    return MapOrErr.takeError();

  return Obj.getSymbolVersionByIndex(Version, IsDefault, **MapOrErr,
                                     Sym.st_shndx == ELF::SHN_UNDEF);
}

template <typename ELFT>
Expected<RelSymbol<ELFT>>
ELFDumper<ELFT>::getRelocationTarget(const Relocation<ELFT> &R,
                                     const Elf_Shdr *SymTab) const {
  if (R.Symbol == 0)
    return RelSymbol<ELFT>(nullptr, "");

  Expected<const Elf_Sym *> SymOrErr =
      Obj.template getEntry<Elf_Sym>(*SymTab, R.Symbol);
  if (!SymOrErr)
    return createError("unable to read an entry with index " + Twine(R.Symbol) +
                       " from " + describe(*SymTab) + ": " +
                       toString(SymOrErr.takeError()));
  const Elf_Sym *Sym = *SymOrErr;
  if (!Sym)
    return RelSymbol<ELFT>(nullptr, "");

  Expected<StringRef> StrTableOrErr = Obj.getStringTableForSymtab(*SymTab);
  if (!StrTableOrErr)
    return StrTableOrErr.takeError();

  const Elf_Sym *FirstSym =
      cantFail(Obj.template getEntry<Elf_Sym>(*SymTab, 0));
  std::string SymbolName =
      getFullSymbolName(*Sym, Sym - FirstSym, getShndxTable(SymTab),
                        *StrTableOrErr, SymTab->sh_type == SHT_DYNSYM);
  return RelSymbol<ELFT>(Sym, SymbolName);
}

template <typename ELFT>
ArrayRef<typename ELFT::Word>
ELFDumper<ELFT>::getShndxTable(const Elf_Shdr *Symtab) const {
  if (Symtab) {
    auto It = ShndxTables.find(Symtab);
    if (It != ShndxTables.end())
      return It->second;
  }
  return {};
}

static std::string maybeDemangle(StringRef Name) {
  return opts::Demangle ? demangle(Name) : Name.str();
}

template <typename ELFT>
std::string ELFDumper<ELFT>::getStaticSymbolName(uint32_t Index) const {
  auto Warn = [&](Error E) -> std::string {
    reportUniqueWarning("unable to read the name of symbol with index " +
                        Twine(Index) + ": " + toString(std::move(E)));
    return "<?>";
  };

  Expected<const typename ELFT::Sym *> SymOrErr =
      Obj.getSymbol(DotSymtabSec, Index);
  if (!SymOrErr)
    return Warn(SymOrErr.takeError());

  Expected<StringRef> StrTabOrErr = Obj.getStringTableForSymtab(*DotSymtabSec);
  if (!StrTabOrErr)
    return Warn(StrTabOrErr.takeError());

  Expected<StringRef> NameOrErr = (*SymOrErr)->getName(*StrTabOrErr);
  if (!NameOrErr)
    return Warn(NameOrErr.takeError());
  return maybeDemangle(*NameOrErr);
}

template <typename ELFT>
std::string ELFDumper<ELFT>::getFullSymbolName(
    const Elf_Sym &Symbol, unsigned SymIndex, DataRegion<Elf_Word> ShndxTable,
    std::optional<StringRef> StrTable, bool IsDynamic) const {
  if (!StrTable)
    return "<?>";

  std::string SymbolName;
  if (Expected<StringRef> NameOrErr = Symbol.getName(*StrTable)) {
    SymbolName = maybeDemangle(*NameOrErr);
  } else {
    reportUniqueWarning(NameOrErr.takeError());
    return "<?>";
  }

  if (SymbolName.empty() && Symbol.getType() == ELF::STT_SECTION) {
    Expected<unsigned> SectionIndex =
        getSymbolSectionIndex(Symbol, SymIndex, ShndxTable);
    if (!SectionIndex) {
      reportUniqueWarning(SectionIndex.takeError());
      return "<?>";
    }
    Expected<StringRef> NameOrErr = getSymbolSectionName(Symbol, *SectionIndex);
    if (!NameOrErr) {
      reportUniqueWarning(NameOrErr.takeError());
      return ("<section " + Twine(*SectionIndex) + ">").str();
    }
    return std::string(*NameOrErr);
  }

  if (!IsDynamic)
    return SymbolName;

  bool IsDefault;
  Expected<StringRef> VersionOrErr = getSymbolVersion(Symbol, IsDefault);
  if (!VersionOrErr) {
    reportUniqueWarning(VersionOrErr.takeError());
    return SymbolName + "@<corrupt>";
  }

  if (!VersionOrErr->empty()) {
    SymbolName += (IsDefault ? "@@" : "@");
    SymbolName += *VersionOrErr;
  }
  return SymbolName;
}

template <typename ELFT>
Expected<unsigned>
ELFDumper<ELFT>::getSymbolSectionIndex(const Elf_Sym &Symbol, unsigned SymIndex,
                                       DataRegion<Elf_Word> ShndxTable) const {
  unsigned Ndx = Symbol.st_shndx;
  if (Ndx == SHN_XINDEX)
    return object::getExtendedSymbolTableIndex<ELFT>(Symbol, SymIndex,
                                                     ShndxTable);
  if (Ndx != SHN_UNDEF && Ndx < SHN_LORESERVE)
    return Ndx;

  auto CreateErr = [&](const Twine &Name,
                       std::optional<unsigned> Offset = std::nullopt) {
    std::string Desc;
    if (Offset)
      Desc = (Name + "+0x" + Twine::utohexstr(*Offset)).str();
    else
      Desc = Name.str();
    return createError(
        "unable to get section index for symbol with st_shndx = 0x" +
        Twine::utohexstr(Ndx) + " (" + Desc + ")");
  };

  if (Ndx >= ELF::SHN_LOPROC && Ndx <= ELF::SHN_HIPROC)
    return CreateErr("SHN_LOPROC", Ndx - ELF::SHN_LOPROC);
  if (Ndx >= ELF::SHN_LOOS && Ndx <= ELF::SHN_HIOS)
    return CreateErr("SHN_LOOS", Ndx - ELF::SHN_LOOS);
  if (Ndx == ELF::SHN_UNDEF)
    return CreateErr("SHN_UNDEF");
  if (Ndx == ELF::SHN_ABS)
    return CreateErr("SHN_ABS");
  if (Ndx == ELF::SHN_COMMON)
    return CreateErr("SHN_COMMON");
  return CreateErr("SHN_LORESERVE", Ndx - SHN_LORESERVE);
}

template <typename ELFT>
Expected<StringRef>
ELFDumper<ELFT>::getSymbolSectionName(const Elf_Sym &Symbol,
                                      unsigned SectionIndex) const {
  Expected<const Elf_Shdr *> SecOrErr = Obj.getSection(SectionIndex);
  if (!SecOrErr)
    return SecOrErr.takeError();
  return Obj.getSectionName(**SecOrErr);
}

template <class ELFO>
static const typename ELFO::Elf_Shdr *
findNotEmptySectionByAddress(const ELFO &Obj, StringRef FileName,
                             uint64_t Addr) {
  for (const typename ELFO::Elf_Shdr &Shdr : cantFail(Obj.sections()))
    if (Shdr.sh_addr == Addr && Shdr.sh_size > 0)
      return &Shdr;
  return nullptr;
}

const EnumEntry<unsigned> ElfClass[] = {
  {"None",   "none",   ELF::ELFCLASSNONE},
  {"32-bit", "ELF32",  ELF::ELFCLASS32},
  {"64-bit", "ELF64",  ELF::ELFCLASS64},
};

const EnumEntry<unsigned> ElfDataEncoding[] = {
  {"None",         "none",                          ELF::ELFDATANONE},
  {"LittleEndian", "2's complement, little endian", ELF::ELFDATA2LSB},
  {"BigEndian",    "2's complement, big endian",    ELF::ELFDATA2MSB},
};

const EnumEntry<unsigned> ElfObjectFileType[] = {
  {"None",         "NONE (none)",              ELF::ET_NONE},
  {"Relocatable",  "REL (Relocatable file)",   ELF::ET_REL},
  {"Executable",   "EXEC (Executable file)",   ELF::ET_EXEC},
  {"SharedObject", "DYN (Shared object file)", ELF::ET_DYN},
  {"Core",         "CORE (Core file)",         ELF::ET_CORE},
};

const EnumEntry<unsigned> ElfOSABI[] = {
  {"SystemV",      "UNIX - System V",      ELF::ELFOSABI_NONE},
  {"HPUX",         "UNIX - HP-UX",         ELF::ELFOSABI_HPUX},
  {"NetBSD",       "UNIX - NetBSD",        ELF::ELFOSABI_NETBSD},
  {"GNU/Linux",    "UNIX - GNU",           ELF::ELFOSABI_LINUX},
  {"GNU/Hurd",     "GNU/Hurd",             ELF::ELFOSABI_HURD},
  {"Solaris",      "UNIX - Solaris",       ELF::ELFOSABI_SOLARIS},
  {"AIX",          "UNIX - AIX",           ELF::ELFOSABI_AIX},
  {"IRIX",         "UNIX - IRIX",          ELF::ELFOSABI_IRIX},
  {"FreeBSD",      "UNIX - FreeBSD",       ELF::ELFOSABI_FREEBSD},
  {"TRU64",        "UNIX - TRU64",         ELF::ELFOSABI_TRU64},
  {"Modesto",      "Novell - Modesto",     ELF::ELFOSABI_MODESTO},
  {"OpenBSD",      "UNIX - OpenBSD",       ELF::ELFOSABI_OPENBSD},
  {"OpenVMS",      "VMS - OpenVMS",        ELF::ELFOSABI_OPENVMS},
  {"NSK",          "HP - Non-Stop Kernel", ELF::ELFOSABI_NSK},
  {"AROS",         "AROS",                 ELF::ELFOSABI_AROS},
  {"FenixOS",      "FenixOS",              ELF::ELFOSABI_FENIXOS},
  {"CloudABI",     "CloudABI",             ELF::ELFOSABI_CLOUDABI},
  {"CUDA",         "NVIDIA - CUDA",        ELF::ELFOSABI_CUDA},
  {"Standalone",   "Standalone App",       ELF::ELFOSABI_STANDALONE}
};

const EnumEntry<unsigned> AMDGPUElfOSABI[] = {
  {"AMDGPU_HSA",    "AMDGPU - HSA",    ELF::ELFOSABI_AMDGPU_HSA},
  {"AMDGPU_PAL",    "AMDGPU - PAL",    ELF::ELFOSABI_AMDGPU_PAL},
  {"AMDGPU_MESA3D", "AMDGPU - MESA3D", ELF::ELFOSABI_AMDGPU_MESA3D}
};

const EnumEntry<unsigned> ARMElfOSABI[] = {
    {"ARM", "ARM", ELF::ELFOSABI_ARM},
    {"ARM FDPIC", "ARM FDPIC", ELF::ELFOSABI_ARM_FDPIC},
};

const EnumEntry<unsigned> C6000ElfOSABI[] = {
  {"C6000_ELFABI", "Bare-metal C6000", ELF::ELFOSABI_C6000_ELFABI},
  {"C6000_LINUX",  "Linux C6000",      ELF::ELFOSABI_C6000_LINUX}
};

const EnumEntry<unsigned> ElfMachineType[] = {
  ENUM_ENT(EM_NONE,          "None"),
  ENUM_ENT(EM_M32,           "WE32100"),
  ENUM_ENT(EM_SPARC,         "Sparc"),
  ENUM_ENT(EM_386,           "Intel 80386"),
  ENUM_ENT(EM_68K,           "MC68000"),
  ENUM_ENT(EM_88K,           "MC88000"),
  ENUM_ENT(EM_IAMCU,         "EM_IAMCU"),
  ENUM_ENT(EM_860,           "Intel 80860"),
  ENUM_ENT(EM_MIPS,          "MIPS R3000"),
  ENUM_ENT(EM_S370,          "IBM System/370"),
  ENUM_ENT(EM_MIPS_RS3_LE,   "MIPS R3000 little-endian"),
  ENUM_ENT(EM_PARISC,        "HPPA"),
  ENUM_ENT(EM_VPP500,        "Fujitsu VPP500"),
  ENUM_ENT(EM_SPARC32PLUS,   "Sparc v8+"),
  ENUM_ENT(EM_960,           "Intel 80960"),
  ENUM_ENT(EM_PPC,           "PowerPC"),
  ENUM_ENT(EM_PPC64,         "PowerPC64"),
  ENUM_ENT(EM_S390,          "IBM S/390"),
  ENUM_ENT(EM_SPU,           "SPU"),
  ENUM_ENT(EM_V800,          "NEC V800 series"),
  ENUM_ENT(EM_FR20,          "Fujistsu FR20"),
  ENUM_ENT(EM_RH32,          "TRW RH-32"),
  ENUM_ENT(EM_RCE,           "Motorola RCE"),
  ENUM_ENT(EM_ARM,           "ARM"),
  ENUM_ENT(EM_ALPHA,         "EM_ALPHA"),
  ENUM_ENT(EM_SH,            "Hitachi SH"),
  ENUM_ENT(EM_SPARCV9,       "Sparc v9"),
  ENUM_ENT(EM_TRICORE,       "Siemens Tricore"),
  ENUM_ENT(EM_ARC,           "ARC"),
  ENUM_ENT(EM_H8_300,        "Hitachi H8/300"),
  ENUM_ENT(EM_H8_300H,       "Hitachi H8/300H"),
  ENUM_ENT(EM_H8S,           "Hitachi H8S"),
  ENUM_ENT(EM_H8_500,        "Hitachi H8/500"),
  ENUM_ENT(EM_IA_64,         "Intel IA-64"),
  ENUM_ENT(EM_MIPS_X,        "Stanford MIPS-X"),
  ENUM_ENT(EM_COLDFIRE,      "Motorola Coldfire"),
  ENUM_ENT(EM_68HC12,        "Motorola MC68HC12 Microcontroller"),
  ENUM_ENT(EM_MMA,           "Fujitsu Multimedia Accelerator"),
  ENUM_ENT(EM_PCP,           "Siemens PCP"),
  ENUM_ENT(EM_NCPU,          "Sony nCPU embedded RISC processor"),
  ENUM_ENT(EM_NDR1,          "Denso NDR1 microprocesspr"),
  ENUM_ENT(EM_STARCORE,      "Motorola Star*Core processor"),
  ENUM_ENT(EM_ME16,          "Toyota ME16 processor"),
  ENUM_ENT(EM_ST100,         "STMicroelectronics ST100 processor"),
  ENUM_ENT(EM_TINYJ,         "Advanced Logic Corp. TinyJ embedded processor"),
  ENUM_ENT(EM_X86_64,        "Advanced Micro Devices X86-64"),
  ENUM_ENT(EM_PDSP,          "Sony DSP processor"),
  ENUM_ENT(EM_PDP10,         "Digital Equipment Corp. PDP-10"),
  ENUM_ENT(EM_PDP11,         "Digital Equipment Corp. PDP-11"),
  ENUM_ENT(EM_FX66,          "Siemens FX66 microcontroller"),
  ENUM_ENT(EM_ST9PLUS,       "STMicroelectronics ST9+ 8/16 bit microcontroller"),
  ENUM_ENT(EM_ST7,           "STMicroelectronics ST7 8-bit microcontroller"),
  ENUM_ENT(EM_68HC16,        "Motorola MC68HC16 Microcontroller"),
  ENUM_ENT(EM_68HC11,        "Motorola MC68HC11 Microcontroller"),
  ENUM_ENT(EM_68HC08,        "Motorola MC68HC08 Microcontroller"),
  ENUM_ENT(EM_68HC05,        "Motorola MC68HC05 Microcontroller"),
  ENUM_ENT(EM_SVX,           "Silicon Graphics SVx"),
  ENUM_ENT(EM_ST19,          "STMicroelectronics ST19 8-bit microcontroller"),
  ENUM_ENT(EM_VAX,           "Digital VAX"),
  ENUM_ENT(EM_CRIS,          "Axis Communications 32-bit embedded processor"),
  ENUM_ENT(EM_JAVELIN,       "Infineon Technologies 32-bit embedded cpu"),
  ENUM_ENT(EM_FIREPATH,      "Element 14 64-bit DSP processor"),
  ENUM_ENT(EM_ZSP,           "LSI Logic's 16-bit DSP processor"),
  ENUM_ENT(EM_MMIX,          "Donald Knuth's educational 64-bit processor"),
  ENUM_ENT(EM_HUANY,         "Harvard Universitys's machine-independent object format"),
  ENUM_ENT(EM_PRISM,         "Vitesse Prism"),
  ENUM_ENT(EM_AVR,           "Atmel AVR 8-bit microcontroller"),
  ENUM_ENT(EM_FR30,          "Fujitsu FR30"),
  ENUM_ENT(EM_D10V,          "Mitsubishi D10V"),
  ENUM_ENT(EM_D30V,          "Mitsubishi D30V"),
  ENUM_ENT(EM_V850,          "NEC v850"),
  ENUM_ENT(EM_M32R,          "Renesas M32R (formerly Mitsubishi M32r)"),
  ENUM_ENT(EM_MN10300,       "Matsushita MN10300"),
  ENUM_ENT(EM_MN10200,       "Matsushita MN10200"),
  ENUM_ENT(EM_PJ,            "picoJava"),
  ENUM_ENT(EM_OPENRISC,      "OpenRISC 32-bit embedded processor"),
  ENUM_ENT(EM_ARC_COMPACT,   "EM_ARC_COMPACT"),
  ENUM_ENT(EM_XTENSA,        "Tensilica Xtensa Processor"),
  ENUM_ENT(EM_VIDEOCORE,     "Alphamosaic VideoCore processor"),
  ENUM_ENT(EM_TMM_GPP,       "Thompson Multimedia General Purpose Processor"),
  ENUM_ENT(EM_NS32K,         "National Semiconductor 32000 series"),
  ENUM_ENT(EM_TPC,           "Tenor Network TPC processor"),
  ENUM_ENT(EM_SNP1K,         "EM_SNP1K"),
  ENUM_ENT(EM_ST200,         "STMicroelectronics ST200 microcontroller"),
  ENUM_ENT(EM_IP2K,          "Ubicom IP2xxx 8-bit microcontrollers"),
  ENUM_ENT(EM_MAX,           "MAX Processor"),
  ENUM_ENT(EM_CR,            "National Semiconductor CompactRISC"),
  ENUM_ENT(EM_F2MC16,        "Fujitsu F2MC16"),
  ENUM_ENT(EM_MSP430,        "Texas Instruments msp430 microcontroller"),
  ENUM_ENT(EM_BLACKFIN,      "Analog Devices Blackfin"),
  ENUM_ENT(EM_SE_C33,        "S1C33 Family of Seiko Epson processors"),
  ENUM_ENT(EM_SEP,           "Sharp embedded microprocessor"),
  ENUM_ENT(EM_ARCA,          "Arca RISC microprocessor"),
  ENUM_ENT(EM_UNICORE,       "Unicore"),
  ENUM_ENT(EM_EXCESS,        "eXcess 16/32/64-bit configurable embedded CPU"),
  ENUM_ENT(EM_DXP,           "Icera Semiconductor Inc. Deep Execution Processor"),
  ENUM_ENT(EM_ALTERA_NIOS2,  "Altera Nios"),
  ENUM_ENT(EM_CRX,           "National Semiconductor CRX microprocessor"),
  ENUM_ENT(EM_XGATE,         "Motorola XGATE embedded processor"),
  ENUM_ENT(EM_C166,          "Infineon Technologies xc16x"),
  ENUM_ENT(EM_M16C,          "Renesas M16C"),
  ENUM_ENT(EM_DSPIC30F,      "Microchip Technology dsPIC30F Digital Signal Controller"),
  ENUM_ENT(EM_CE,            "Freescale Communication Engine RISC core"),
  ENUM_ENT(EM_M32C,          "Renesas M32C"),
  ENUM_ENT(EM_TSK3000,       "Altium TSK3000 core"),
  ENUM_ENT(EM_RS08,          "Freescale RS08 embedded processor"),
  ENUM_ENT(EM_SHARC,         "EM_SHARC"),
  ENUM_ENT(EM_ECOG2,         "Cyan Technology eCOG2 microprocessor"),
  ENUM_ENT(EM_SCORE7,        "SUNPLUS S+Core"),
  ENUM_ENT(EM_DSP24,         "New Japan Radio (NJR) 24-bit DSP Processor"),
  ENUM_ENT(EM_VIDEOCORE3,    "Broadcom VideoCore III processor"),
  ENUM_ENT(EM_LATTICEMICO32, "Lattice Mico32"),
  ENUM_ENT(EM_SE_C17,        "Seiko Epson C17 family"),
  ENUM_ENT(EM_TI_C6000,      "Texas Instruments TMS320C6000 DSP family"),
  ENUM_ENT(EM_TI_C2000,      "Texas Instruments TMS320C2000 DSP family"),
  ENUM_ENT(EM_TI_C5500,      "Texas Instruments TMS320C55x DSP family"),
  ENUM_ENT(EM_MMDSP_PLUS,    "STMicroelectronics 64bit VLIW Data Signal Processor"),
  ENUM_ENT(EM_CYPRESS_M8C,   "Cypress M8C microprocessor"),
  ENUM_ENT(EM_R32C,          "Renesas R32C series microprocessors"),
  ENUM_ENT(EM_TRIMEDIA,      "NXP Semiconductors TriMedia architecture family"),
  ENUM_ENT(EM_HEXAGON,       "Qualcomm Hexagon"),
  ENUM_ENT(EM_8051,          "Intel 8051 and variants"),
  ENUM_ENT(EM_STXP7X,        "STMicroelectronics STxP7x family"),
  ENUM_ENT(EM_NDS32,         "Andes Technology compact code size embedded RISC processor family"),
  ENUM_ENT(EM_ECOG1,         "Cyan Technology eCOG1 microprocessor"),
  // FIXME: Following EM_ECOG1X definitions is dead code since EM_ECOG1X has
  //        an identical number to EM_ECOG1.
  ENUM_ENT(EM_ECOG1X,        "Cyan Technology eCOG1X family"),
  ENUM_ENT(EM_MAXQ30,        "Dallas Semiconductor MAXQ30 Core microcontrollers"),
  ENUM_ENT(EM_XIMO16,        "New Japan Radio (NJR) 16-bit DSP Processor"),
  ENUM_ENT(EM_MANIK,         "M2000 Reconfigurable RISC Microprocessor"),
  ENUM_ENT(EM_CRAYNV2,       "Cray Inc. NV2 vector architecture"),
  ENUM_ENT(EM_RX,            "Renesas RX"),
  ENUM_ENT(EM_METAG,         "Imagination Technologies Meta processor architecture"),
  ENUM_ENT(EM_MCST_ELBRUS,   "MCST Elbrus general purpose hardware architecture"),
  ENUM_ENT(EM_ECOG16,        "Cyan Technology eCOG16 family"),
  ENUM_ENT(EM_CR16,          "National Semiconductor CompactRISC 16-bit processor"),
  ENUM_ENT(EM_ETPU,          "Freescale Extended Time Processing Unit"),
  ENUM_ENT(EM_SLE9X,         "Infineon Technologies SLE9X core"),
  ENUM_ENT(EM_L10M,          "EM_L10M"),
  ENUM_ENT(EM_K10M,          "EM_K10M"),
  ENUM_ENT(EM_AARCH64,       "AArch64"),
  ENUM_ENT(EM_AVR32,         "Atmel Corporation 32-bit microprocessor family"),
  ENUM_ENT(EM_STM8,          "STMicroeletronics STM8 8-bit microcontroller"),
  ENUM_ENT(EM_TILE64,        "Tilera TILE64 multicore architecture family"),
  ENUM_ENT(EM_TILEPRO,       "Tilera TILEPro multicore architecture family"),
  ENUM_ENT(EM_MICROBLAZE,    "Xilinx MicroBlaze 32-bit RISC soft processor core"),
  ENUM_ENT(EM_CUDA,          "NVIDIA CUDA architecture"),
  ENUM_ENT(EM_TILEGX,        "Tilera TILE-Gx multicore architecture family"),
  ENUM_ENT(EM_CLOUDSHIELD,   "EM_CLOUDSHIELD"),
  ENUM_ENT(EM_COREA_1ST,     "EM_COREA_1ST"),
  ENUM_ENT(EM_COREA_2ND,     "EM_COREA_2ND"),
  ENUM_ENT(EM_ARC_COMPACT2,  "EM_ARC_COMPACT2"),
  ENUM_ENT(EM_OPEN8,         "EM_OPEN8"),
  ENUM_ENT(EM_RL78,          "Renesas RL78"),
  ENUM_ENT(EM_VIDEOCORE5,    "Broadcom VideoCore V processor"),
  ENUM_ENT(EM_78KOR,         "EM_78KOR"),
  ENUM_ENT(EM_56800EX,       "EM_56800EX"),
  ENUM_ENT(EM_AMDGPU,        "EM_AMDGPU"),
  ENUM_ENT(EM_RISCV,         "RISC-V"),
  ENUM_ENT(EM_LANAI,         "EM_LANAI"),
  ENUM_ENT(EM_BPF,           "EM_BPF"),
  ENUM_ENT(EM_VE,            "NEC SX-Aurora Vector Engine"),
  ENUM_ENT(EM_LOONGARCH,     "LoongArch"),
};

const EnumEntry<unsigned> ElfSymbolBindings[] = {
    {"Local",  "LOCAL",  ELF::STB_LOCAL},
    {"Global", "GLOBAL", ELF::STB_GLOBAL},
    {"Weak",   "WEAK",   ELF::STB_WEAK},
    {"Unique", "UNIQUE", ELF::STB_GNU_UNIQUE}};

const EnumEntry<unsigned> ElfSymbolVisibilities[] = {
    {"DEFAULT",   "DEFAULT",   ELF::STV_DEFAULT},
    {"INTERNAL",  "INTERNAL",  ELF::STV_INTERNAL},
    {"HIDDEN",    "HIDDEN",    ELF::STV_HIDDEN},
    {"PROTECTED", "PROTECTED", ELF::STV_PROTECTED}};

const EnumEntry<unsigned> AMDGPUSymbolTypes[] = {
  { "AMDGPU_HSA_KERNEL",            ELF::STT_AMDGPU_HSA_KERNEL }
};

static const char *getGroupType(uint32_t Flag) {
  if (Flag & ELF::GRP_COMDAT)
    return "COMDAT";
  else
    return "(unknown)";
}

const EnumEntry<unsigned> ElfSectionFlags[] = {
  ENUM_ENT(SHF_WRITE,            "W"),
  ENUM_ENT(SHF_ALLOC,            "A"),
  ENUM_ENT(SHF_EXECINSTR,        "X"),
  ENUM_ENT(SHF_MERGE,            "M"),
  ENUM_ENT(SHF_STRINGS,          "S"),
  ENUM_ENT(SHF_INFO_LINK,        "I"),
  ENUM_ENT(SHF_LINK_ORDER,       "L"),
  ENUM_ENT(SHF_OS_NONCONFORMING, "O"),
  ENUM_ENT(SHF_GROUP,            "G"),
  ENUM_ENT(SHF_TLS,              "T"),
  ENUM_ENT(SHF_COMPRESSED,       "C"),
  ENUM_ENT(SHF_EXCLUDE,          "E"),
};

const EnumEntry<unsigned> ElfGNUSectionFlags[] = {
  ENUM_ENT(SHF_GNU_RETAIN, "R")
};

const EnumEntry<unsigned> ElfSolarisSectionFlags[] = {
  ENUM_ENT(SHF_SUNW_NODISCARD, "R")
};

const EnumEntry<unsigned> ElfXCoreSectionFlags[] = {
  ENUM_ENT(XCORE_SHF_CP_SECTION, ""),
  ENUM_ENT(XCORE_SHF_DP_SECTION, "")
};

const EnumEntry<unsigned> ElfARMSectionFlags[] = {
  ENUM_ENT(SHF_ARM_PURECODE, "y")
};

const EnumEntry<unsigned> ElfHexagonSectionFlags[] = {
  ENUM_ENT(SHF_HEX_GPREL, "")
};

const EnumEntry<unsigned> ElfMipsSectionFlags[] = {
  ENUM_ENT(SHF_MIPS_NODUPES, ""),
  ENUM_ENT(SHF_MIPS_NAMES,   ""),
  ENUM_ENT(SHF_MIPS_LOCAL,   ""),
  ENUM_ENT(SHF_MIPS_NOSTRIP, ""),
  ENUM_ENT(SHF_MIPS_GPREL,   ""),
  ENUM_ENT(SHF_MIPS_MERGE,   ""),
  ENUM_ENT(SHF_MIPS_ADDR,    ""),
  ENUM_ENT(SHF_MIPS_STRING,  "")
};

const EnumEntry<unsigned> ElfX86_64SectionFlags[] = {
  ENUM_ENT(SHF_X86_64_LARGE, "l")
};

static std::vector<EnumEntry<unsigned>>
getSectionFlagsForTarget(unsigned EOSAbi, unsigned EMachine) {
  std::vector<EnumEntry<unsigned>> Ret(std::begin(ElfSectionFlags),
                                       std::end(ElfSectionFlags));
  switch (EOSAbi) {
  case ELFOSABI_SOLARIS:
    Ret.insert(Ret.end(), std::begin(ElfSolarisSectionFlags),
               std::end(ElfSolarisSectionFlags));
    break;
  default:
    Ret.insert(Ret.end(), std::begin(ElfGNUSectionFlags),
               std::end(ElfGNUSectionFlags));
    break;
  }
  switch (EMachine) {
  case EM_ARM:
    Ret.insert(Ret.end(), std::begin(ElfARMSectionFlags),
               std::end(ElfARMSectionFlags));
    break;
  case EM_HEXAGON:
    Ret.insert(Ret.end(), std::begin(ElfHexagonSectionFlags),
               std::end(ElfHexagonSectionFlags));
    break;
  case EM_MIPS:
    Ret.insert(Ret.end(), std::begin(ElfMipsSectionFlags),
               std::end(ElfMipsSectionFlags));
    break;
  case EM_X86_64:
    Ret.insert(Ret.end(), std::begin(ElfX86_64SectionFlags),
               std::end(ElfX86_64SectionFlags));
    break;
  case EM_XCORE:
    Ret.insert(Ret.end(), std::begin(ElfXCoreSectionFlags),
               std::end(ElfXCoreSectionFlags));
    break;
  default:
    break;
  }
  return Ret;
}

static std::string getGNUFlags(unsigned EOSAbi, unsigned EMachine,
                               uint64_t Flags) {
  // Here we are trying to build the flags string in the same way as GNU does.
  // It is not that straightforward. Imagine we have sh_flags == 0x90000000.
  // SHF_EXCLUDE ("E") has a value of 0x80000000 and SHF_MASKPROC is 0xf0000000.
  // GNU readelf will not print "E" or "Ep" in this case, but will print just
  // "p". It only will print "E" when no other processor flag is set.
  std::string Str;
  bool HasUnknownFlag = false;
  bool HasOSFlag = false;
  bool HasProcFlag = false;
  std::vector<EnumEntry<unsigned>> FlagsList =
      getSectionFlagsForTarget(EOSAbi, EMachine);
  while (Flags) {
    // Take the least significant bit as a flag.
    uint64_t Flag = Flags & -Flags;
    Flags -= Flag;

    // Find the flag in the known flags list.
    auto I = llvm::find_if(FlagsList, [=](const EnumEntry<unsigned> &E) {
      // Flags with empty names are not printed in GNU style output.
      return E.Value == Flag && !E.AltName.empty();
    });
    if (I != FlagsList.end()) {
      Str += I->AltName;
      continue;
    }

    // If we did not find a matching regular flag, then we deal with an OS
    // specific flag, processor specific flag or an unknown flag.
    if (Flag & ELF::SHF_MASKOS) {
      HasOSFlag = true;
      Flags &= ~ELF::SHF_MASKOS;
    } else if (Flag & ELF::SHF_MASKPROC) {
      HasProcFlag = true;
      // Mask off all the processor-specific bits. This removes the SHF_EXCLUDE
      // bit if set so that it doesn't also get printed.
      Flags &= ~ELF::SHF_MASKPROC;
    } else {
      HasUnknownFlag = true;
    }
  }

  // "o", "p" and "x" are printed last.
  if (HasOSFlag)
    Str += "o";
  if (HasProcFlag)
    Str += "p";
  if (HasUnknownFlag)
    Str += "x";
  return Str;
}

static StringRef segmentTypeToString(unsigned Arch, unsigned Type) {
  // Check potentially overlapped processor-specific program header type.
  switch (Arch) {
  case ELF::EM_ARM:
    switch (Type) { LLVM_READOBJ_ENUM_CASE(ELF, PT_ARM_EXIDX); }
    break;
  case ELF::EM_MIPS:
  case ELF::EM_MIPS_RS3_LE:
    switch (Type) {
      LLVM_READOBJ_ENUM_CASE(ELF, PT_MIPS_REGINFO);
      LLVM_READOBJ_ENUM_CASE(ELF, PT_MIPS_RTPROC);
      LLVM_READOBJ_ENUM_CASE(ELF, PT_MIPS_OPTIONS);
      LLVM_READOBJ_ENUM_CASE(ELF, PT_MIPS_ABIFLAGS);
    }
    break;
  case ELF::EM_RISCV:
    switch (Type) { LLVM_READOBJ_ENUM_CASE(ELF, PT_RISCV_ATTRIBUTES); }
  }

  switch (Type) {
    LLVM_READOBJ_ENUM_CASE(ELF, PT_NULL);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_LOAD);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_DYNAMIC);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_INTERP);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_NOTE);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_SHLIB);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_PHDR);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_TLS);

    LLVM_READOBJ_ENUM_CASE(ELF, PT_GNU_EH_FRAME);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_SUNW_UNWIND);

    LLVM_READOBJ_ENUM_CASE(ELF, PT_GNU_STACK);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_GNU_RELRO);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_GNU_PROPERTY);

    LLVM_READOBJ_ENUM_CASE(ELF, PT_OPENBSD_MUTABLE);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_OPENBSD_RANDOMIZE);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_OPENBSD_WXNEEDED);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_OPENBSD_NOBTCFI);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_OPENBSD_SYSCALLS);
    LLVM_READOBJ_ENUM_CASE(ELF, PT_OPENBSD_BOOTDATA);
  default:
    return "";
  }
}

static std::string getGNUPtType(unsigned Arch, unsigned Type) {
  StringRef Seg = segmentTypeToString(Arch, Type);
  if (Seg.empty())
    return std::string("<unknown>: ") + to_string(format_hex(Type, 1));

  // E.g. "PT_ARM_EXIDX" -> "EXIDX".
  if (Seg.consume_front("PT_ARM_"))
    return Seg.str();

  // E.g. "PT_MIPS_REGINFO" -> "REGINFO".
  if (Seg.consume_front("PT_MIPS_"))
    return Seg.str();

  // E.g. "PT_RISCV_ATTRIBUTES"
  if (Seg.consume_front("PT_RISCV_"))
    return Seg.str();

  // E.g. "PT_LOAD" -> "LOAD".
  assert(Seg.starts_with("PT_"));
  return Seg.drop_front(3).str();
}

const EnumEntry<unsigned> ElfSegmentFlags[] = {
  LLVM_READOBJ_ENUM_ENT(ELF, PF_X),
  LLVM_READOBJ_ENUM_ENT(ELF, PF_W),
  LLVM_READOBJ_ENUM_ENT(ELF, PF_R)
};

const EnumEntry<unsigned> ElfHeaderMipsFlags[] = {
  ENUM_ENT(EF_MIPS_NOREORDER, "noreorder"),
  ENUM_ENT(EF_MIPS_PIC, "pic"),
  ENUM_ENT(EF_MIPS_CPIC, "cpic"),
  ENUM_ENT(EF_MIPS_ABI2, "abi2"),
  ENUM_ENT(EF_MIPS_32BITMODE, "32bitmode"),
  ENUM_ENT(EF_MIPS_FP64, "fp64"),
  ENUM_ENT(EF_MIPS_NAN2008, "nan2008"),
  ENUM_ENT(EF_MIPS_ABI_O32, "o32"),
  ENUM_ENT(EF_MIPS_ABI_O64, "o64"),
  ENUM_ENT(EF_MIPS_ABI_EABI32, "eabi32"),
  ENUM_ENT(EF_MIPS_ABI_EABI64, "eabi64"),
  ENUM_ENT(EF_MIPS_MACH_3900, "3900"),
  ENUM_ENT(EF_MIPS_MACH_4010, "4010"),
  ENUM_ENT(EF_MIPS_MACH_4100, "4100"),
  ENUM_ENT(EF_MIPS_MACH_4650, "4650"),
  ENUM_ENT(EF_MIPS_MACH_4120, "4120"),
  ENUM_ENT(EF_MIPS_MACH_4111, "4111"),
  ENUM_ENT(EF_MIPS_MACH_SB1, "sb1"),
  ENUM_ENT(EF_MIPS_MACH_OCTEON, "octeon"),
  ENUM_ENT(EF_MIPS_MACH_XLR, "xlr"),
  ENUM_ENT(EF_MIPS_MACH_OCTEON2, "octeon2"),
  ENUM_ENT(EF_MIPS_MACH_OCTEON3, "octeon3"),
  ENUM_ENT(EF_MIPS_MACH_5400, "5400"),
  ENUM_ENT(EF_MIPS_MACH_5900, "5900"),
  ENUM_ENT(EF_MIPS_MACH_5500, "5500"),
  ENUM_ENT(EF_MIPS_MACH_9000, "9000"),
  ENUM_ENT(EF_MIPS_MACH_LS2E, "loongson-2e"),
  ENUM_ENT(EF_MIPS_MACH_LS2F, "loongson-2f"),
  ENUM_ENT(EF_MIPS_MACH_LS3A, "loongson-3a"),
  ENUM_ENT(EF_MIPS_MICROMIPS, "micromips"),
  ENUM_ENT(EF_MIPS_ARCH_ASE_M16, "mips16"),
  ENUM_ENT(EF_MIPS_ARCH_ASE_MDMX, "mdmx"),
  ENUM_ENT(EF_MIPS_ARCH_1, "mips1"),
  ENUM_ENT(EF_MIPS_ARCH_2, "mips2"),
  ENUM_ENT(EF_MIPS_ARCH_3, "mips3"),
  ENUM_ENT(EF_MIPS_ARCH_4, "mips4"),
  ENUM_ENT(EF_MIPS_ARCH_5, "mips5"),
  ENUM_ENT(EF_MIPS_ARCH_32, "mips32"),
  ENUM_ENT(EF_MIPS_ARCH_64, "mips64"),
  ENUM_ENT(EF_MIPS_ARCH_32R2, "mips32r2"),
  ENUM_ENT(EF_MIPS_ARCH_64R2, "mips64r2"),
  ENUM_ENT(EF_MIPS_ARCH_32R6, "mips32r6"),
  ENUM_ENT(EF_MIPS_ARCH_64R6, "mips64r6")
};

// clang-format off
#define AMDGPU_MACH_ENUM_ENTS                                                  \
  ENUM_ENT(EF_AMDGPU_MACH_NONE, "none"),                                       \
  ENUM_ENT(EF_AMDGPU_MACH_R600_R600, "r600"),                                  \
  ENUM_ENT(EF_AMDGPU_MACH_R600_R630, "r630"),                                  \
  ENUM_ENT(EF_AMDGPU_MACH_R600_RS880, "rs880"),                                \
  ENUM_ENT(EF_AMDGPU_MACH_R600_RV670, "rv670"),                                \
  ENUM_ENT(EF_AMDGPU_MACH_R600_RV710, "rv710"),                                \
  ENUM_ENT(EF_AMDGPU_MACH_R600_RV730, "rv730"),                                \
  ENUM_ENT(EF_AMDGPU_MACH_R600_RV770, "rv770"),                                \
  ENUM_ENT(EF_AMDGPU_MACH_R600_CEDAR, "cedar"),                                \
  ENUM_ENT(EF_AMDGPU_MACH_R600_CYPRESS, "cypress"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_R600_JUNIPER, "juniper"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_R600_REDWOOD, "redwood"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_R600_SUMO, "sumo"),                                  \
  ENUM_ENT(EF_AMDGPU_MACH_R600_BARTS, "barts"),                                \
  ENUM_ENT(EF_AMDGPU_MACH_R600_CAICOS, "caicos"),                              \
  ENUM_ENT(EF_AMDGPU_MACH_R600_CAYMAN, "cayman"),                              \
  ENUM_ENT(EF_AMDGPU_MACH_R600_TURKS, "turks"),                                \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX600, "gfx600"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX601, "gfx601"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX602, "gfx602"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX700, "gfx700"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX701, "gfx701"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX702, "gfx702"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX703, "gfx703"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX704, "gfx704"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX705, "gfx705"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX801, "gfx801"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX802, "gfx802"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX803, "gfx803"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX805, "gfx805"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX810, "gfx810"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX900, "gfx900"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX902, "gfx902"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX904, "gfx904"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX906, "gfx906"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX908, "gfx908"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX909, "gfx909"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX90A, "gfx90a"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX90C, "gfx90c"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX940, "gfx940"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX941, "gfx941"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX942, "gfx942"),                            \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1010, "gfx1010"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1011, "gfx1011"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1012, "gfx1012"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1013, "gfx1013"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1030, "gfx1030"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1031, "gfx1031"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1032, "gfx1032"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1033, "gfx1033"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1034, "gfx1034"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1035, "gfx1035"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1036, "gfx1036"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1100, "gfx1100"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1101, "gfx1101"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1102, "gfx1102"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1103, "gfx1103"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1150, "gfx1150"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1151, "gfx1151"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1152, "gfx1152"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1200, "gfx1200"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX1201, "gfx1201"),                          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX9_GENERIC, "gfx9-generic"),                \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX10_1_GENERIC, "gfx10-1-generic"),          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX10_3_GENERIC, "gfx10-3-generic"),          \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX11_GENERIC, "gfx11-generic"),              \
  ENUM_ENT(EF_AMDGPU_MACH_AMDGCN_GFX12_GENERIC, "gfx12-generic")
// clang-format on

const EnumEntry<unsigned> ElfHeaderAMDGPUFlagsABIVersion3[] = {
    AMDGPU_MACH_ENUM_ENTS,
    ENUM_ENT(EF_AMDGPU_FEATURE_XNACK_V3, "xnack"),
    ENUM_ENT(EF_AMDGPU_FEATURE_SRAMECC_V3, "sramecc"),
};

const EnumEntry<unsigned> ElfHeaderAMDGPUFlagsABIVersion4[] = {
    AMDGPU_MACH_ENUM_ENTS,
    ENUM_ENT(EF_AMDGPU_FEATURE_XNACK_ANY_V4, "xnack"),
    ENUM_ENT(EF_AMDGPU_FEATURE_XNACK_OFF_V4, "xnack-"),
    ENUM_ENT(EF_AMDGPU_FEATURE_XNACK_ON_V4, "xnack+"),
    ENUM_ENT(EF_AMDGPU_FEATURE_SRAMECC_ANY_V4, "sramecc"),
    ENUM_ENT(EF_AMDGPU_FEATURE_SRAMECC_OFF_V4, "sramecc-"),
    ENUM_ENT(EF_AMDGPU_FEATURE_SRAMECC_ON_V4, "sramecc+"),
};

const EnumEntry<unsigned> ElfHeaderNVPTXFlags[] = {
    ENUM_ENT(EF_CUDA_SM20, "sm_20"), ENUM_ENT(EF_CUDA_SM21, "sm_21"),
    ENUM_ENT(EF_CUDA_SM30, "sm_30"), ENUM_ENT(EF_CUDA_SM32, "sm_32"),
    ENUM_ENT(EF_CUDA_SM35, "sm_35"), ENUM_ENT(EF_CUDA_SM37, "sm_37"),
    ENUM_ENT(EF_CUDA_SM50, "sm_50"), ENUM_ENT(EF_CUDA_SM52, "sm_52"),
    ENUM_ENT(EF_CUDA_SM53, "sm_53"), ENUM_ENT(EF_CUDA_SM60, "sm_60"),
    ENUM_ENT(EF_CUDA_SM61, "sm_61"), ENUM_ENT(EF_CUDA_SM62, "sm_62"),
    ENUM_ENT(EF_CUDA_SM70, "sm_70"), ENUM_ENT(EF_CUDA_SM72, "sm_72"),
    ENUM_ENT(EF_CUDA_SM75, "sm_75"), ENUM_ENT(EF_CUDA_SM80, "sm_80"),
    ENUM_ENT(EF_CUDA_SM86, "sm_86"), ENUM_ENT(EF_CUDA_SM87, "sm_87"),
    ENUM_ENT(EF_CUDA_SM89, "sm_89"), ENUM_ENT(EF_CUDA_SM90, "sm_90"),
};

const EnumEntry<unsigned> ElfHeaderRISCVFlags[] = {
  ENUM_ENT(EF_RISCV_RVC, "RVC"),
  ENUM_ENT(EF_RISCV_FLOAT_ABI_SINGLE, "single-float ABI"),
  ENUM_ENT(EF_RISCV_FLOAT_ABI_DOUBLE, "double-float ABI"),
  ENUM_ENT(EF_RISCV_FLOAT_ABI_QUAD, "quad-float ABI"),
  ENUM_ENT(EF_RISCV_RVE, "RVE"),
  ENUM_ENT(EF_RISCV_TSO, "TSO"),
};

const EnumEntry<unsigned> ElfHeaderAVRFlags[] = {
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVR1),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVR2),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVR25),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVR3),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVR31),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVR35),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVR4),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVR5),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVR51),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVR6),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_AVRTINY),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_XMEGA1),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_XMEGA2),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_XMEGA3),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_XMEGA4),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_XMEGA5),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_XMEGA6),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_AVR_ARCH_XMEGA7),
  ENUM_ENT(EF_AVR_LINKRELAX_PREPARED, "relaxable"),
};

const EnumEntry<unsigned> ElfHeaderLoongArchFlags[] = {
  ENUM_ENT(EF_LOONGARCH_ABI_SOFT_FLOAT, "SOFT-FLOAT"),
  ENUM_ENT(EF_LOONGARCH_ABI_SINGLE_FLOAT, "SINGLE-FLOAT"),
  ENUM_ENT(EF_LOONGARCH_ABI_DOUBLE_FLOAT, "DOUBLE-FLOAT"),
  ENUM_ENT(EF_LOONGARCH_OBJABI_V0, "OBJ-v0"),
  ENUM_ENT(EF_LOONGARCH_OBJABI_V1, "OBJ-v1"),
};

static const EnumEntry<unsigned> ElfHeaderXtensaFlags[] = {
  LLVM_READOBJ_ENUM_ENT(ELF, EF_XTENSA_MACH_NONE),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_XTENSA_XT_INSN),
  LLVM_READOBJ_ENUM_ENT(ELF, EF_XTENSA_XT_LIT)
};

const EnumEntry<unsigned> ElfSymOtherFlags[] = {
  LLVM_READOBJ_ENUM_ENT(ELF, STV_INTERNAL),
  LLVM_READOBJ_ENUM_ENT(ELF, STV_HIDDEN),
  LLVM_READOBJ_ENUM_ENT(ELF, STV_PROTECTED)
};

const EnumEntry<unsigned> ElfMipsSymOtherFlags[] = {
  LLVM_READOBJ_ENUM_ENT(ELF, STO_MIPS_OPTIONAL),
  LLVM_READOBJ_ENUM_ENT(ELF, STO_MIPS_PLT),
  LLVM_READOBJ_ENUM_ENT(ELF, STO_MIPS_PIC),
  LLVM_READOBJ_ENUM_ENT(ELF, STO_MIPS_MICROMIPS)
};

const EnumEntry<unsigned> ElfAArch64SymOtherFlags[] = {
  LLVM_READOBJ_ENUM_ENT(ELF, STO_AARCH64_VARIANT_PCS)
};

const EnumEntry<unsigned> ElfMips16SymOtherFlags[] = {
  LLVM_READOBJ_ENUM_ENT(ELF, STO_MIPS_OPTIONAL),
  LLVM_READOBJ_ENUM_ENT(ELF, STO_MIPS_PLT),
  LLVM_READOBJ_ENUM_ENT(ELF, STO_MIPS_MIPS16)
};

const EnumEntry<unsigned> ElfRISCVSymOtherFlags[] = {
    LLVM_READOBJ_ENUM_ENT(ELF, STO_RISCV_VARIANT_CC)};

static const char *getElfMipsOptionsOdkType(unsigned Odk) {
  switch (Odk) {
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_NULL);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_REGINFO);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_EXCEPTIONS);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_PAD);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_HWPATCH);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_FILL);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_TAGS);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_HWAND);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_HWOR);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_GP_GROUP);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_IDENT);
  LLVM_READOBJ_ENUM_CASE(ELF, ODK_PAGESIZE);
  default:
    return "Unknown";
  }
}

template <typename ELFT>
std::pair<const typename ELFT::Phdr *, const typename ELFT::Shdr *>
ELFDumper<ELFT>::findDynamic() {
  // Try to locate the PT_DYNAMIC header.
  const Elf_Phdr *DynamicPhdr = nullptr;
  if (Expected<ArrayRef<Elf_Phdr>> PhdrsOrErr = Obj.program_headers()) {
    for (const Elf_Phdr &Phdr : *PhdrsOrErr) {
      if (Phdr.p_type != ELF::PT_DYNAMIC)
        continue;
      DynamicPhdr = &Phdr;
      break;
    }
  } else {
    reportUniqueWarning(
        "unable to read program headers to locate the PT_DYNAMIC segment: " +
        toString(PhdrsOrErr.takeError()));
  }

  // Try to locate the .dynamic section in the sections header table.
  const Elf_Shdr *DynamicSec = nullptr;
  for (const Elf_Shdr &Sec : cantFail(Obj.sections())) {
    if (Sec.sh_type != ELF::SHT_DYNAMIC)
      continue;
    DynamicSec = &Sec;
    break;
  }

  if (DynamicPhdr && ((DynamicPhdr->p_offset + DynamicPhdr->p_filesz >
                       ObjF.getMemoryBufferRef().getBufferSize()) ||
                      (DynamicPhdr->p_offset + DynamicPhdr->p_filesz <
                       DynamicPhdr->p_offset))) {
    reportUniqueWarning(
        "PT_DYNAMIC segment offset (0x" +
        Twine::utohexstr(DynamicPhdr->p_offset) + ") + file size (0x" +
        Twine::utohexstr(DynamicPhdr->p_filesz) +
        ") exceeds the size of the file (0x" +
        Twine::utohexstr(ObjF.getMemoryBufferRef().getBufferSize()) + ")");
    // Don't use the broken dynamic header.
    DynamicPhdr = nullptr;
  }

  if (DynamicPhdr && DynamicSec) {
    if (DynamicSec->sh_addr + DynamicSec->sh_size >
            DynamicPhdr->p_vaddr + DynamicPhdr->p_memsz ||
        DynamicSec->sh_addr < DynamicPhdr->p_vaddr)
      reportUniqueWarning(describe(*DynamicSec) +
                          " is not contained within the "
                          "PT_DYNAMIC segment");

    if (DynamicSec->sh_addr != DynamicPhdr->p_vaddr)
      reportUniqueWarning(describe(*DynamicSec) + " is not at the start of "
                                                  "PT_DYNAMIC segment");
  }

  return std::make_pair(DynamicPhdr, DynamicSec);
}

template <typename ELFT>
void ELFDumper<ELFT>::loadDynamicTable() {
  const Elf_Phdr *DynamicPhdr;
  const Elf_Shdr *DynamicSec;
  std::tie(DynamicPhdr, DynamicSec) = findDynamic();
  if (!DynamicPhdr && !DynamicSec)
    return;

  DynRegionInfo FromPhdr(ObjF, *this);
  bool IsPhdrTableValid = false;
  if (DynamicPhdr) {
    // Use cantFail(), because p_offset/p_filesz fields of a PT_DYNAMIC are
    // validated in findDynamic() and so createDRI() is not expected to fail.
    FromPhdr = cantFail(createDRI(DynamicPhdr->p_offset, DynamicPhdr->p_filesz,
                                  sizeof(Elf_Dyn)));
    FromPhdr.SizePrintName = "PT_DYNAMIC size";
    FromPhdr.EntSizePrintName = "";
    IsPhdrTableValid = !FromPhdr.template getAsArrayRef<Elf_Dyn>().empty();
  }

  // Locate the dynamic table described in a section header.
  // Ignore sh_entsize and use the expected value for entry size explicitly.
  // This allows us to dump dynamic sections with a broken sh_entsize
  // field.
  DynRegionInfo FromSec(ObjF, *this);
  bool IsSecTableValid = false;
  if (DynamicSec) {
    Expected<DynRegionInfo> RegOrErr =
        createDRI(DynamicSec->sh_offset, DynamicSec->sh_size, sizeof(Elf_Dyn));
    if (RegOrErr) {
      FromSec = *RegOrErr;
      FromSec.Context = describe(*DynamicSec);
      FromSec.EntSizePrintName = "";
      IsSecTableValid = !FromSec.template getAsArrayRef<Elf_Dyn>().empty();
    } else {
      reportUniqueWarning("unable to read the dynamic table from " +
                          describe(*DynamicSec) + ": " +
                          toString(RegOrErr.takeError()));
    }
  }

  // When we only have information from one of the SHT_DYNAMIC section header or
  // PT_DYNAMIC program header, just use that.
  if (!DynamicPhdr || !DynamicSec) {
    if ((DynamicPhdr && IsPhdrTableValid) || (DynamicSec && IsSecTableValid)) {
      DynamicTable = DynamicPhdr ? FromPhdr : FromSec;
      parseDynamicTable();
    } else {
      reportUniqueWarning("no valid dynamic table was found");
    }
    return;
  }

  // At this point we have tables found from the section header and from the
  // dynamic segment. Usually they match, but we have to do sanity checks to
  // verify that.

  if (FromPhdr.Addr != FromSec.Addr)
    reportUniqueWarning("SHT_DYNAMIC section header and PT_DYNAMIC "
                        "program header disagree about "
                        "the location of the dynamic table");

  if (!IsPhdrTableValid && !IsSecTableValid) {
    reportUniqueWarning("no valid dynamic table was found");
    return;
  }

  // Information in the PT_DYNAMIC program header has priority over the
  // information in a section header.
  if (IsPhdrTableValid) {
    if (!IsSecTableValid)
      reportUniqueWarning(
          "SHT_DYNAMIC dynamic table is invalid: PT_DYNAMIC will be used");
    DynamicTable = FromPhdr;
  } else {
    reportUniqueWarning(
        "PT_DYNAMIC dynamic table is invalid: SHT_DYNAMIC will be used");
    DynamicTable = FromSec;
  }

  parseDynamicTable();
}

template <typename ELFT>
ELFDumper<ELFT>::ELFDumper(const object::ELFObjectFile<ELFT> &O,
                           ScopedPrinter &Writer)
    : ObjDumper(Writer, O.getFileName()), ObjF(O), Obj(O.getELFFile()),
      FileName(O.getFileName()), DynRelRegion(O, *this),
      DynRelaRegion(O, *this), DynCrelRegion(O, *this), DynRelrRegion(O, *this),
      DynPLTRelRegion(O, *this), DynSymTabShndxRegion(O, *this),
      DynamicTable(O, *this) {
  if (!O.IsContentValid())
    return;

  typename ELFT::ShdrRange Sections = cantFail(Obj.sections());
  for (const Elf_Shdr &Sec : Sections) {
    switch (Sec.sh_type) {
    case ELF::SHT_SYMTAB:
      if (!DotSymtabSec)
        DotSymtabSec = &Sec;
      break;
    case ELF::SHT_DYNSYM:
      if (!DotDynsymSec)
        DotDynsymSec = &Sec;

      if (!DynSymRegion) {
        Expected<DynRegionInfo> RegOrErr =
            createDRI(Sec.sh_offset, Sec.sh_size, Sec.sh_entsize);
        if (RegOrErr) {
          DynSymRegion = *RegOrErr;
          DynSymRegion->Context = describe(Sec);

          if (Expected<StringRef> E = Obj.getStringTableForSymtab(Sec))
            DynamicStringTable = *E;
          else
            reportUniqueWarning("unable to get the string table for the " +
                                describe(Sec) + ": " + toString(E.takeError()));
        } else {
          reportUniqueWarning("unable to read dynamic symbols from " +
                              describe(Sec) + ": " +
                              toString(RegOrErr.takeError()));
        }
      }
      break;
    case ELF::SHT_SYMTAB_SHNDX: {
      uint32_t SymtabNdx = Sec.sh_link;
      if (SymtabNdx >= Sections.size()) {
        reportUniqueWarning(
            "unable to get the associated symbol table for " + describe(Sec) +
            ": sh_link (" + Twine(SymtabNdx) +
            ") is greater than or equal to the total number of sections (" +
            Twine(Sections.size()) + ")");
        continue;
      }

      if (Expected<ArrayRef<Elf_Word>> ShndxTableOrErr =
              Obj.getSHNDXTable(Sec)) {
        if (!ShndxTables.insert({&Sections[SymtabNdx], *ShndxTableOrErr})
                 .second)
          reportUniqueWarning(
              "multiple SHT_SYMTAB_SHNDX sections are linked to " +
              describe(Sec));
      } else {
        reportUniqueWarning(ShndxTableOrErr.takeError());
      }
      break;
    }
    case ELF::SHT_GNU_versym:
      if (!SymbolVersionSection)
        SymbolVersionSection = &Sec;
      break;
    case ELF::SHT_GNU_verdef:
      if (!SymbolVersionDefSection)
        SymbolVersionDefSection = &Sec;
      break;
    case ELF::SHT_GNU_verneed:
      if (!SymbolVersionNeedSection)
        SymbolVersionNeedSection = &Sec;
      break;
    case ELF::SHT_LLVM_ADDRSIG:
      if (!DotAddrsigSec)
        DotAddrsigSec = &Sec;
      break;
    }
  }

  loadDynamicTable();
}

template <typename ELFT> void ELFDumper<ELFT>::parseDynamicTable() {
  auto toMappedAddr = [&](uint64_t Tag, uint64_t VAddr) -> const uint8_t * {
    auto MappedAddrOrError = Obj.toMappedAddr(VAddr, [&](const Twine &Msg) {
      this->reportUniqueWarning(Msg);
      return Error::success();
    });
    if (!MappedAddrOrError) {
      this->reportUniqueWarning("unable to parse DT_" +
                                Obj.getDynamicTagAsString(Tag) + ": " +
                                llvm::toString(MappedAddrOrError.takeError()));
      return nullptr;
    }
    return MappedAddrOrError.get();
  };

  const char *StringTableBegin = nullptr;
  uint64_t StringTableSize = 0;
  std::optional<DynRegionInfo> DynSymFromTable;
  for (const Elf_Dyn &Dyn : dynamic_table()) {
    if (Obj.getHeader().e_machine == EM_AARCH64) {
      switch (Dyn.d_tag) {
      case ELF::DT_AARCH64_AUTH_RELRSZ:
        DynRelrRegion.Size = Dyn.getVal();
        DynRelrRegion.SizePrintName = "DT_AARCH64_AUTH_RELRSZ value";
        continue;
      case ELF::DT_AARCH64_AUTH_RELRENT:
        DynRelrRegion.EntSize = Dyn.getVal();
        DynRelrRegion.EntSizePrintName = "DT_AARCH64_AUTH_RELRENT value";
        continue;
      }
    }
    switch (Dyn.d_tag) {
    case ELF::DT_HASH:
      HashTable = reinterpret_cast<const Elf_Hash *>(
          toMappedAddr(Dyn.getTag(), Dyn.getPtr()));
      break;
    case ELF::DT_GNU_HASH:
      GnuHashTable = reinterpret_cast<const Elf_GnuHash *>(
          toMappedAddr(Dyn.getTag(), Dyn.getPtr()));
      break;
    case ELF::DT_STRTAB:
      StringTableBegin = reinterpret_cast<const char *>(
          toMappedAddr(Dyn.getTag(), Dyn.getPtr()));
      break;
    case ELF::DT_STRSZ:
      StringTableSize = Dyn.getVal();
      break;
    case ELF::DT_SYMTAB: {
      // If we can't map the DT_SYMTAB value to an address (e.g. when there are
      // no program headers), we ignore its value.
      if (const uint8_t *VA = toMappedAddr(Dyn.getTag(), Dyn.getPtr())) {
        DynSymFromTable.emplace(ObjF, *this);
        DynSymFromTable->Addr = VA;
        DynSymFromTable->EntSize = sizeof(Elf_Sym);
        DynSymFromTable->EntSizePrintName = "";
      }
      break;
    }
    case ELF::DT_SYMENT: {
      uint64_t Val = Dyn.getVal();
      if (Val != sizeof(Elf_Sym))
        this->reportUniqueWarning("DT_SYMENT value of 0x" +
                                  Twine::utohexstr(Val) +
                                  " is not the size of a symbol (0x" +
                                  Twine::utohexstr(sizeof(Elf_Sym)) + ")");
      break;
    }
    case ELF::DT_RELA:
      DynRelaRegion.Addr = toMappedAddr(Dyn.getTag(), Dyn.getPtr());
      break;
    case ELF::DT_RELASZ:
      DynRelaRegion.Size = Dyn.getVal();
      DynRelaRegion.SizePrintName = "DT_RELASZ value";
      break;
    case ELF::DT_RELAENT:
      DynRelaRegion.EntSize = Dyn.getVal();
      DynRelaRegion.EntSizePrintName = "DT_RELAENT value";
      break;
    case ELF::DT_CREL:
      DynCrelRegion.Addr = toMappedAddr(Dyn.getTag(), Dyn.getPtr());
      break;
    case ELF::DT_SONAME:
      SONameOffset = Dyn.getVal();
      break;
    case ELF::DT_REL:
      DynRelRegion.Addr = toMappedAddr(Dyn.getTag(), Dyn.getPtr());
      break;
    case ELF::DT_RELSZ:
      DynRelRegion.Size = Dyn.getVal();
      DynRelRegion.SizePrintName = "DT_RELSZ value";
      break;
    case ELF::DT_RELENT:
      DynRelRegion.EntSize = Dyn.getVal();
      DynRelRegion.EntSizePrintName = "DT_RELENT value";
      break;
    case ELF::DT_RELR:
    case ELF::DT_ANDROID_RELR:
    case ELF::DT_AARCH64_AUTH_RELR:
      DynRelrRegion.Addr = toMappedAddr(Dyn.getTag(), Dyn.getPtr());
      break;
    case ELF::DT_RELRSZ:
    case ELF::DT_ANDROID_RELRSZ:
    case ELF::DT_AARCH64_AUTH_RELRSZ:
      DynRelrRegion.Size = Dyn.getVal();
      DynRelrRegion.SizePrintName = Dyn.d_tag == ELF::DT_RELRSZ
                                        ? "DT_RELRSZ value"
                                        : "DT_ANDROID_RELRSZ value";
      break;
    case ELF::DT_RELRENT:
    case ELF::DT_ANDROID_RELRENT:
    case ELF::DT_AARCH64_AUTH_RELRENT:
      DynRelrRegion.EntSize = Dyn.getVal();
      DynRelrRegion.EntSizePrintName = Dyn.d_tag == ELF::DT_RELRENT
                                           ? "DT_RELRENT value"
                                           : "DT_ANDROID_RELRENT value";
      break;
    case ELF::DT_PLTREL:
      if (Dyn.getVal() == DT_REL)
        DynPLTRelRegion.EntSize = sizeof(Elf_Rel);
      else if (Dyn.getVal() == DT_RELA)
        DynPLTRelRegion.EntSize = sizeof(Elf_Rela);
      else if (Dyn.getVal() == DT_CREL)
        DynPLTRelRegion.EntSize = 1;
      else
        reportUniqueWarning(Twine("unknown DT_PLTREL value of ") +
                            Twine((uint64_t)Dyn.getVal()));
      DynPLTRelRegion.EntSizePrintName = "PLTREL entry size";
      break;
    case ELF::DT_JMPREL:
      DynPLTRelRegion.Addr = toMappedAddr(Dyn.getTag(), Dyn.getPtr());
      break;
    case ELF::DT_PLTRELSZ:
      DynPLTRelRegion.Size = Dyn.getVal();
      DynPLTRelRegion.SizePrintName = "DT_PLTRELSZ value";
      break;
    case ELF::DT_SYMTAB_SHNDX:
      DynSymTabShndxRegion.Addr = toMappedAddr(Dyn.getTag(), Dyn.getPtr());
      DynSymTabShndxRegion.EntSize = sizeof(Elf_Word);
      break;
    }
  }

  if (StringTableBegin) {
    const uint64_t FileSize = Obj.getBufSize();
    const uint64_t Offset = (const uint8_t *)StringTableBegin - Obj.base();
    if (StringTableSize > FileSize - Offset)
      reportUniqueWarning(
          "the dynamic string table at 0x" + Twine::utohexstr(Offset) +
          " goes past the end of the file (0x" + Twine::utohexstr(FileSize) +
          ") with DT_STRSZ = 0x" + Twine::utohexstr(StringTableSize));
    else
      DynamicStringTable = StringRef(StringTableBegin, StringTableSize);
  }

  const bool IsHashTableSupported = getHashTableEntSize() == 4;
  if (DynSymRegion) {
    // Often we find the information about the dynamic symbol table
    // location in the SHT_DYNSYM section header. However, the value in
    // DT_SYMTAB has priority, because it is used by dynamic loaders to
    // locate .dynsym at runtime. The location we find in the section header
    // and the location we find here should match.
    if (DynSymFromTable && DynSymFromTable->Addr != DynSymRegion->Addr)
      reportUniqueWarning(
          createError("SHT_DYNSYM section header and DT_SYMTAB disagree about "
                      "the location of the dynamic symbol table"));

    // According to the ELF gABI: "The number of symbol table entries should
    // equal nchain". Check to see if the DT_HASH hash table nchain value
    // conflicts with the number of symbols in the dynamic symbol table
    // according to the section header.
    if (HashTable && IsHashTableSupported) {
      if (DynSymRegion->EntSize == 0)
        reportUniqueWarning("SHT_DYNSYM section has sh_entsize == 0");
      else if (HashTable->nchain != DynSymRegion->Size / DynSymRegion->EntSize)
        reportUniqueWarning(
            "hash table nchain (" + Twine(HashTable->nchain) +
            ") differs from symbol count derived from SHT_DYNSYM section "
            "header (" +
            Twine(DynSymRegion->Size / DynSymRegion->EntSize) + ")");
    }
  }

  // Delay the creation of the actual dynamic symbol table until now, so that
  // checks can always be made against the section header-based properties,
  // without worrying about tag order.
  if (DynSymFromTable) {
    if (!DynSymRegion) {
      DynSymRegion = DynSymFromTable;
    } else {
      DynSymRegion->Addr = DynSymFromTable->Addr;
      DynSymRegion->EntSize = DynSymFromTable->EntSize;
      DynSymRegion->EntSizePrintName = DynSymFromTable->EntSizePrintName;
    }
  }

  // Derive the dynamic symbol table size from the DT_HASH hash table, if
  // present.
  if (HashTable && IsHashTableSupported && DynSymRegion) {
    const uint64_t FileSize = Obj.getBufSize();
    const uint64_t DerivedSize =
        (uint64_t)HashTable->nchain * DynSymRegion->EntSize;
    const uint64_t Offset = (const uint8_t *)DynSymRegion->Addr - Obj.base();
    if (DerivedSize > FileSize - Offset)
      reportUniqueWarning(
          "the size (0x" + Twine::utohexstr(DerivedSize) +
          ") of the dynamic symbol table at 0x" + Twine::utohexstr(Offset) +
          ", derived from the hash table, goes past the end of the file (0x" +
          Twine::utohexstr(FileSize) + ") and will be ignored");
    else
      DynSymRegion->Size = HashTable->nchain * DynSymRegion->EntSize;
  }
}

template <typename ELFT> void ELFDumper<ELFT>::printVersionInfo() {
  // Dump version symbol section.
  printVersionSymbolSection(SymbolVersionSection);

  // Dump version definition section.
  printVersionDefinitionSection(SymbolVersionDefSection);

  // Dump version dependency section.
  printVersionDependencySection(SymbolVersionNeedSection);
}

#define LLVM_READOBJ_DT_FLAG_ENT(prefix, enum)                                 \
  { #enum, prefix##_##enum }

const EnumEntry<unsigned> ElfDynamicDTFlags[] = {
  LLVM_READOBJ_DT_FLAG_ENT(DF, ORIGIN),
  LLVM_READOBJ_DT_FLAG_ENT(DF, SYMBOLIC),
  LLVM_READOBJ_DT_FLAG_ENT(DF, TEXTREL),
  LLVM_READOBJ_DT_FLAG_ENT(DF, BIND_NOW),
  LLVM_READOBJ_DT_FLAG_ENT(DF, STATIC_TLS)
};

const EnumEntry<unsigned> ElfDynamicDTFlags1[] = {
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, NOW),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, GLOBAL),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, GROUP),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, NODELETE),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, LOADFLTR),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, INITFIRST),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, NOOPEN),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, ORIGIN),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, DIRECT),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, TRANS),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, INTERPOSE),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, NODEFLIB),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, NODUMP),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, CONFALT),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, ENDFILTEE),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, DISPRELDNE),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, DISPRELPND),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, NODIRECT),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, IGNMULDEF),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, NOKSYMS),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, NOHDR),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, EDITED),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, NORELOC),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, SYMINTPOSE),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, GLOBAUDIT),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, SINGLETON),
  LLVM_READOBJ_DT_FLAG_ENT(DF_1, PIE),
};

const EnumEntry<unsigned> ElfDynamicDTMipsFlags[] = {
  LLVM_READOBJ_DT_FLAG_ENT(RHF, NONE),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, QUICKSTART),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, NOTPOT),
  LLVM_READOBJ_DT_FLAG_ENT(RHS, NO_LIBRARY_REPLACEMENT),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, NO_MOVE),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, SGI_ONLY),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, GUARANTEE_INIT),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, DELTA_C_PLUS_PLUS),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, GUARANTEE_START_INIT),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, PIXIE),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, DEFAULT_DELAY_LOAD),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, REQUICKSTART),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, REQUICKSTARTED),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, CORD),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, NO_UNRES_UNDEF),
  LLVM_READOBJ_DT_FLAG_ENT(RHF, RLD_ORDER_SAFE)
};

#undef LLVM_READOBJ_DT_FLAG_ENT

template <typename T, typename TFlag>
void printFlags(T Value, ArrayRef<EnumEntry<TFlag>> Flags, raw_ostream &OS) {
  SmallVector<EnumEntry<TFlag>, 10> SetFlags;
  for (const EnumEntry<TFlag> &Flag : Flags)
    if (Flag.Value != 0 && (Value & Flag.Value) == Flag.Value)
      SetFlags.push_back(Flag);

  for (const EnumEntry<TFlag> &Flag : SetFlags)
    OS << Flag.Name << " ";
}

template <class ELFT>
const typename ELFT::Shdr *
ELFDumper<ELFT>::findSectionByName(StringRef Name) const {
  for (const Elf_Shdr &Shdr : cantFail(Obj.sections())) {
    if (Expected<StringRef> NameOrErr = Obj.getSectionName(Shdr)) {
      if (*NameOrErr == Name)
        return &Shdr;
    } else {
      reportUniqueWarning("unable to read the name of " + describe(Shdr) +
                          ": " + toString(NameOrErr.takeError()));
    }
  }
  return nullptr;
}

template <class ELFT>
std::string ELFDumper<ELFT>::getDynamicEntry(uint64_t Type,
                                             uint64_t Value) const {
  auto FormatHexValue = [](uint64_t V) {
    std::string Str;
    raw_string_ostream OS(Str);
    const char *ConvChar =
        (opts::Output == opts::GNU) ? "0x%" PRIx64 : "0x%" PRIX64;
    OS << format(ConvChar, V);
    return OS.str();
  };

  auto FormatFlags = [](uint64_t V,
                        llvm::ArrayRef<llvm::EnumEntry<unsigned int>> Array) {
    std::string Str;
    raw_string_ostream OS(Str);
    printFlags(V, Array, OS);
    return OS.str();
  };

  // Handle custom printing of architecture specific tags
  switch (Obj.getHeader().e_machine) {
  case EM_AARCH64:
    switch (Type) {
    case DT_AARCH64_BTI_PLT:
    case DT_AARCH64_PAC_PLT:
    case DT_AARCH64_VARIANT_PCS:
    case DT_AARCH64_MEMTAG_GLOBALSSZ:
      return std::to_string(Value);
    case DT_AARCH64_MEMTAG_MODE:
      switch (Value) {
        case 0:
          return "Synchronous (0)";
        case 1:
          return "Asynchronous (1)";
        default:
          return (Twine("Unknown (") + Twine(Value) + ")").str();
      }
    case DT_AARCH64_MEMTAG_HEAP:
    case DT_AARCH64_MEMTAG_STACK:
      switch (Value) {
        case 0:
          return "Disabled (0)";
        case 1:
          return "Enabled (1)";
        default:
          return (Twine("Unknown (") + Twine(Value) + ")").str();
      }
    case DT_AARCH64_MEMTAG_GLOBALS:
      return (Twine("0x") + utohexstr(Value, /*LowerCase=*/true)).str();
    default:
      break;
    }
    break;
  case EM_HEXAGON:
    switch (Type) {
    case DT_HEXAGON_VER:
      return std::to_string(Value);
    case DT_HEXAGON_SYMSZ:
    case DT_HEXAGON_PLT:
      return FormatHexValue(Value);
    default:
      break;
    }
    break;
  case EM_MIPS:
    switch (Type) {
    case DT_MIPS_RLD_VERSION:
    case DT_MIPS_LOCAL_GOTNO:
    case DT_MIPS_SYMTABNO:
    case DT_MIPS_UNREFEXTNO:
      return std::to_string(Value);
    case DT_MIPS_TIME_STAMP:
    case DT_MIPS_ICHECKSUM:
    case DT_MIPS_IVERSION:
    case DT_MIPS_BASE_ADDRESS:
    case DT_MIPS_MSYM:
    case DT_MIPS_CONFLICT:
    case DT_MIPS_LIBLIST:
    case DT_MIPS_CONFLICTNO:
    case DT_MIPS_LIBLISTNO:
    case DT_MIPS_GOTSYM:
    case DT_MIPS_HIPAGENO:
    case DT_MIPS_RLD_MAP:
    case DT_MIPS_DELTA_CLASS:
    case DT_MIPS_DELTA_CLASS_NO:
    case DT_MIPS_DELTA_INSTANCE:
    case DT_MIPS_DELTA_RELOC:
    case DT_MIPS_DELTA_RELOC_NO:
    case DT_MIPS_DELTA_SYM:
    case DT_MIPS_DELTA_SYM_NO:
    case DT_MIPS_DELTA_CLASSSYM:
    case DT_MIPS_DELTA_CLASSSYM_NO:
    case DT_MIPS_CXX_FLAGS:
    case DT_MIPS_PIXIE_INIT:
    case DT_MIPS_SYMBOL_LIB:
    case DT_MIPS_LOCALPAGE_GOTIDX:
    case DT_MIPS_LOCAL_GOTIDX:
    case DT_MIPS_HIDDEN_GOTIDX:
    case DT_MIPS_PROTECTED_GOTIDX:
    case DT_MIPS_OPTIONS:
    case DT_MIPS_INTERFACE:
    case DT_MIPS_DYNSTR_ALIGN:
    case DT_MIPS_INTERFACE_SIZE:
    case DT_MIPS_RLD_TEXT_RESOLVE_ADDR:
    case DT_MIPS_PERF_SUFFIX:
    case DT_MIPS_COMPACT_SIZE:
    case DT_MIPS_GP_VALUE:
    case DT_MIPS_AUX_DYNAMIC:
    case DT_MIPS_PLTGOT:
    case DT_MIPS_RWPLT:
    case DT_MIPS_RLD_MAP_REL:
    case DT_MIPS_XHASH:
      return FormatHexValue(Value);
    case DT_MIPS_FLAGS:
      return FormatFlags(Value, ArrayRef(ElfDynamicDTMipsFlags));
    default:
      break;
    }
    break;
  default:
    break;
  }

  switch (Type) {
  case DT_PLTREL:
    if (Value == DT_REL)
      return "REL";
    if (Value == DT_RELA)
      return "RELA";
    if (Value == DT_CREL)
      return "CREL";
    [[fallthrough]];
  case DT_PLTGOT:
  case DT_HASH:
  case DT_STRTAB:
  case DT_SYMTAB:
  case DT_RELA:
  case DT_INIT:
  case DT_FINI:
  case DT_REL:
  case DT_JMPREL:
  case DT_INIT_ARRAY:
  case DT_FINI_ARRAY:
  case DT_PREINIT_ARRAY:
  case DT_DEBUG:
  case DT_CREL:
  case DT_VERDEF:
  case DT_VERNEED:
  case DT_VERSYM:
  case DT_GNU_HASH:
  case DT_NULL:
    return FormatHexValue(Value);
  case DT_RELACOUNT:
  case DT_RELCOUNT:
  case DT_VERDEFNUM:
  case DT_VERNEEDNUM:
    return std::to_string(Value);
  case DT_PLTRELSZ:
  case DT_RELASZ:
  case DT_RELAENT:
  case DT_STRSZ:
  case DT_SYMENT:
  case DT_RELSZ:
  case DT_RELENT:
  case DT_INIT_ARRAYSZ:
  case DT_FINI_ARRAYSZ:
  case DT_PREINIT_ARRAYSZ:
  case DT_RELRSZ:
  case DT_RELRENT:
  case DT_AARCH64_AUTH_RELRSZ:
  case DT_AARCH64_AUTH_RELRENT:
  case DT_ANDROID_RELSZ:
  case DT_ANDROID_RELASZ:
    return std::to_string(Value) + " (bytes)";
  case DT_NEEDED:
  case DT_SONAME:
  case DT_AUXILIARY:
  case DT_USED:
  case DT_FILTER:
  case DT_RPATH:
  case DT_RUNPATH: {
    const std::map<uint64_t, const char *> TagNames = {
        {DT_NEEDED, "Shared library"},       {DT_SONAME, "Library soname"},
        {DT_AUXILIARY, "Auxiliary library"}, {DT_USED, "Not needed object"},
        {DT_FILTER, "Filter library"},       {DT_RPATH, "Library rpath"},
        {DT_RUNPATH, "Library runpath"},
    };

    return (Twine(TagNames.at(Type)) + ": [" + getDynamicString(Value) + "]")
        .str();
  }
  case DT_FLAGS:
    return FormatFlags(Value, ArrayRef(ElfDynamicDTFlags));
  case DT_FLAGS_1:
    return FormatFlags(Value, ArrayRef(ElfDynamicDTFlags1));
  default:
    return FormatHexValue(Value);
  }
}

template <class ELFT>
StringRef ELFDumper<ELFT>::getDynamicString(uint64_t Value) const {
  if (DynamicStringTable.empty() && !DynamicStringTable.data()) {
    reportUniqueWarning("string table was not found");
    return "<?>";
  }

  auto WarnAndReturn = [this](const Twine &Msg, uint64_t Offset) {
    reportUniqueWarning("string table at offset 0x" + Twine::utohexstr(Offset) +
                        Msg);
    return "<?>";
  };

  const uint64_t FileSize = Obj.getBufSize();
  const uint64_t Offset =
      (const uint8_t *)DynamicStringTable.data() - Obj.base();
  if (DynamicStringTable.size() > FileSize - Offset)
    return WarnAndReturn(" with size 0x" +
                             Twine::utohexstr(DynamicStringTable.size()) +
                             " goes past the end of the file (0x" +
                             Twine::utohexstr(FileSize) + ")",
                         Offset);

  if (Value >= DynamicStringTable.size())
    return WarnAndReturn(
        ": unable to read the string at 0x" + Twine::utohexstr(Offset + Value) +
            ": it goes past the end of the table (0x" +
            Twine::utohexstr(Offset + DynamicStringTable.size()) + ")",
        Offset);

  if (DynamicStringTable.back() != '\0')
    return WarnAndReturn(": unable to read the string at 0x" +
                             Twine::utohexstr(Offset + Value) +
                             ": the string table is not null-terminated",
                         Offset);

  return DynamicStringTable.data() + Value;
}

template <class ELFT> void ELFDumper<ELFT>::printUnwindInfo() {
  DwarfCFIEH::PrinterContext<ELFT> Ctx(W, ObjF);
  Ctx.printUnwindInformation();
}

// The namespace is needed to fix the compilation with GCC older than 7.0+.
namespace {
template <> void ELFDumper<ELF32LE>::printUnwindInfo() {
  if (Obj.getHeader().e_machine == EM_ARM) {
    ARM::EHABI::PrinterContext<ELF32LE> Ctx(W, Obj, ObjF.getFileName(),
                                            DotSymtabSec);
    Ctx.PrintUnwindInformation();
  }
  DwarfCFIEH::PrinterContext<ELF32LE> Ctx(W, ObjF);
  Ctx.printUnwindInformation();
}
} // namespace

template <class ELFT> void ELFDumper<ELFT>::printNeededLibraries() {
  ListScope D(W, "NeededLibraries");

  std::vector<StringRef> Libs;
  for (const auto &Entry : dynamic_table())
    if (Entry.d_tag == ELF::DT_NEEDED)
      Libs.push_back(getDynamicString(Entry.d_un.d_val));

  llvm::sort(Libs);

  for (StringRef L : Libs)
    W.printString(L);
}

template <class ELFT>
static Error checkHashTable(const ELFDumper<ELFT> &Dumper,
                            const typename ELFT::Hash *H,
                            bool *IsHeaderValid = nullptr) {
  const ELFFile<ELFT> &Obj = Dumper.getElfObject().getELFFile();
  const uint64_t SecOffset = (const uint8_t *)H - Obj.base();
  if (Dumper.getHashTableEntSize() == 8) {
    auto It = llvm::find_if(ElfMachineType, [&](const EnumEntry<unsigned> &E) {
      return E.Value == Obj.getHeader().e_machine;
    });
    if (IsHeaderValid)
      *IsHeaderValid = false;
    return createError("the hash table at 0x" + Twine::utohexstr(SecOffset) +
                       " is not supported: it contains non-standard 8 "
                       "byte entries on " +
                       It->AltName + " platform");
  }

  auto MakeError = [&](const Twine &Msg = "") {
    return createError("the hash table at offset 0x" +
                       Twine::utohexstr(SecOffset) +
                       " goes past the end of the file (0x" +
                       Twine::utohexstr(Obj.getBufSize()) + ")" + Msg);
  };

  // Each SHT_HASH section starts from two 32-bit fields: nbucket and nchain.
  const unsigned HeaderSize = 2 * sizeof(typename ELFT::Word);

  if (IsHeaderValid)
    *IsHeaderValid = Obj.getBufSize() - SecOffset >= HeaderSize;

  if (Obj.getBufSize() - SecOffset < HeaderSize)
    return MakeError();

  if (Obj.getBufSize() - SecOffset - HeaderSize <
      ((uint64_t)H->nbucket + H->nchain) * sizeof(typename ELFT::Word))
    return MakeError(", nbucket = " + Twine(H->nbucket) +
                     ", nchain = " + Twine(H->nchain));
  return Error::success();
}

template <class ELFT>
static Error checkGNUHashTable(const ELFFile<ELFT> &Obj,
                               const typename ELFT::GnuHash *GnuHashTable,
                               bool *IsHeaderValid = nullptr) {
  const uint8_t *TableData = reinterpret_cast<const uint8_t *>(GnuHashTable);
  assert(TableData >= Obj.base() && TableData < Obj.base() + Obj.getBufSize() &&
         "GnuHashTable must always point to a location inside the file");

  uint64_t TableOffset = TableData - Obj.base();
  if (IsHeaderValid)
    *IsHeaderValid = TableOffset + /*Header size:*/ 16 < Obj.getBufSize();
  if (TableOffset + 16 + (uint64_t)GnuHashTable->nbuckets * 4 +
          (uint64_t)GnuHashTable->maskwords * sizeof(typename ELFT::Off) >=
      Obj.getBufSize())
    return createError("unable to dump the SHT_GNU_HASH "
                       "section at 0x" +
                       Twine::utohexstr(TableOffset) +
                       ": it goes past the end of the file");
  return Error::success();
}

template <typename ELFT> void ELFDumper<ELFT>::printHashTable() {
  DictScope D(W, "HashTable");
  if (!HashTable)
    return;

  bool IsHeaderValid;
  Error Err = checkHashTable(*this, HashTable, &IsHeaderValid);
  if (IsHeaderValid) {
    W.printNumber("Num Buckets", HashTable->nbucket);
    W.printNumber("Num Chains", HashTable->nchain);
  }

  if (Err) {
    reportUniqueWarning(std::move(Err));
    return;
  }

  W.printList("Buckets", HashTable->buckets());
  W.printList("Chains", HashTable->chains());
}

template <class ELFT>
static Expected<ArrayRef<typename ELFT::Word>>
getGnuHashTableChains(std::optional<DynRegionInfo> DynSymRegion,
                      const typename ELFT::GnuHash *GnuHashTable) {
  if (!DynSymRegion)
    return createError("no dynamic symbol table found");

  ArrayRef<typename ELFT::Sym> DynSymTable =
      DynSymRegion->template getAsArrayRef<typename ELFT::Sym>();
  size_t NumSyms = DynSymTable.size();
  if (!NumSyms)
    return createError("the dynamic symbol table is empty");

  if (GnuHashTable->symndx < NumSyms)
    return GnuHashTable->values(NumSyms);

  // A normal empty GNU hash table section produced by linker might have
  // symndx set to the number of dynamic symbols + 1 (for the zero symbol)
  // and have dummy null values in the Bloom filter and in the buckets
  // vector (or no values at all). It happens because the value of symndx is not
  // important for dynamic loaders when the GNU hash table is empty. They just
  // skip the whole object during symbol lookup. In such cases, the symndx value
  // is irrelevant and we should not report a warning.
  ArrayRef<typename ELFT::Word> Buckets = GnuHashTable->buckets();
  if (!llvm::all_of(Buckets, [](typename ELFT::Word V) { return V == 0; }))
    return createError(
        "the first hashed symbol index (" + Twine(GnuHashTable->symndx) +
        ") is greater than or equal to the number of dynamic symbols (" +
        Twine(NumSyms) + ")");
  // There is no way to represent an array of (dynamic symbols count - symndx)
  // length.
  return ArrayRef<typename ELFT::Word>();
}

template <typename ELFT>
void ELFDumper<ELFT>::printGnuHashTable() {
  DictScope D(W, "GnuHashTable");
  if (!GnuHashTable)
    return;

  bool IsHeaderValid;
  Error Err = checkGNUHashTable<ELFT>(Obj, GnuHashTable, &IsHeaderValid);
  if (IsHeaderValid) {
    W.printNumber("Num Buckets", GnuHashTable->nbuckets);
    W.printNumber("First Hashed Symbol Index", GnuHashTable->symndx);
    W.printNumber("Num Mask Words", GnuHashTable->maskwords);
    W.printNumber("Shift Count", GnuHashTable->shift2);
  }

  if (Err) {
    reportUniqueWarning(std::move(Err));
    return;
  }

  ArrayRef<typename ELFT::Off> BloomFilter = GnuHashTable->filter();
  W.printHexList("Bloom Filter", BloomFilter);

  ArrayRef<Elf_Word> Buckets = GnuHashTable->buckets();
  W.printList("Buckets", Buckets);

  Expected<ArrayRef<Elf_Word>> Chains =
      getGnuHashTableChains<ELFT>(DynSymRegion, GnuHashTable);
  if (!Chains) {
    reportUniqueWarning("unable to dump 'Values' for the SHT_GNU_HASH "
                        "section: " +
                        toString(Chains.takeError()));
    return;
  }

  W.printHexList("Values", *Chains);
}

template <typename ELFT> void ELFDumper<ELFT>::printHashHistograms() {
  // Print histogram for the .hash section.
  if (this->HashTable) {
    if (Error E = checkHashTable<ELFT>(*this, this->HashTable))
      this->reportUniqueWarning(std::move(E));
    else
      printHashHistogram(*this->HashTable);
  }

  // Print histogram for the .gnu.hash section.
  if (this->GnuHashTable) {
    if (Error E = checkGNUHashTable<ELFT>(this->Obj, this->GnuHashTable))
      this->reportUniqueWarning(std::move(E));
    else
      printGnuHashHistogram(*this->GnuHashTable);
  }
}

template <typename ELFT>
void ELFDumper<ELFT>::printHashHistogram(const Elf_Hash &HashTable) const {
  size_t NBucket = HashTable.nbucket;
  size_t NChain = HashTable.nchain;
  ArrayRef<Elf_Word> Buckets = HashTable.buckets();
  ArrayRef<Elf_Word> Chains = HashTable.chains();
  size_t TotalSyms = 0;
  // If hash table is correct, we have at least chains with 0 length.
  size_t MaxChain = 1;

  if (NChain == 0 || NBucket == 0)
    return;

  std::vector<size_t> ChainLen(NBucket, 0);
  // Go over all buckets and note chain lengths of each bucket (total
  // unique chain lengths).
  for (size_t B = 0; B < NBucket; ++B) {
    BitVector Visited(NChain);
    for (size_t C = Buckets[B]; C < NChain; C = Chains[C]) {
      if (C == ELF::STN_UNDEF)
          break;
      if (Visited[C]) {
          this->reportUniqueWarning(
              ".hash section is invalid: bucket " + Twine(C) +
              ": a cycle was detected in the linked chain");
          break;
      }
      Visited[C] = true;
      if (MaxChain <= ++ChainLen[B])
          ++MaxChain;
    }
    TotalSyms += ChainLen[B];
  }

  if (!TotalSyms)
    return;

  std::vector<size_t> Count(MaxChain, 0);
  // Count how long is the chain for each bucket.
  for (size_t B = 0; B < NBucket; B++)
    ++Count[ChainLen[B]];
  // Print Number of buckets with each chain lengths and their cumulative
  // coverage of the symbols.
  printHashHistogramStats(NBucket, MaxChain, TotalSyms, Count, /*IsGnu=*/false);
}

template <class ELFT>
void ELFDumper<ELFT>::printGnuHashHistogram(
    const Elf_GnuHash &GnuHashTable) const {
  Expected<ArrayRef<Elf_Word>> ChainsOrErr =
      getGnuHashTableChains<ELFT>(this->DynSymRegion, &GnuHashTable);
  if (!ChainsOrErr) {
    this->reportUniqueWarning("unable to print the GNU hash table histogram: " +
                              toString(ChainsOrErr.takeError()));
    return;
  }

  ArrayRef<Elf_Word> Chains = *ChainsOrErr;
  size_t Symndx = GnuHashTable.symndx;
  size_t TotalSyms = 0;
  size_t MaxChain = 1;

  size_t NBucket = GnuHashTable.nbuckets;
  if (Chains.empty() || NBucket == 0)
    return;

  ArrayRef<Elf_Word> Buckets = GnuHashTable.buckets();
  std::vector<size_t> ChainLen(NBucket, 0);
  for (size_t B = 0; B < NBucket; ++B) {
    if (!Buckets[B])
      continue;
    size_t Len = 1;
    for (size_t C = Buckets[B] - Symndx;
         C < Chains.size() && (Chains[C] & 1) == 0; ++C)
      if (MaxChain < ++Len)
          ++MaxChain;
    ChainLen[B] = Len;
    TotalSyms += Len;
  }
  ++MaxChain;

  if (!TotalSyms)
    return;

  std::vector<size_t> Count(MaxChain, 0);
  for (size_t B = 0; B < NBucket; ++B)
    ++Count[ChainLen[B]];
  // Print Number of buckets with each chain lengths and their cumulative
  // coverage of the symbols.
  printHashHistogramStats(NBucket, MaxChain, TotalSyms, Count, /*IsGnu=*/true);
}

template <typename ELFT> void ELFDumper<ELFT>::printLoadName() {
  StringRef SOName = "<Not found>";
  if (SONameOffset)
    SOName = getDynamicString(*SONameOffset);
  W.printString("LoadName", SOName);
}

template <class ELFT> void ELFDumper<ELFT>::printArchSpecificInfo() {
  switch (Obj.getHeader().e_machine) {
  case EM_HEXAGON:
    printAttributes(ELF::SHT_HEXAGON_ATTRIBUTES,
                    std::make_unique<HexagonAttributeParser>(&W),
                    llvm::endianness::little);
    break;
  case EM_ARM:
    printAttributes(
        ELF::SHT_ARM_ATTRIBUTES, std::make_unique<ARMAttributeParser>(&W),
        Obj.isLE() ? llvm::endianness::little : llvm::endianness::big);
    break;
  case EM_RISCV:
    if (Obj.isLE())
      printAttributes(ELF::SHT_RISCV_ATTRIBUTES,
                      std::make_unique<RISCVAttributeParser>(&W),
                      llvm::endianness::little);
    else
      reportUniqueWarning("attribute printing not implemented for big-endian "
                          "RISC-V objects");
    break;
  case EM_MSP430:
    printAttributes(ELF::SHT_MSP430_ATTRIBUTES,
                    std::make_unique<MSP430AttributeParser>(&W),
                    llvm::endianness::little);
    break;
  case EM_MIPS: {
    printMipsABIFlags();
    printMipsOptions();
    printMipsReginfo();
    MipsGOTParser<ELFT> Parser(*this);
    if (Error E = Parser.findGOT(dynamic_table(), dynamic_symbols()))
      reportUniqueWarning(std::move(E));
    else if (!Parser.isGotEmpty())
      printMipsGOT(Parser);

    if (Error E = Parser.findPLT(dynamic_table()))
      reportUniqueWarning(std::move(E));
    else if (!Parser.isPltEmpty())
      printMipsPLT(Parser);
    break;
  }
  default:
    break;
  }
}

template <class ELFT>
void ELFDumper<ELFT>::printAttributes(
    unsigned AttrShType, std::unique_ptr<ELFAttributeParser> AttrParser,
    llvm::endianness Endianness) {
  assert((AttrShType != ELF::SHT_NULL) && AttrParser &&
         "Incomplete ELF attribute implementation");
  DictScope BA(W, "BuildAttributes");
  for (const Elf_Shdr &Sec : cantFail(Obj.sections())) {
    if (Sec.sh_type != AttrShType)
      continue;

    ArrayRef<uint8_t> Contents;
    if (Expected<ArrayRef<uint8_t>> ContentOrErr =
            Obj.getSectionContents(Sec)) {
      Contents = *ContentOrErr;
      if (Contents.empty()) {
        reportUniqueWarning("the " + describe(Sec) + " is empty");
        continue;
      }
    } else {
      reportUniqueWarning("unable to read the content of the " + describe(Sec) +
                          ": " + toString(ContentOrErr.takeError()));
      continue;
    }

    W.printHex("FormatVersion", Contents[0]);

    if (Error E = AttrParser->parse(Contents, Endianness))
      reportUniqueWarning("unable to dump attributes from the " +
                          describe(Sec) + ": " + toString(std::move(E)));
  }
}

namespace {

template <class ELFT> class MipsGOTParser {
public:
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)
  using Entry = typename ELFT::Addr;
  using Entries = ArrayRef<Entry>;

  const bool IsStatic;
  const ELFFile<ELFT> &Obj;
  const ELFDumper<ELFT> &Dumper;

  MipsGOTParser(const ELFDumper<ELFT> &D);
  Error findGOT(Elf_Dyn_Range DynTable, Elf_Sym_Range DynSyms);
  Error findPLT(Elf_Dyn_Range DynTable);

  bool isGotEmpty() const { return GotEntries.empty(); }
  bool isPltEmpty() const { return PltEntries.empty(); }

  uint64_t getGp() const;

  const Entry *getGotLazyResolver() const;
  const Entry *getGotModulePointer() const;
  const Entry *getPltLazyResolver() const;
  const Entry *getPltModulePointer() const;

  Entries getLocalEntries() const;
  Entries getGlobalEntries() const;
  Entries getOtherEntries() const;
  Entries getPltEntries() const;

  uint64_t getGotAddress(const Entry * E) const;
  int64_t getGotOffset(const Entry * E) const;
  const Elf_Sym *getGotSym(const Entry *E) const;

  uint64_t getPltAddress(const Entry * E) const;
  const Elf_Sym *getPltSym(const Entry *E) const;

  StringRef getPltStrTable() const { return PltStrTable; }
  const Elf_Shdr *getPltSymTable() const { return PltSymTable; }

private:
  const Elf_Shdr *GotSec;
  size_t LocalNum;
  size_t GlobalNum;

  const Elf_Shdr *PltSec;
  const Elf_Shdr *PltRelSec;
  const Elf_Shdr *PltSymTable;
  StringRef FileName;

  Elf_Sym_Range GotDynSyms;
  StringRef PltStrTable;

  Entries GotEntries;
  Entries PltEntries;
};

} // end anonymous namespace

template <class ELFT>
MipsGOTParser<ELFT>::MipsGOTParser(const ELFDumper<ELFT> &D)
    : IsStatic(D.dynamic_table().empty()), Obj(D.getElfObject().getELFFile()),
      Dumper(D), GotSec(nullptr), LocalNum(0), GlobalNum(0), PltSec(nullptr),
      PltRelSec(nullptr), PltSymTable(nullptr),
      FileName(D.getElfObject().getFileName()) {}

template <class ELFT>
Error MipsGOTParser<ELFT>::findGOT(Elf_Dyn_Range DynTable,
                                   Elf_Sym_Range DynSyms) {
  // See "Global Offset Table" in Chapter 5 in the following document
  // for detailed GOT description.
  // ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf

  // Find static GOT secton.
  if (IsStatic) {
    GotSec = Dumper.findSectionByName(".got");
    if (!GotSec)
      return Error::success();

    ArrayRef<uint8_t> Content =
        unwrapOrError(FileName, Obj.getSectionContents(*GotSec));
    GotEntries = Entries(reinterpret_cast<const Entry *>(Content.data()),
                         Content.size() / sizeof(Entry));
    LocalNum = GotEntries.size();
    return Error::success();
  }

  // Lookup dynamic table tags which define the GOT layout.
  std::optional<uint64_t> DtPltGot;
  std::optional<uint64_t> DtLocalGotNum;
  std::optional<uint64_t> DtGotSym;
  for (const auto &Entry : DynTable) {
    switch (Entry.getTag()) {
    case ELF::DT_PLTGOT:
      DtPltGot = Entry.getVal();
      break;
    case ELF::DT_MIPS_LOCAL_GOTNO:
      DtLocalGotNum = Entry.getVal();
      break;
    case ELF::DT_MIPS_GOTSYM:
      DtGotSym = Entry.getVal();
      break;
    }
  }

  if (!DtPltGot && !DtLocalGotNum && !DtGotSym)
    return Error::success();

  if (!DtPltGot)
    return createError("cannot find PLTGOT dynamic tag");
  if (!DtLocalGotNum)
    return createError("cannot find MIPS_LOCAL_GOTNO dynamic tag");
  if (!DtGotSym)
    return createError("cannot find MIPS_GOTSYM dynamic tag");

  size_t DynSymTotal = DynSyms.size();
  if (*DtGotSym > DynSymTotal)
    return createError("DT_MIPS_GOTSYM value (" + Twine(*DtGotSym) +
                       ") exceeds the number of dynamic symbols (" +
                       Twine(DynSymTotal) + ")");

  GotSec = findNotEmptySectionByAddress(Obj, FileName, *DtPltGot);
  if (!GotSec)
    return createError("there is no non-empty GOT section at 0x" +
                       Twine::utohexstr(*DtPltGot));

  LocalNum = *DtLocalGotNum;
  GlobalNum = DynSymTotal - *DtGotSym;

  ArrayRef<uint8_t> Content =
      unwrapOrError(FileName, Obj.getSectionContents(*GotSec));
  GotEntries = Entries(reinterpret_cast<const Entry *>(Content.data()),
                       Content.size() / sizeof(Entry));
  GotDynSyms = DynSyms.drop_front(*DtGotSym);

  return Error::success();
}

template <class ELFT>
Error MipsGOTParser<ELFT>::findPLT(Elf_Dyn_Range DynTable) {
  // Lookup dynamic table tags which define the PLT layout.
  std::optional<uint64_t> DtMipsPltGot;
  std::optional<uint64_t> DtJmpRel;
  for (const auto &Entry : DynTable) {
    switch (Entry.getTag()) {
    case ELF::DT_MIPS_PLTGOT:
      DtMipsPltGot = Entry.getVal();
      break;
    case ELF::DT_JMPREL:
      DtJmpRel = Entry.getVal();
      break;
    }
  }

  if (!DtMipsPltGot && !DtJmpRel)
    return Error::success();

  // Find PLT section.
  if (!DtMipsPltGot)
    return createError("cannot find MIPS_PLTGOT dynamic tag");
  if (!DtJmpRel)
    return createError("cannot find JMPREL dynamic tag");

  PltSec = findNotEmptySectionByAddress(Obj, FileName, *DtMipsPltGot);
  if (!PltSec)
    return createError("there is no non-empty PLTGOT section at 0x" +
                       Twine::utohexstr(*DtMipsPltGot));

  PltRelSec = findNotEmptySectionByAddress(Obj, FileName, *DtJmpRel);
  if (!PltRelSec)
    return createError("there is no non-empty RELPLT section at 0x" +
                       Twine::utohexstr(*DtJmpRel));

  if (Expected<ArrayRef<uint8_t>> PltContentOrErr =
          Obj.getSectionContents(*PltSec))
    PltEntries =
        Entries(reinterpret_cast<const Entry *>(PltContentOrErr->data()),
                PltContentOrErr->size() / sizeof(Entry));
  else
    return createError("unable to read PLTGOT section content: " +
                       toString(PltContentOrErr.takeError()));

  if (Expected<const Elf_Shdr *> PltSymTableOrErr =
          Obj.getSection(PltRelSec->sh_link))
    PltSymTable = *PltSymTableOrErr;
  else
    return createError("unable to get a symbol table linked to the " +
                       describe(Obj, *PltRelSec) + ": " +
                       toString(PltSymTableOrErr.takeError()));

  if (Expected<StringRef> StrTabOrErr =
          Obj.getStringTableForSymtab(*PltSymTable))
    PltStrTable = *StrTabOrErr;
  else
    return createError("unable to get a string table for the " +
                       describe(Obj, *PltSymTable) + ": " +
                       toString(StrTabOrErr.takeError()));

  return Error::success();
}

template <class ELFT> uint64_t MipsGOTParser<ELFT>::getGp() const {
  return GotSec->sh_addr + 0x7ff0;
}

template <class ELFT>
const typename MipsGOTParser<ELFT>::Entry *
MipsGOTParser<ELFT>::getGotLazyResolver() const {
  return LocalNum > 0 ? &GotEntries[0] : nullptr;
}

template <class ELFT>
const typename MipsGOTParser<ELFT>::Entry *
MipsGOTParser<ELFT>::getGotModulePointer() const {
  if (LocalNum < 2)
    return nullptr;
  const Entry &E = GotEntries[1];
  if ((E >> (sizeof(Entry) * 8 - 1)) == 0)
    return nullptr;
  return &E;
}

template <class ELFT>
typename MipsGOTParser<ELFT>::Entries
MipsGOTParser<ELFT>::getLocalEntries() const {
  size_t Skip = getGotModulePointer() ? 2 : 1;
  if (LocalNum - Skip <= 0)
    return Entries();
  return GotEntries.slice(Skip, LocalNum - Skip);
}

template <class ELFT>
typename MipsGOTParser<ELFT>::Entries
MipsGOTParser<ELFT>::getGlobalEntries() const {
  if (GlobalNum == 0)
    return Entries();
  return GotEntries.slice(LocalNum, GlobalNum);
}

template <class ELFT>
typename MipsGOTParser<ELFT>::Entries
MipsGOTParser<ELFT>::getOtherEntries() const {
  size_t OtherNum = GotEntries.size() - LocalNum - GlobalNum;
  if (OtherNum == 0)
    return Entries();
  return GotEntries.slice(LocalNum + GlobalNum, OtherNum);
}

template <class ELFT>
uint64_t MipsGOTParser<ELFT>::getGotAddress(const Entry *E) const {
  int64_t Offset = std::distance(GotEntries.data(), E) * sizeof(Entry);
  return GotSec->sh_addr + Offset;
}

template <class ELFT>
int64_t MipsGOTParser<ELFT>::getGotOffset(const Entry *E) const {
  int64_t Offset = std::distance(GotEntries.data(), E) * sizeof(Entry);
  return Offset - 0x7ff0;
}

template <class ELFT>
const typename MipsGOTParser<ELFT>::Elf_Sym *
MipsGOTParser<ELFT>::getGotSym(const Entry *E) const {
  int64_t Offset = std::distance(GotEntries.data(), E);
  return &GotDynSyms[Offset - LocalNum];
}

template <class ELFT>
const typename MipsGOTParser<ELFT>::Entry *
MipsGOTParser<ELFT>::getPltLazyResolver() const {
  return PltEntries.empty() ? nullptr : &PltEntries[0];
}

template <class ELFT>
const typename MipsGOTParser<ELFT>::Entry *
MipsGOTParser<ELFT>::getPltModulePointer() const {
  return PltEntries.size() < 2 ? nullptr : &PltEntries[1];
}

template <class ELFT>
typename MipsGOTParser<ELFT>::Entries
MipsGOTParser<ELFT>::getPltEntries() const {
  if (PltEntries.size() <= 2)
    return Entries();
  return PltEntries.slice(2, PltEntries.size() - 2);
}

template <class ELFT>
uint64_t MipsGOTParser<ELFT>::getPltAddress(const Entry *E) const {
  int64_t Offset = std::distance(PltEntries.data(), E) * sizeof(Entry);
  return PltSec->sh_addr + Offset;
}

template <class ELFT>
const typename MipsGOTParser<ELFT>::Elf_Sym *
MipsGOTParser<ELFT>::getPltSym(const Entry *E) const {
  int64_t Offset = std::distance(getPltEntries().data(), E);
  if (PltRelSec->sh_type == ELF::SHT_REL) {
    Elf_Rel_Range Rels = unwrapOrError(FileName, Obj.rels(*PltRelSec));
    return unwrapOrError(FileName,
                         Obj.getRelocationSymbol(Rels[Offset], PltSymTable));
  } else {
    Elf_Rela_Range Rels = unwrapOrError(FileName, Obj.relas(*PltRelSec));
    return unwrapOrError(FileName,
                         Obj.getRelocationSymbol(Rels[Offset], PltSymTable));
  }
}

const EnumEntry<unsigned> ElfMipsISAExtType[] = {
  {"None",                    Mips::AFL_EXT_NONE},
  {"Broadcom SB-1",           Mips::AFL_EXT_SB1},
  {"Cavium Networks Octeon",  Mips::AFL_EXT_OCTEON},
  {"Cavium Networks Octeon2", Mips::AFL_EXT_OCTEON2},
  {"Cavium Networks OcteonP", Mips::AFL_EXT_OCTEONP},
  {"Cavium Networks Octeon3", Mips::AFL_EXT_OCTEON3},
  {"LSI R4010",               Mips::AFL_EXT_4010},
  {"Loongson 2E",             Mips::AFL_EXT_LOONGSON_2E},
  {"Loongson 2F",             Mips::AFL_EXT_LOONGSON_2F},
  {"Loongson 3A",             Mips::AFL_EXT_LOONGSON_3A},
  {"MIPS R4650",              Mips::AFL_EXT_4650},
  {"MIPS R5900",              Mips::AFL_EXT_5900},
  {"MIPS R10000",             Mips::AFL_EXT_10000},
  {"NEC VR4100",              Mips::AFL_EXT_4100},
  {"NEC VR4111/VR4181",       Mips::AFL_EXT_4111},
  {"NEC VR4120",              Mips::AFL_EXT_4120},
  {"NEC VR5400",              Mips::AFL_EXT_5400},
  {"NEC VR5500",              Mips::AFL_EXT_5500},
  {"RMI Xlr",                 Mips::AFL_EXT_XLR},
  {"Toshiba R3900",           Mips::AFL_EXT_3900}
};

const EnumEntry<unsigned> ElfMipsASEFlags[] = {
  {"DSP",                Mips::AFL_ASE_DSP},
  {"DSPR2",              Mips::AFL_ASE_DSPR2},
  {"Enhanced VA Scheme", Mips::AFL_ASE_EVA},
  {"MCU",                Mips::AFL_ASE_MCU},
  {"MDMX",               Mips::AFL_ASE_MDMX},
  {"MIPS-3D",            Mips::AFL_ASE_MIPS3D},
  {"MT",                 Mips::AFL_ASE_MT},
  {"SmartMIPS",          Mips::AFL_ASE_SMARTMIPS},
  {"VZ",                 Mips::AFL_ASE_VIRT},
  {"MSA",                Mips::AFL_ASE_MSA},
  {"MIPS16",             Mips::AFL_ASE_MIPS16},
  {"microMIPS",          Mips::AFL_ASE_MICROMIPS},
  {"XPA",                Mips::AFL_ASE_XPA},
  {"CRC",                Mips::AFL_ASE_CRC},
  {"GINV",               Mips::AFL_ASE_GINV},
};

const EnumEntry<unsigned> ElfMipsFpABIType[] = {
  {"Hard or soft float",                  Mips::Val_GNU_MIPS_ABI_FP_ANY},
  {"Hard float (double precision)",       Mips::Val_GNU_MIPS_ABI_FP_DOUBLE},
  {"Hard float (single precision)",       Mips::Val_GNU_MIPS_ABI_FP_SINGLE},
  {"Soft float",                          Mips::Val_GNU_MIPS_ABI_FP_SOFT},
  {"Hard float (MIPS32r2 64-bit FPU 12 callee-saved)",
   Mips::Val_GNU_MIPS_ABI_FP_OLD_64},
  {"Hard float (32-bit CPU, Any FPU)",    Mips::Val_GNU_MIPS_ABI_FP_XX},
  {"Hard float (32-bit CPU, 64-bit FPU)", Mips::Val_GNU_MIPS_ABI_FP_64},
  {"Hard float compat (32-bit CPU, 64-bit FPU)",
   Mips::Val_GNU_MIPS_ABI_FP_64A}
};

static const EnumEntry<unsigned> ElfMipsFlags1[] {
  {"ODDSPREG", Mips::AFL_FLAGS1_ODDSPREG},
};

static int getMipsRegisterSize(uint8_t Flag) {
  switch (Flag) {
  case Mips::AFL_REG_NONE:
    return 0;
  case Mips::AFL_REG_32:
    return 32;
  case Mips::AFL_REG_64:
    return 64;
  case Mips::AFL_REG_128:
    return 128;
  default:
    return -1;
  }
}

template <class ELFT>
static void printMipsReginfoData(ScopedPrinter &W,
                                 const Elf_Mips_RegInfo<ELFT> &Reginfo) {
  W.printHex("GP", Reginfo.ri_gp_value);
  W.printHex("General Mask", Reginfo.ri_gprmask);
  W.printHex("Co-Proc Mask0", Reginfo.ri_cprmask[0]);
  W.printHex("Co-Proc Mask1", Reginfo.ri_cprmask[1]);
  W.printHex("Co-Proc Mask2", Reginfo.ri_cprmask[2]);
  W.printHex("Co-Proc Mask3", Reginfo.ri_cprmask[3]);
}

template <class ELFT> void ELFDumper<ELFT>::printMipsReginfo() {
  const Elf_Shdr *RegInfoSec = findSectionByName(".reginfo");
  if (!RegInfoSec) {
    W.startLine() << "There is no .reginfo section in the file.\n";
    return;
  }

  Expected<ArrayRef<uint8_t>> ContentsOrErr =
      Obj.getSectionContents(*RegInfoSec);
  if (!ContentsOrErr) {
    this->reportUniqueWarning(
        "unable to read the content of the .reginfo section (" +
        describe(*RegInfoSec) + "): " + toString(ContentsOrErr.takeError()));
    return;
  }

  if (ContentsOrErr->size() < sizeof(Elf_Mips_RegInfo<ELFT>)) {
    this->reportUniqueWarning("the .reginfo section has an invalid size (0x" +
                              Twine::utohexstr(ContentsOrErr->size()) + ")");
    return;
  }

  DictScope GS(W, "MIPS RegInfo");
  printMipsReginfoData(W, *reinterpret_cast<const Elf_Mips_RegInfo<ELFT> *>(
                              ContentsOrErr->data()));
}

template <class ELFT>
static Expected<const Elf_Mips_Options<ELFT> *>
readMipsOptions(const uint8_t *SecBegin, ArrayRef<uint8_t> &SecData,
                bool &IsSupported) {
  if (SecData.size() < sizeof(Elf_Mips_Options<ELFT>))
    return createError("the .MIPS.options section has an invalid size (0x" +
                       Twine::utohexstr(SecData.size()) + ")");

  const Elf_Mips_Options<ELFT> *O =
      reinterpret_cast<const Elf_Mips_Options<ELFT> *>(SecData.data());
  const uint8_t Size = O->size;
  if (Size > SecData.size()) {
    const uint64_t Offset = SecData.data() - SecBegin;
    const uint64_t SecSize = Offset + SecData.size();
    return createError("a descriptor of size 0x" + Twine::utohexstr(Size) +
                       " at offset 0x" + Twine::utohexstr(Offset) +
                       " goes past the end of the .MIPS.options "
                       "section of size 0x" +
                       Twine::utohexstr(SecSize));
  }

  IsSupported = O->kind == ODK_REGINFO;
  const size_t ExpectedSize =
      sizeof(Elf_Mips_Options<ELFT>) + sizeof(Elf_Mips_RegInfo<ELFT>);

  if (IsSupported)
    if (Size < ExpectedSize)
      return createError(
          "a .MIPS.options entry of kind " +
          Twine(getElfMipsOptionsOdkType(O->kind)) +
          " has an invalid size (0x" + Twine::utohexstr(Size) +
          "), the expected size is 0x" + Twine::utohexstr(ExpectedSize));

  SecData = SecData.drop_front(Size);
  return O;
}

template <class ELFT> void ELFDumper<ELFT>::printMipsOptions() {
  const Elf_Shdr *MipsOpts = findSectionByName(".MIPS.options");
  if (!MipsOpts) {
    W.startLine() << "There is no .MIPS.options section in the file.\n";
    return;
  }

  DictScope GS(W, "MIPS Options");

  ArrayRef<uint8_t> Data =
      unwrapOrError(ObjF.getFileName(), Obj.getSectionContents(*MipsOpts));
  const uint8_t *const SecBegin = Data.begin();
  while (!Data.empty()) {
    bool IsSupported;
    Expected<const Elf_Mips_Options<ELFT> *> OptsOrErr =
        readMipsOptions<ELFT>(SecBegin, Data, IsSupported);
    if (!OptsOrErr) {
      reportUniqueWarning(OptsOrErr.takeError());
      break;
    }

    unsigned Kind = (*OptsOrErr)->kind;
    const char *Type = getElfMipsOptionsOdkType(Kind);
    if (!IsSupported) {
      W.startLine() << "Unsupported MIPS options tag: " << Type << " (" << Kind
                    << ")\n";
      continue;
    }

    DictScope GS(W, Type);
    if (Kind == ODK_REGINFO)
      printMipsReginfoData(W, (*OptsOrErr)->getRegInfo());
    else
      llvm_unreachable("unexpected .MIPS.options section descriptor kind");
  }
}

template <class ELFT> void ELFDumper<ELFT>::printStackMap() const {
  const Elf_Shdr *StackMapSection = findSectionByName(".llvm_stackmaps");
  if (!StackMapSection)
    return;

  auto Warn = [&](Error &&E) {
    this->reportUniqueWarning("unable to read the stack map from " +
                              describe(*StackMapSection) + ": " +
                              toString(std::move(E)));
  };

  Expected<ArrayRef<uint8_t>> ContentOrErr =
      Obj.getSectionContents(*StackMapSection);
  if (!ContentOrErr) {
    Warn(ContentOrErr.takeError());
    return;
  }

  if (Error E =
          StackMapParser<ELFT::Endianness>::validateHeader(*ContentOrErr)) {
    Warn(std::move(E));
    return;
  }

  prettyPrintStackMap(W, StackMapParser<ELFT::Endianness>(*ContentOrErr));
}

template <class ELFT>
void ELFDumper<ELFT>::printReloc(const Relocation<ELFT> &R, unsigned RelIndex,
                                 const Elf_Shdr &Sec, const Elf_Shdr *SymTab) {
  Expected<RelSymbol<ELFT>> Target = getRelocationTarget(R, SymTab);
  if (!Target)
    reportUniqueWarning("unable to print relocation " + Twine(RelIndex) +
                        " in " + describe(Sec) + ": " +
                        toString(Target.takeError()));
  else
    printRelRelaReloc(R, *Target);
}

template <class ELFT>
std::vector<EnumEntry<unsigned>>
ELFDumper<ELFT>::getOtherFlagsFromSymbol(const Elf_Ehdr &Header,
                                         const Elf_Sym &Symbol) const {
  std::vector<EnumEntry<unsigned>> SymOtherFlags(std::begin(ElfSymOtherFlags),
                                                 std::end(ElfSymOtherFlags));
  if (Header.e_machine == EM_MIPS) {
    // Someone in their infinite wisdom decided to make STO_MIPS_MIPS16
    // flag overlap with other ST_MIPS_xxx flags. So consider both
    // cases separately.
    if ((Symbol.st_other & STO_MIPS_MIPS16) == STO_MIPS_MIPS16)
      SymOtherFlags.insert(SymOtherFlags.end(),
                           std::begin(ElfMips16SymOtherFlags),
                           std::end(ElfMips16SymOtherFlags));
    else
      SymOtherFlags.insert(SymOtherFlags.end(),
                           std::begin(ElfMipsSymOtherFlags),
                           std::end(ElfMipsSymOtherFlags));
  } else if (Header.e_machine == EM_AARCH64) {
    SymOtherFlags.insert(SymOtherFlags.end(),
                         std::begin(ElfAArch64SymOtherFlags),
                         std::end(ElfAArch64SymOtherFlags));
  } else if (Header.e_machine == EM_RISCV) {
    SymOtherFlags.insert(SymOtherFlags.end(), std::begin(ElfRISCVSymOtherFlags),
                         std::end(ElfRISCVSymOtherFlags));
  }
  return SymOtherFlags;
}

static inline void printFields(formatted_raw_ostream &OS, StringRef Str1,
                               StringRef Str2) {
  OS.PadToColumn(2u);
  OS << Str1;
  OS.PadToColumn(37u);
  OS << Str2 << "\n";
  OS.flush();
}

template <class ELFT>
static std::string getSectionHeadersNumString(const ELFFile<ELFT> &Obj,
                                              StringRef FileName) {
  const typename ELFT::Ehdr &ElfHeader = Obj.getHeader();
  if (ElfHeader.e_shnum != 0)
    return to_string(ElfHeader.e_shnum);

  Expected<ArrayRef<typename ELFT::Shdr>> ArrOrErr = Obj.sections();
  if (!ArrOrErr) {
    // In this case we can ignore an error, because we have already reported a
    // warning about the broken section header table earlier.
    consumeError(ArrOrErr.takeError());
    return "<?>";
  }

  if (ArrOrErr->empty())
    return "0";
  return "0 (" + to_string((*ArrOrErr)[0].sh_size) + ")";
}

template <class ELFT>
static std::string getSectionHeaderTableIndexString(const ELFFile<ELFT> &Obj,
                                                    StringRef FileName) {
  const typename ELFT::Ehdr &ElfHeader = Obj.getHeader();
  if (ElfHeader.e_shstrndx != SHN_XINDEX)
    return to_string(ElfHeader.e_shstrndx);

  Expected<ArrayRef<typename ELFT::Shdr>> ArrOrErr = Obj.sections();
  if (!ArrOrErr) {
    // In this case we can ignore an error, because we have already reported a
    // warning about the broken section header table earlier.
    consumeError(ArrOrErr.takeError());
    return "<?>";
  }

  if (ArrOrErr->empty())
    return "65535 (corrupt: out of range)";
  return to_string(ElfHeader.e_shstrndx) + " (" +
         to_string((*ArrOrErr)[0].sh_link) + ")";
}

static const EnumEntry<unsigned> *getObjectFileEnumEntry(unsigned Type) {
  auto It = llvm::find_if(ElfObjectFileType, [&](const EnumEntry<unsigned> &E) {
    return E.Value == Type;
  });
  if (It != ArrayRef(ElfObjectFileType).end())
    return It;
  return nullptr;
}

template <class ELFT>
void GNUELFDumper<ELFT>::printFileSummary(StringRef FileStr, ObjectFile &Obj,
                                          ArrayRef<std::string> InputFilenames,
                                          const Archive *A) {
  if (InputFilenames.size() > 1 || A) {
    this->W.startLine() << "\n";
    this->W.printString("File", FileStr);
  }
}

template <class ELFT> void GNUELFDumper<ELFT>::printFileHeaders() {
  const Elf_Ehdr &e = this->Obj.getHeader();
  OS << "ELF Header:\n";
  OS << "  Magic:  ";
  std::string Str;
  for (int i = 0; i < ELF::EI_NIDENT; i++)
    OS << format(" %02x", static_cast<int>(e.e_ident[i]));
  OS << "\n";
  Str = enumToString(e.e_ident[ELF::EI_CLASS], ArrayRef(ElfClass));
  printFields(OS, "Class:", Str);
  Str = enumToString(e.e_ident[ELF::EI_DATA], ArrayRef(ElfDataEncoding));
  printFields(OS, "Data:", Str);
  OS.PadToColumn(2u);
  OS << "Version:";
  OS.PadToColumn(37u);
  OS << utohexstr(e.e_ident[ELF::EI_VERSION]);
  if (e.e_version == ELF::EV_CURRENT)
    OS << " (current)";
  OS << "\n";
  auto OSABI = ArrayRef(ElfOSABI);
  if (e.e_ident[ELF::EI_OSABI] >= ELF::ELFOSABI_FIRST_ARCH &&
      e.e_ident[ELF::EI_OSABI] <= ELF::ELFOSABI_LAST_ARCH) {
    switch (e.e_machine) {
    case ELF::EM_ARM:
      OSABI = ArrayRef(ARMElfOSABI);
      break;
    case ELF::EM_AMDGPU:
      OSABI = ArrayRef(AMDGPUElfOSABI);
      break;
    default:
      break;
    }
  }
  Str = enumToString(e.e_ident[ELF::EI_OSABI], OSABI);
  printFields(OS, "OS/ABI:", Str);
  printFields(OS,
              "ABI Version:", std::to_string(e.e_ident[ELF::EI_ABIVERSION]));

  if (const EnumEntry<unsigned> *E = getObjectFileEnumEntry(e.e_type)) {
    Str = E->AltName.str();
  } else {
    if (e.e_type >= ET_LOPROC)
      Str = "Processor Specific: (" + utohexstr(e.e_type, /*LowerCase=*/true) + ")";
    else if (e.e_type >= ET_LOOS)
      Str = "OS Specific: (" + utohexstr(e.e_type, /*LowerCase=*/true) + ")";
    else
      Str = "<unknown>: " + utohexstr(e.e_type, /*LowerCase=*/true);
  }
  printFields(OS, "Type:", Str);

  Str = enumToString(e.e_machine, ArrayRef(ElfMachineType));
  printFields(OS, "Machine:", Str);
  Str = "0x" + utohexstr(e.e_version);
  printFields(OS, "Version:", Str);
  Str = "0x" + utohexstr(e.e_entry);
  printFields(OS, "Entry point address:", Str);
  Str = to_string(e.e_phoff) + " (bytes into file)";
  printFields(OS, "Start of program headers:", Str);
  Str = to_string(e.e_shoff) + " (bytes into file)";
  printFields(OS, "Start of section headers:", Str);
  std::string ElfFlags;
  if (e.e_machine == EM_MIPS)
    ElfFlags = printFlags(
        e.e_flags, ArrayRef(ElfHeaderMipsFlags), unsigned(ELF::EF_MIPS_ARCH),
        unsigned(ELF::EF_MIPS_ABI), unsigned(ELF::EF_MIPS_MACH));
  else if (e.e_machine == EM_RISCV)
    ElfFlags = printFlags(e.e_flags, ArrayRef(ElfHeaderRISCVFlags));
  else if (e.e_machine == EM_AVR)
    ElfFlags = printFlags(e.e_flags, ArrayRef(ElfHeaderAVRFlags),
                          unsigned(ELF::EF_AVR_ARCH_MASK));
  else if (e.e_machine == EM_LOONGARCH)
    ElfFlags = printFlags(e.e_flags, ArrayRef(ElfHeaderLoongArchFlags),
                          unsigned(ELF::EF_LOONGARCH_ABI_MODIFIER_MASK),
                          unsigned(ELF::EF_LOONGARCH_OBJABI_MASK));
  else if (e.e_machine == EM_XTENSA)
    ElfFlags = printFlags(e.e_flags, ArrayRef(ElfHeaderXtensaFlags),
                          unsigned(ELF::EF_XTENSA_MACH));
  else if (e.e_machine == EM_CUDA)
    ElfFlags = printFlags(e.e_flags, ArrayRef(ElfHeaderNVPTXFlags),
                          unsigned(ELF::EF_CUDA_SM));
  else if (e.e_machine == EM_AMDGPU) {
    switch (e.e_ident[ELF::EI_ABIVERSION]) {
    default:
      break;
    case 0:
      // ELFOSABI_AMDGPU_PAL, ELFOSABI_AMDGPU_MESA3D support *_V3 flags.
      [[fallthrough]];
    case ELF::ELFABIVERSION_AMDGPU_HSA_V3:
      ElfFlags =
          printFlags(e.e_flags, ArrayRef(ElfHeaderAMDGPUFlagsABIVersion3),
                     unsigned(ELF::EF_AMDGPU_MACH));
      break;
    case ELF::ELFABIVERSION_AMDGPU_HSA_V4:
    case ELF::ELFABIVERSION_AMDGPU_HSA_V5:
      ElfFlags =
          printFlags(e.e_flags, ArrayRef(ElfHeaderAMDGPUFlagsABIVersion4),
                     unsigned(ELF::EF_AMDGPU_MACH),
                     unsigned(ELF::EF_AMDGPU_FEATURE_XNACK_V4),
                     unsigned(ELF::EF_AMDGPU_FEATURE_SRAMECC_V4));
      break;
    case ELF::ELFABIVERSION_AMDGPU_HSA_V6: {
      ElfFlags =
          printFlags(e.e_flags, ArrayRef(ElfHeaderAMDGPUFlagsABIVersion4),
                     unsigned(ELF::EF_AMDGPU_MACH),
                     unsigned(ELF::EF_AMDGPU_FEATURE_XNACK_V4),
                     unsigned(ELF::EF_AMDGPU_FEATURE_SRAMECC_V4));
      if (auto GenericV = e.e_flags & ELF::EF_AMDGPU_GENERIC_VERSION) {
        ElfFlags +=
            ", generic_v" +
            to_string(GenericV >> ELF::EF_AMDGPU_GENERIC_VERSION_OFFSET);
      }
    } break;
    }
  }
  Str = "0x" + utohexstr(e.e_flags);
  if (!ElfFlags.empty())
    Str = Str + ", " + ElfFlags;
  printFields(OS, "Flags:", Str);
  Str = to_string(e.e_ehsize) + " (bytes)";
  printFields(OS, "Size of this header:", Str);
  Str = to_string(e.e_phentsize) + " (bytes)";
  printFields(OS, "Size of program headers:", Str);
  Str = to_string(e.e_phnum);
  printFields(OS, "Number of program headers:", Str);
  Str = to_string(e.e_shentsize) + " (bytes)";
  printFields(OS, "Size of section headers:", Str);
  Str = getSectionHeadersNumString(this->Obj, this->FileName);
  printFields(OS, "Number of section headers:", Str);
  Str = getSectionHeaderTableIndexString(this->Obj, this->FileName);
  printFields(OS, "Section header string table index:", Str);
}

template <class ELFT> std::vector<GroupSection> ELFDumper<ELFT>::getGroups() {
  auto GetSignature = [&](const Elf_Sym &Sym, unsigned SymNdx,
                          const Elf_Shdr &Symtab) -> StringRef {
    Expected<StringRef> StrTableOrErr = Obj.getStringTableForSymtab(Symtab);
    if (!StrTableOrErr) {
      reportUniqueWarning("unable to get the string table for " +
                          describe(Symtab) + ": " +
                          toString(StrTableOrErr.takeError()));
      return "<?>";
    }

    StringRef Strings = *StrTableOrErr;
    if (Sym.st_name >= Strings.size()) {
      reportUniqueWarning("unable to get the name of the symbol with index " +
                          Twine(SymNdx) + ": st_name (0x" +
                          Twine::utohexstr(Sym.st_name) +
                          ") is past the end of the string table of size 0x" +
                          Twine::utohexstr(Strings.size()));
      return "<?>";
    }

    return StrTableOrErr->data() + Sym.st_name;
  };

  std::vector<GroupSection> Ret;
  uint64_t I = 0;
  for (const Elf_Shdr &Sec : cantFail(Obj.sections())) {
    ++I;
    if (Sec.sh_type != ELF::SHT_GROUP)
      continue;

    StringRef Signature = "<?>";
    if (Expected<const Elf_Shdr *> SymtabOrErr = Obj.getSection(Sec.sh_link)) {
      if (Expected<const Elf_Sym *> SymOrErr =
              Obj.template getEntry<Elf_Sym>(**SymtabOrErr, Sec.sh_info))
        Signature = GetSignature(**SymOrErr, Sec.sh_info, **SymtabOrErr);
      else
        reportUniqueWarning("unable to get the signature symbol for " +
                            describe(Sec) + ": " +
                            toString(SymOrErr.takeError()));
    } else {
      reportUniqueWarning("unable to get the symbol table for " +
                          describe(Sec) + ": " +
                          toString(SymtabOrErr.takeError()));
    }

    ArrayRef<Elf_Word> Data;
    if (Expected<ArrayRef<Elf_Word>> ContentsOrErr =
            Obj.template getSectionContentsAsArray<Elf_Word>(Sec)) {
      if (ContentsOrErr->empty())
        reportUniqueWarning("unable to read the section group flag from the " +
                            describe(Sec) + ": the section is empty");
      else
        Data = *ContentsOrErr;
    } else {
      reportUniqueWarning("unable to get the content of the " + describe(Sec) +
                          ": " + toString(ContentsOrErr.takeError()));
    }

    Ret.push_back({getPrintableSectionName(Sec),
                   maybeDemangle(Signature),
                   Sec.sh_name,
                   I - 1,
                   Sec.sh_link,
                   Sec.sh_info,
                   Data.empty() ? Elf_Word(0) : Data[0],
                   {}});

    if (Data.empty())
      continue;

    std::vector<GroupMember> &GM = Ret.back().Members;
    for (uint32_t Ndx : Data.slice(1)) {
      if (Expected<const Elf_Shdr *> SecOrErr = Obj.getSection(Ndx)) {
        GM.push_back({getPrintableSectionName(**SecOrErr), Ndx});
      } else {
        reportUniqueWarning("unable to get the section with index " +
                            Twine(Ndx) + " when dumping the " + describe(Sec) +
                            ": " + toString(SecOrErr.takeError()));
        GM.push_back({"<?>", Ndx});
      }
    }
  }
  return Ret;
}

static DenseMap<uint64_t, const GroupSection *>
mapSectionsToGroups(ArrayRef<GroupSection> Groups) {
  DenseMap<uint64_t, const GroupSection *> Ret;
  for (const GroupSection &G : Groups)
    for (const GroupMember &GM : G.Members)
      Ret.insert({GM.Index, &G});
  return Ret;
}

template <class ELFT> void GNUELFDumper<ELFT>::printGroupSections() {
  std::vector<GroupSection> V = this->getGroups();
  DenseMap<uint64_t, const GroupSection *> Map = mapSectionsToGroups(V);
  for (const GroupSection &G : V) {
    OS << "\n"
       << getGroupType(G.Type) << " group section ["
       << format_decimal(G.Index, 5) << "] `" << G.Name << "' [" << G.Signature
       << "] contains " << G.Members.size() << " sections:\n"
       << "   [Index]    Name\n";
    for (const GroupMember &GM : G.Members) {
      const GroupSection *MainGroup = Map[GM.Index];
      if (MainGroup != &G)
        this->reportUniqueWarning(
            "section with index " + Twine(GM.Index) +
            ", included in the group section with index " +
            Twine(MainGroup->Index) +
            ", was also found in the group section with index " +
            Twine(G.Index));
      OS << "   [" << format_decimal(GM.Index, 5) << "]   " << GM.Name << "\n";
    }
  }

  if (V.empty())
    OS << "There are no section groups in this file.\n";
}

template <class ELFT>
void GNUELFDumper<ELFT>::printRelRelaReloc(const Relocation<ELFT> &R,
                                           const RelSymbol<ELFT> &RelSym) {
  // First two fields are bit width dependent. The rest of them are fixed width.
  unsigned Bias = ELFT::Is64Bits ? 8 : 0;
  Field Fields[5] = {0, 10 + Bias, 19 + 2 * Bias, 42 + 2 * Bias, 53 + 2 * Bias};
  unsigned Width = ELFT::Is64Bits ? 16 : 8;

  Fields[0].Str = to_string(format_hex_no_prefix(R.Offset, Width));
  Fields[1].Str = to_string(format_hex_no_prefix(R.Info, Width));

  SmallString<32> RelocName;
  this->Obj.getRelocationTypeName(R.Type, RelocName);
  Fields[2].Str = RelocName.c_str();

  if (RelSym.Sym)
    Fields[3].Str =
        to_string(format_hex_no_prefix(RelSym.Sym->getValue(), Width));
  if (RelSym.Sym && RelSym.Name.empty())
    Fields[4].Str = "<null>";
  else
    Fields[4].Str = std::string(RelSym.Name);

  for (const Field &F : Fields)
    printField(F);

  std::string Addend;
  if (std::optional<int64_t> A = R.Addend) {
    int64_t RelAddend = *A;
    if (!Fields[4].Str.empty()) {
      if (RelAddend < 0) {
        Addend = " - ";
        RelAddend = -static_cast<uint64_t>(RelAddend);
      } else {
        Addend = " + ";
      }
    }
    Addend += utohexstr(RelAddend, /*LowerCase=*/true);
  }
  OS << Addend << "\n";
}

template <class ELFT>
static void printRelocHeaderFields(formatted_raw_ostream &OS, unsigned SType,
                                   const typename ELFT::Ehdr &EHeader,
                                   uint64_t CrelHdr = 0) {
  bool IsRela = SType == ELF::SHT_RELA || SType == ELF::SHT_ANDROID_RELA;
  if (ELFT::Is64Bits)
    OS << "    Offset             Info             Type               Symbol's "
          "Value  Symbol's Name";
  else
    OS << " Offset     Info    Type                Sym. Value  Symbol's Name";
  if (IsRela || (SType == ELF::SHT_CREL && (CrelHdr & CREL_HDR_ADDEND)))
    OS << " + Addend";
  OS << "\n";
}

template <class ELFT>
void GNUELFDumper<ELFT>::printDynamicRelocHeader(unsigned Type, StringRef Name,
                                                 const DynRegionInfo &Reg) {
  uint64_t Offset = Reg.Addr - this->Obj.base();
  OS << "\n'" << Name.str().c_str() << "' relocation section at offset 0x"
     << utohexstr(Offset, /*LowerCase=*/true);
  if (Type != ELF::SHT_CREL)
    OS << " contains " << Reg.Size << " bytes";
  OS << ":\n";
  printRelocHeaderFields<ELFT>(OS, Type, this->Obj.getHeader());
}

template <class ELFT>
static bool isRelocationSec(const typename ELFT::Shdr &Sec,
                            const typename ELFT::Ehdr &EHeader) {
  return Sec.sh_type == ELF::SHT_REL || Sec.sh_type == ELF::SHT_RELA ||
         Sec.sh_type == ELF::SHT_RELR || Sec.sh_type == ELF::SHT_CREL ||
         Sec.sh_type == ELF::SHT_ANDROID_REL ||
         Sec.sh_type == ELF::SHT_ANDROID_RELA ||
         Sec.sh_type == ELF::SHT_ANDROID_RELR ||
         (EHeader.e_machine == EM_AARCH64 &&
          Sec.sh_type == ELF::SHT_AARCH64_AUTH_RELR);
}

template <class ELFT> void GNUELFDumper<ELFT>::printRelocations() {
  auto PrintAsRelr = [&](const Elf_Shdr &Sec) {
    return Sec.sh_type == ELF::SHT_RELR ||
           Sec.sh_type == ELF::SHT_ANDROID_RELR ||
           (this->Obj.getHeader().e_machine == EM_AARCH64 &&
            Sec.sh_type == ELF::SHT_AARCH64_AUTH_RELR);
  };
  auto GetEntriesNum = [&](const Elf_Shdr &Sec) -> Expected<size_t> {
    // Android's packed relocation section needs to be unpacked first
    // to get the actual number of entries.
    if (Sec.sh_type == ELF::SHT_ANDROID_REL ||
        Sec.sh_type == ELF::SHT_ANDROID_RELA) {
      Expected<std::vector<typename ELFT::Rela>> RelasOrErr =
          this->Obj.android_relas(Sec);
      if (!RelasOrErr)
        return RelasOrErr.takeError();
      return RelasOrErr->size();
    }

    if (Sec.sh_type == ELF::SHT_CREL) {
      Expected<ArrayRef<uint8_t>> ContentsOrErr =
          this->Obj.getSectionContents(Sec);
      if (!ContentsOrErr)
        return ContentsOrErr.takeError();
      auto NumOrErr = this->Obj.getCrelHeader(*ContentsOrErr);
      if (!NumOrErr)
        return NumOrErr.takeError();
      return *NumOrErr / 8;
    }

    if (PrintAsRelr(Sec)) {
      Expected<Elf_Relr_Range> RelrsOrErr = this->Obj.relrs(Sec);
      if (!RelrsOrErr)
        return RelrsOrErr.takeError();
      return this->Obj.decode_relrs(*RelrsOrErr).size();
    }

    return Sec.getEntityCount();
  };

  bool HasRelocSections = false;
  for (const Elf_Shdr &Sec : cantFail(this->Obj.sections())) {
    if (!isRelocationSec<ELFT>(Sec, this->Obj.getHeader()))
      continue;
    HasRelocSections = true;

    std::string EntriesNum = "<?>";
    if (Expected<size_t> NumOrErr = GetEntriesNum(Sec))
      EntriesNum = std::to_string(*NumOrErr);
    else
      this->reportUniqueWarning("unable to get the number of relocations in " +
                                this->describe(Sec) + ": " +
                                toString(NumOrErr.takeError()));

    uintX_t Offset = Sec.sh_offset;
    StringRef Name = this->getPrintableSectionName(Sec);
    OS << "\nRelocation section '" << Name << "' at offset 0x"
       << utohexstr(Offset, /*LowerCase=*/true) << " contains " << EntriesNum
       << " entries:\n";

    if (PrintAsRelr(Sec)) {
      printRelr(Sec);
    } else {
      uint64_t CrelHdr = 0;
      // For CREL, read the header and call printRelocationsHelper only if
      // GetEntriesNum(Sec) succeeded.
      if (Sec.sh_type == ELF::SHT_CREL && EntriesNum != "<?>") {
        CrelHdr = cantFail(this->Obj.getCrelHeader(
            cantFail(this->Obj.getSectionContents(Sec))));
      }
      printRelocHeaderFields<ELFT>(OS, Sec.sh_type, this->Obj.getHeader(),
                                   CrelHdr);
      if (Sec.sh_type != ELF::SHT_CREL || EntriesNum != "<?>")
        this->printRelocationsHelper(Sec);
    }
  }
  if (!HasRelocSections)
    OS << "\nThere are no relocations in this file.\n";
}

template <class ELFT> void GNUELFDumper<ELFT>::printRelr(const Elf_Shdr &Sec) {
  Expected<Elf_Relr_Range> RangeOrErr = this->Obj.relrs(Sec);
  if (!RangeOrErr) {
    this->reportUniqueWarning("unable to read relocations from " +
                              this->describe(Sec) + ": " +
                              toString(RangeOrErr.takeError()));
    return;
  }
  if (ELFT::Is64Bits)
    OS << "Index: Entry            Address           Symbolic Address\n";
  else
    OS << "Index: Entry    Address   Symbolic Address\n";

  // If .symtab is available, collect its defined symbols and sort them by
  // st_value.
  SmallVector<std::pair<uint64_t, std::string>, 0> Syms;
  if (this->DotSymtabSec) {
    Elf_Sym_Range Symtab;
    std::optional<StringRef> Strtab;
    std::tie(Symtab, Strtab) = this->getSymtabAndStrtab();
    if (Symtab.size() && Strtab) {
      for (auto [I, Sym] : enumerate(Symtab)) {
        if (!Sym.st_shndx)
          continue;
        Syms.emplace_back(Sym.st_value,
                          this->getFullSymbolName(Sym, I, ArrayRef<Elf_Word>(),
                                                  *Strtab, false));
      }
    }
  }
  llvm::stable_sort(Syms);

  typename ELFT::uint Base = 0;
  size_t I = 0;
  auto Print = [&](uint64_t Where) {
    OS << format_hex_no_prefix(Where, ELFT::Is64Bits ? 16 : 8);
    for (; I < Syms.size() && Syms[I].first <= Where; ++I)
      ;
    // Try symbolizing the address. Find the nearest symbol before or at the
    // address and print the symbol and the address difference.
    if (I) {
      OS << "  " << Syms[I - 1].second;
      if (Syms[I - 1].first < Where)
        OS << " + 0x" << Twine::utohexstr(Where - Syms[I - 1].first);
    }
    OS << '\n';
  };
  for (auto [Index, R] : enumerate(*RangeOrErr)) {
    typename ELFT::uint Entry = R;
    OS << formatv("{0:4}:  ", Index)
       << format_hex_no_prefix(Entry, ELFT::Is64Bits ? 16 : 8) << ' ';
    if ((Entry & 1) == 0) {
      Print(Entry);
      Base = Entry + sizeof(typename ELFT::uint);
    } else {
      bool First = true;
      for (auto Where = Base; Entry >>= 1;
           Where += sizeof(typename ELFT::uint)) {
        if (Entry & 1) {
          if (First)
            First = false;
          else
            OS.indent(ELFT::Is64Bits ? 24 : 16);
          Print(Where);
        }
      }
      Base += (CHAR_BIT * sizeof(Entry) - 1) * sizeof(typename ELFT::uint);
    }
  }
}

// Print the offset of a particular section from anyone of the ranges:
// [SHT_LOOS, SHT_HIOS], [SHT_LOPROC, SHT_HIPROC], [SHT_LOUSER, SHT_HIUSER].
// If 'Type' does not fall within any of those ranges, then a string is
// returned as '<unknown>' followed by the type value.
static std::string getSectionTypeOffsetString(unsigned Type) {
  if (Type >= SHT_LOOS && Type <= SHT_HIOS)
    return "LOOS+0x" + utohexstr(Type - SHT_LOOS);
  else if (Type >= SHT_LOPROC && Type <= SHT_HIPROC)
    return "LOPROC+0x" + utohexstr(Type - SHT_LOPROC);
  else if (Type >= SHT_LOUSER && Type <= SHT_HIUSER)
    return "LOUSER+0x" + utohexstr(Type - SHT_LOUSER);
  return "0x" + utohexstr(Type) + ": <unknown>";
}

static std::string getSectionTypeString(unsigned Machine, unsigned Type) {
  StringRef Name = getELFSectionTypeName(Machine, Type);

  // Handle SHT_GNU_* type names.
  if (Name.consume_front("SHT_GNU_")) {
    if (Name == "HASH")
      return "GNU_HASH";
    // E.g. SHT_GNU_verneed -> VERNEED.
    return Name.upper();
  }

  if (Name == "SHT_SYMTAB_SHNDX")
    return "SYMTAB SECTION INDICES";

  if (Name.consume_front("SHT_"))
    return Name.str();
  return getSectionTypeOffsetString(Type);
}

static void printSectionDescription(formatted_raw_ostream &OS,
                                    unsigned EMachine) {
  OS << "Key to Flags:\n";
  OS << "  W (write), A (alloc), X (execute), M (merge), S (strings), I "
        "(info),\n";
  OS << "  L (link order), O (extra OS processing required), G (group), T "
        "(TLS),\n";
  OS << "  C (compressed), x (unknown), o (OS specific), E (exclude),\n";
  OS << "  R (retain)";

  if (EMachine == EM_X86_64)
    OS << ", l (large)";
  else if (EMachine == EM_ARM)
    OS << ", y (purecode)";

  OS << ", p (processor specific)\n";
}

template <class ELFT> void GNUELFDumper<ELFT>::printSectionHeaders() {
  ArrayRef<Elf_Shdr> Sections = cantFail(this->Obj.sections());
  if (Sections.empty()) {
    OS << "\nThere are no sections in this file.\n";
    Expected<StringRef> SecStrTableOrErr =
        this->Obj.getSectionStringTable(Sections, this->WarningHandler);
    if (!SecStrTableOrErr)
      this->reportUniqueWarning(SecStrTableOrErr.takeError());
    return;
  }
  unsigned Bias = ELFT::Is64Bits ? 0 : 8;
  OS << "There are " << to_string(Sections.size())
     << " section headers, starting at offset "
     << "0x" << utohexstr(this->Obj.getHeader().e_shoff, /*LowerCase=*/true) << ":\n\n";
  OS << "Section Headers:\n";
  Field Fields[11] = {
      {"[Nr]", 2},        {"Name", 7},        {"Type", 25},
      {"Address", 41},    {"Off", 58 - Bias}, {"Size", 65 - Bias},
      {"ES", 72 - Bias},  {"Flg", 75 - Bias}, {"Lk", 79 - Bias},
      {"Inf", 82 - Bias}, {"Al", 86 - Bias}};
  for (const Field &F : Fields)
    printField(F);
  OS << "\n";

  StringRef SecStrTable;
  if (Expected<StringRef> SecStrTableOrErr =
          this->Obj.getSectionStringTable(Sections, this->WarningHandler))
    SecStrTable = *SecStrTableOrErr;
  else
    this->reportUniqueWarning(SecStrTableOrErr.takeError());

  size_t SectionIndex = 0;
  for (const Elf_Shdr &Sec : Sections) {
    Fields[0].Str = to_string(SectionIndex);
    if (SecStrTable.empty())
      Fields[1].Str = "<no-strings>";
    else
      Fields[1].Str = std::string(unwrapOrError<StringRef>(
          this->FileName, this->Obj.getSectionName(Sec, SecStrTable)));
    Fields[2].Str =
        getSectionTypeString(this->Obj.getHeader().e_machine, Sec.sh_type);
    Fields[3].Str =
        to_string(format_hex_no_prefix(Sec.sh_addr, ELFT::Is64Bits ? 16 : 8));
    Fields[4].Str = to_string(format_hex_no_prefix(Sec.sh_offset, 6));
    Fields[5].Str = to_string(format_hex_no_prefix(Sec.sh_size, 6));
    Fields[6].Str = to_string(format_hex_no_prefix(Sec.sh_entsize, 2));
    Fields[7].Str = getGNUFlags(this->Obj.getHeader().e_ident[ELF::EI_OSABI],
                                this->Obj.getHeader().e_machine, Sec.sh_flags);
    Fields[8].Str = to_string(Sec.sh_link);
    Fields[9].Str = to_string(Sec.sh_info);
    Fields[10].Str = to_string(Sec.sh_addralign);

    OS.PadToColumn(Fields[0].Column);
    OS << "[" << right_justify(Fields[0].Str, 2) << "]";
    for (int i = 1; i < 7; i++)
      printField(Fields[i]);
    OS.PadToColumn(Fields[7].Column);
    OS << right_justify(Fields[7].Str, 3);
    OS.PadToColumn(Fields[8].Column);
    OS << right_justify(Fields[8].Str, 2);
    OS.PadToColumn(Fields[9].Column);
    OS << right_justify(Fields[9].Str, 3);
    OS.PadToColumn(Fields[10].Column);
    OS << right_justify(Fields[10].Str, 2);
    OS << "\n";
    ++SectionIndex;
  }
  printSectionDescription(OS, this->Obj.getHeader().e_machine);
}

template <class ELFT>
void GNUELFDumper<ELFT>::printSymtabMessage(const Elf_Shdr *Symtab,
                                            size_t Entries,
                                            bool NonVisibilityBitsUsed,
                                            bool ExtraSymInfo) const {
  StringRef Name;
  if (Symtab)
    Name = this->getPrintableSectionName(*Symtab);
  if (!Name.empty())
    OS << "\nSymbol table '" << Name << "'";
  else
    OS << "\nSymbol table for image";
  OS << " contains " << Entries << " entries:\n";

  if (ELFT::Is64Bits) {
    OS << "   Num:    Value          Size Type    Bind   Vis";
    if (ExtraSymInfo)
      OS << "+Other";
  } else {
    OS << "   Num:    Value  Size Type    Bind   Vis";
    if (ExtraSymInfo)
      OS << "+Other";
  }

  OS.PadToColumn((ELFT::Is64Bits ? 56 : 48) + (NonVisibilityBitsUsed ? 13 : 0));
  if (ExtraSymInfo)
    OS << "Ndx(SecName) Name [+ Version Info]\n";
  else
    OS << "Ndx Name\n";
}

template <class ELFT>
std::string GNUELFDumper<ELFT>::getSymbolSectionNdx(
    const Elf_Sym &Symbol, unsigned SymIndex, DataRegion<Elf_Word> ShndxTable,
    bool ExtraSymInfo) const {
  unsigned SectionIndex = Symbol.st_shndx;
  switch (SectionIndex) {
  case ELF::SHN_UNDEF:
    return "UND";
  case ELF::SHN_ABS:
    return "ABS";
  case ELF::SHN_COMMON:
    return "COM";
  case ELF::SHN_XINDEX: {
    Expected<uint32_t> IndexOrErr =
        object::getExtendedSymbolTableIndex<ELFT>(Symbol, SymIndex, ShndxTable);
    if (!IndexOrErr) {
      assert(Symbol.st_shndx == SHN_XINDEX &&
             "getExtendedSymbolTableIndex should only fail due to an invalid "
             "SHT_SYMTAB_SHNDX table/reference");
      this->reportUniqueWarning(IndexOrErr.takeError());
      return "RSV[0xffff]";
    }
    SectionIndex = *IndexOrErr;
    break;
  }
  default:
    // Find if:
    // Processor specific
    if (SectionIndex >= ELF::SHN_LOPROC && SectionIndex <= ELF::SHN_HIPROC)
      return std::string("PRC[0x") +
             to_string(format_hex_no_prefix(SectionIndex, 4)) + "]";
    // OS specific
    if (SectionIndex >= ELF::SHN_LOOS && SectionIndex <= ELF::SHN_HIOS)
      return std::string("OS[0x") +
             to_string(format_hex_no_prefix(SectionIndex, 4)) + "]";
    // Architecture reserved:
    if (SectionIndex >= ELF::SHN_LORESERVE &&
        SectionIndex <= ELF::SHN_HIRESERVE)
      return std::string("RSV[0x") +
             to_string(format_hex_no_prefix(SectionIndex, 4)) + "]";
    break;
  }

  std::string Extra;
  if (ExtraSymInfo) {
    auto Sec = this->Obj.getSection(SectionIndex);
    if (!Sec) {
      this->reportUniqueWarning(Sec.takeError());
    } else {
      auto SecName = this->Obj.getSectionName(**Sec);
      if (!SecName)
        this->reportUniqueWarning(SecName.takeError());
      else
        Extra = Twine(" (" + *SecName + ")").str();
    }
  }
  return to_string(format_decimal(SectionIndex, 3)) + Extra;
}

template <class ELFT>
void GNUELFDumper<ELFT>::printSymbol(const Elf_Sym &Symbol, unsigned SymIndex,
                                     DataRegion<Elf_Word> ShndxTable,
                                     std::optional<StringRef> StrTable,
                                     bool IsDynamic, bool NonVisibilityBitsUsed,
                                     bool ExtraSymInfo) const {
  unsigned Bias = ELFT::Is64Bits ? 8 : 0;
  Field Fields[8] = {0,         8,         17 + Bias, 23 + Bias,
                     31 + Bias, 38 + Bias, 48 + Bias, 51 + Bias};
  Fields[0].Str = to_string(format_decimal(SymIndex, 6)) + ":";
  Fields[1].Str =
      to_string(format_hex_no_prefix(Symbol.st_value, ELFT::Is64Bits ? 16 : 8));
  Fields[2].Str = to_string(format_decimal(Symbol.st_size, 5));

  unsigned char SymbolType = Symbol.getType();
  if (this->Obj.getHeader().e_machine == ELF::EM_AMDGPU &&
      SymbolType >= ELF::STT_LOOS && SymbolType < ELF::STT_HIOS)
    Fields[3].Str = enumToString(SymbolType, ArrayRef(AMDGPUSymbolTypes));
  else
    Fields[3].Str = enumToString(SymbolType, ArrayRef(ElfSymbolTypes));

  Fields[4].Str =
      enumToString(Symbol.getBinding(), ArrayRef(ElfSymbolBindings));
  Fields[5].Str =
      enumToString(Symbol.getVisibility(), ArrayRef(ElfSymbolVisibilities));

  if (Symbol.st_other & ~0x3) {
    if (this->Obj.getHeader().e_machine == ELF::EM_AARCH64) {
      uint8_t Other = Symbol.st_other & ~0x3;
      if (Other & STO_AARCH64_VARIANT_PCS) {
        Other &= ~STO_AARCH64_VARIANT_PCS;
        Fields[5].Str += " [VARIANT_PCS";
        if (Other != 0)
          Fields[5].Str.append(" | " + utohexstr(Other, /*LowerCase=*/true));
        Fields[5].Str.append("]");
      }
    } else if (this->Obj.getHeader().e_machine == ELF::EM_RISCV) {
      uint8_t Other = Symbol.st_other & ~0x3;
      if (Other & STO_RISCV_VARIANT_CC) {
        Other &= ~STO_RISCV_VARIANT_CC;
        Fields[5].Str += " [VARIANT_CC";
        if (Other != 0)
          Fields[5].Str.append(" | " + utohexstr(Other, /*LowerCase=*/true));
        Fields[5].Str.append("]");
      }
    } else {
      Fields[5].Str +=
          " [<other: " + to_string(format_hex(Symbol.st_other, 2)) + ">]";
    }
  }

  Fields[6].Column += NonVisibilityBitsUsed ? 13 : 0;
  Fields[6].Str =
      getSymbolSectionNdx(Symbol, SymIndex, ShndxTable, ExtraSymInfo);

  Fields[7].Column += ExtraSymInfo ? 10 : 0;
  Fields[7].Str = this->getFullSymbolName(Symbol, SymIndex, ShndxTable,
                                          StrTable, IsDynamic);
  for (const Field &Entry : Fields)
    printField(Entry);
  OS << "\n";
}

template <class ELFT>
void GNUELFDumper<ELFT>::printHashedSymbol(const Elf_Sym *Symbol,
                                           unsigned SymIndex,
                                           DataRegion<Elf_Word> ShndxTable,
                                           StringRef StrTable,
                                           uint32_t Bucket) {
  unsigned Bias = ELFT::Is64Bits ? 8 : 0;
  Field Fields[9] = {0,         6,         11,        20 + Bias, 25 + Bias,
                     34 + Bias, 41 + Bias, 49 + Bias, 53 + Bias};
  Fields[0].Str = to_string(format_decimal(SymIndex, 5));
  Fields[1].Str = to_string(format_decimal(Bucket, 3)) + ":";

  Fields[2].Str = to_string(
      format_hex_no_prefix(Symbol->st_value, ELFT::Is64Bits ? 16 : 8));
  Fields[3].Str = to_string(format_decimal(Symbol->st_size, 5));

  unsigned char SymbolType = Symbol->getType();
  if (this->Obj.getHeader().e_machine == ELF::EM_AMDGPU &&
      SymbolType >= ELF::STT_LOOS && SymbolType < ELF::STT_HIOS)
    Fields[4].Str = enumToString(SymbolType, ArrayRef(AMDGPUSymbolTypes));
  else
    Fields[4].Str = enumToString(SymbolType, ArrayRef(ElfSymbolTypes));

  Fields[5].Str =
      enumToString(Symbol->getBinding(), ArrayRef(ElfSymbolBindings));
  Fields[6].Str =
      enumToString(Symbol->getVisibility(), ArrayRef(ElfSymbolVisibilities));
  Fields[7].Str = getSymbolSectionNdx(*Symbol, SymIndex, ShndxTable);
  Fields[8].Str =
      this->getFullSymbolName(*Symbol, SymIndex, ShndxTable, StrTable, true);

  for (const Field &Entry : Fields)
    printField(Entry);
  OS << "\n";
}

template <class ELFT>
void GNUELFDumper<ELFT>::printSymbols(bool PrintSymbols,
                                      bool PrintDynamicSymbols,
                                      bool ExtraSymInfo) {
  if (!PrintSymbols && !PrintDynamicSymbols)
    return;
  // GNU readelf prints both the .dynsym and .symtab with --symbols.
  this->printSymbolsHelper(true, ExtraSymInfo);
  if (PrintSymbols)
    this->printSymbolsHelper(false, ExtraSymInfo);
}

template <class ELFT>
void GNUELFDumper<ELFT>::printHashTableSymbols(const Elf_Hash &SysVHash) {
  if (this->DynamicStringTable.empty())
    return;

  if (ELFT::Is64Bits)
    OS << "  Num Buc:    Value          Size   Type   Bind Vis      Ndx Name";
  else
    OS << "  Num Buc:    Value  Size   Type   Bind Vis      Ndx Name";
  OS << "\n";

  Elf_Sym_Range DynSyms = this->dynamic_symbols();
  const Elf_Sym *FirstSym = DynSyms.empty() ? nullptr : &DynSyms[0];
  if (!FirstSym) {
    this->reportUniqueWarning(
        Twine("unable to print symbols for the .hash table: the "
              "dynamic symbol table ") +
        (this->DynSymRegion ? "is empty" : "was not found"));
    return;
  }

  DataRegion<Elf_Word> ShndxTable(
      (const Elf_Word *)this->DynSymTabShndxRegion.Addr, this->Obj.end());
  auto Buckets = SysVHash.buckets();
  auto Chains = SysVHash.chains();
  for (uint32_t Buc = 0; Buc < SysVHash.nbucket; Buc++) {
    if (Buckets[Buc] == ELF::STN_UNDEF)
      continue;
    BitVector Visited(SysVHash.nchain);
    for (uint32_t Ch = Buckets[Buc]; Ch < SysVHash.nchain; Ch = Chains[Ch]) {
      if (Ch == ELF::STN_UNDEF)
        break;

      if (Visited[Ch]) {
        this->reportUniqueWarning(".hash section is invalid: bucket " +
                                  Twine(Ch) +
                                  ": a cycle was detected in the linked chain");
        break;
      }

      printHashedSymbol(FirstSym + Ch, Ch, ShndxTable, this->DynamicStringTable,
                        Buc);
      Visited[Ch] = true;
    }
  }
}

template <class ELFT>
void GNUELFDumper<ELFT>::printGnuHashTableSymbols(const Elf_GnuHash &GnuHash) {
  if (this->DynamicStringTable.empty())
    return;

  Elf_Sym_Range DynSyms = this->dynamic_symbols();
  const Elf_Sym *FirstSym = DynSyms.empty() ? nullptr : &DynSyms[0];
  if (!FirstSym) {
    this->reportUniqueWarning(
        Twine("unable to print symbols for the .gnu.hash table: the "
              "dynamic symbol table ") +
        (this->DynSymRegion ? "is empty" : "was not found"));
    return;
  }

  auto GetSymbol = [&](uint64_t SymIndex,
                       uint64_t SymsTotal) -> const Elf_Sym * {
    if (SymIndex >= SymsTotal) {
      this->reportUniqueWarning(
          "unable to print hashed symbol with index " + Twine(SymIndex) +
          ", which is greater than or equal to the number of dynamic symbols "
          "(" +
          Twine::utohexstr(SymsTotal) + ")");
      return nullptr;
    }
    return FirstSym + SymIndex;
  };

  Expected<ArrayRef<Elf_Word>> ValuesOrErr =
      getGnuHashTableChains<ELFT>(this->DynSymRegion, &GnuHash);
  ArrayRef<Elf_Word> Values;
  if (!ValuesOrErr)
    this->reportUniqueWarning("unable to get hash values for the SHT_GNU_HASH "
                              "section: " +
                              toString(ValuesOrErr.takeError()));
  else
    Values = *ValuesOrErr;

  DataRegion<Elf_Word> ShndxTable(
      (const Elf_Word *)this->DynSymTabShndxRegion.Addr, this->Obj.end());
  ArrayRef<Elf_Word> Buckets = GnuHash.buckets();
  for (uint32_t Buc = 0; Buc < GnuHash.nbuckets; Buc++) {
    if (Buckets[Buc] == ELF::STN_UNDEF)
      continue;
    uint32_t Index = Buckets[Buc];
    // Print whole chain.
    while (true) {
      uint32_t SymIndex = Index++;
      if (const Elf_Sym *Sym = GetSymbol(SymIndex, DynSyms.size()))
        printHashedSymbol(Sym, SymIndex, ShndxTable, this->DynamicStringTable,
                          Buc);
      else
        break;

      if (SymIndex < GnuHash.symndx) {
        this->reportUniqueWarning(
            "unable to read the hash value for symbol with index " +
            Twine(SymIndex) +
            ", which is less than the index of the first hashed symbol (" +
            Twine(GnuHash.symndx) + ")");
        break;
      }

       // Chain ends at symbol with stopper bit.
      if ((Values[SymIndex - GnuHash.symndx] & 1) == 1)
        break;
    }
  }
}

template <class ELFT> void GNUELFDumper<ELFT>::printHashSymbols() {
  if (this->HashTable) {
    OS << "\n Symbol table of .hash for image:\n";
    if (Error E = checkHashTable<ELFT>(*this, this->HashTable))
      this->reportUniqueWarning(std::move(E));
    else
      printHashTableSymbols(*this->HashTable);
  }

  // Try printing the .gnu.hash table.
  if (this->GnuHashTable) {
    OS << "\n Symbol table of .gnu.hash for image:\n";
    if (ELFT::Is64Bits)
      OS << "  Num Buc:    Value          Size   Type   Bind Vis      Ndx Name";
    else
      OS << "  Num Buc:    Value  Size   Type   Bind Vis      Ndx Name";
    OS << "\n";

    if (Error E = checkGNUHashTable<ELFT>(this->Obj, this->GnuHashTable))
      this->reportUniqueWarning(std::move(E));
    else
      printGnuHashTableSymbols(*this->GnuHashTable);
  }
}

template <class ELFT> void GNUELFDumper<ELFT>::printSectionDetails() {
  ArrayRef<Elf_Shdr> Sections = cantFail(this->Obj.sections());
  if (Sections.empty()) {
    OS << "\nThere are no sections in this file.\n";
    Expected<StringRef> SecStrTableOrErr =
        this->Obj.getSectionStringTable(Sections, this->WarningHandler);
    if (!SecStrTableOrErr)
      this->reportUniqueWarning(SecStrTableOrErr.takeError());
    return;
  }
  OS << "There are " << to_string(Sections.size())
     << " section headers, starting at offset "
     << "0x" << utohexstr(this->Obj.getHeader().e_shoff, /*LowerCase=*/true) << ":\n\n";

  OS << "Section Headers:\n";

  auto PrintFields = [&](ArrayRef<Field> V) {
    for (const Field &F : V)
      printField(F);
    OS << "\n";
  };

  PrintFields({{"[Nr]", 2}, {"Name", 7}});

  constexpr bool Is64 = ELFT::Is64Bits;
  PrintFields({{"Type", 7},
               {Is64 ? "Address" : "Addr", 23},
               {"Off", Is64 ? 40 : 32},
               {"Size", Is64 ? 47 : 39},
               {"ES", Is64 ? 54 : 46},
               {"Lk", Is64 ? 59 : 51},
               {"Inf", Is64 ? 62 : 54},
               {"Al", Is64 ? 66 : 57}});
  PrintFields({{"Flags", 7}});

  StringRef SecStrTable;
  if (Expected<StringRef> SecStrTableOrErr =
          this->Obj.getSectionStringTable(Sections, this->WarningHandler))
    SecStrTable = *SecStrTableOrErr;
  else
    this->reportUniqueWarning(SecStrTableOrErr.takeError());

  size_t SectionIndex = 0;
  const unsigned AddrSize = Is64 ? 16 : 8;
  for (const Elf_Shdr &S : Sections) {
    StringRef Name = "<?>";
    if (Expected<StringRef> NameOrErr =
            this->Obj.getSectionName(S, SecStrTable))
      Name = *NameOrErr;
    else
      this->reportUniqueWarning(NameOrErr.takeError());

    OS.PadToColumn(2);
    OS << "[" << right_justify(to_string(SectionIndex), 2) << "]";
    PrintFields({{Name, 7}});
    PrintFields(
        {{getSectionTypeString(this->Obj.getHeader().e_machine, S.sh_type), 7},
         {to_string(format_hex_no_prefix(S.sh_addr, AddrSize)), 23},
         {to_string(format_hex_no_prefix(S.sh_offset, 6)), Is64 ? 39 : 32},
         {to_string(format_hex_no_prefix(S.sh_size, 6)), Is64 ? 47 : 39},
         {to_string(format_hex_no_prefix(S.sh_entsize, 2)), Is64 ? 54 : 46},
         {to_string(S.sh_link), Is64 ? 59 : 51},
         {to_string(S.sh_info), Is64 ? 63 : 55},
         {to_string(S.sh_addralign), Is64 ? 66 : 58}});

    OS.PadToColumn(7);
    OS << "[" << to_string(format_hex_no_prefix(S.sh_flags, AddrSize)) << "]: ";

    DenseMap<unsigned, StringRef> FlagToName = {
        {SHF_WRITE, "WRITE"},           {SHF_ALLOC, "ALLOC"},
        {SHF_EXECINSTR, "EXEC"},        {SHF_MERGE, "MERGE"},
        {SHF_STRINGS, "STRINGS"},       {SHF_INFO_LINK, "INFO LINK"},
        {SHF_LINK_ORDER, "LINK ORDER"}, {SHF_OS_NONCONFORMING, "OS NONCONF"},
        {SHF_GROUP, "GROUP"},           {SHF_TLS, "TLS"},
        {SHF_COMPRESSED, "COMPRESSED"}, {SHF_EXCLUDE, "EXCLUDE"}};

    uint64_t Flags = S.sh_flags;
    uint64_t UnknownFlags = 0;
    ListSeparator LS;
    while (Flags) {
      // Take the least significant bit as a flag.
      uint64_t Flag = Flags & -Flags;
      Flags -= Flag;

      auto It = FlagToName.find(Flag);
      if (It != FlagToName.end())
        OS << LS << It->second;
      else
        UnknownFlags |= Flag;
    }

    auto PrintUnknownFlags = [&](uint64_t Mask, StringRef Name) {
      uint64_t FlagsToPrint = UnknownFlags & Mask;
      if (!FlagsToPrint)
        return;

      OS << LS << Name << " ("
         << to_string(format_hex_no_prefix(FlagsToPrint, AddrSize)) << ")";
      UnknownFlags &= ~Mask;
    };

    PrintUnknownFlags(SHF_MASKOS, "OS");
    PrintUnknownFlags(SHF_MASKPROC, "PROC");
    PrintUnknownFlags(uint64_t(-1), "UNKNOWN");

    OS << "\n";
    ++SectionIndex;

    if (!(S.sh_flags & SHF_COMPRESSED))
      continue;
    Expected<ArrayRef<uint8_t>> Data = this->Obj.getSectionContents(S);
    if (!Data || Data->size() < sizeof(Elf_Chdr)) {
      consumeError(Data.takeError());
      reportWarning(createError("SHF_COMPRESSED section '" + Name +
                                "' does not have an Elf_Chdr header"),
                    this->FileName);
      OS.indent(7);
      OS << "[<corrupt>]";
    } else {
      OS.indent(7);
      auto *Chdr = reinterpret_cast<const Elf_Chdr *>(Data->data());
      if (Chdr->ch_type == ELFCOMPRESS_ZLIB)
        OS << "ZLIB";
      else if (Chdr->ch_type == ELFCOMPRESS_ZSTD)
        OS << "ZSTD";
      else
        OS << format("[<unknown>: 0x%x]", unsigned(Chdr->ch_type));
      OS << ", " << format_hex_no_prefix(Chdr->ch_size, ELFT::Is64Bits ? 16 : 8)
         << ", " << Chdr->ch_addralign;
    }
    OS << '\n';
  }
}

static inline std::string printPhdrFlags(unsigned Flag) {
  std::string Str;
  Str = (Flag & PF_R) ? "R" : " ";
  Str += (Flag & PF_W) ? "W" : " ";
  Str += (Flag & PF_X) ? "E" : " ";
  return Str;
}

template <class ELFT>
static bool checkTLSSections(const typename ELFT::Phdr &Phdr,
                             const typename ELFT::Shdr &Sec) {
  if (Sec.sh_flags & ELF::SHF_TLS) {
    // .tbss must only be shown in the PT_TLS segment.
    if (Sec.sh_type == ELF::SHT_NOBITS)
      return Phdr.p_type == ELF::PT_TLS;

    // SHF_TLS sections are only shown in PT_TLS, PT_LOAD or PT_GNU_RELRO
    // segments.
    return (Phdr.p_type == ELF::PT_TLS) || (Phdr.p_type == ELF::PT_LOAD) ||
           (Phdr.p_type == ELF::PT_GNU_RELRO);
  }

  // PT_TLS must only have SHF_TLS sections.
  return Phdr.p_type != ELF::PT_TLS;
}

template <class ELFT>
static bool checkPTDynamic(const typename ELFT::Phdr &Phdr,
                           const typename ELFT::Shdr &Sec) {
  if (Phdr.p_type != ELF::PT_DYNAMIC || Phdr.p_memsz == 0 || Sec.sh_size != 0)
    return true;

  // We get here when we have an empty section. Only non-empty sections can be
  // at the start or at the end of PT_DYNAMIC.
  // Is section within the phdr both based on offset and VMA?
  bool CheckOffset = (Sec.sh_type == ELF::SHT_NOBITS) ||
                     (Sec.sh_offset > Phdr.p_offset &&
                      Sec.sh_offset < Phdr.p_offset + Phdr.p_filesz);
  bool CheckVA = !(Sec.sh_flags & ELF::SHF_ALLOC) ||
                 (Sec.sh_addr > Phdr.p_vaddr && Sec.sh_addr < Phdr.p_memsz);
  return CheckOffset && CheckVA;
}

template <class ELFT>
void GNUELFDumper<ELFT>::printProgramHeaders(
    bool PrintProgramHeaders, cl::boolOrDefault PrintSectionMapping) {
  const bool ShouldPrintSectionMapping = (PrintSectionMapping != cl::BOU_FALSE);
  // Exit early if no program header or section mapping details were requested.
  if (!PrintProgramHeaders && !ShouldPrintSectionMapping)
    return;

  if (PrintProgramHeaders) {
    const Elf_Ehdr &Header = this->Obj.getHeader();
    if (Header.e_phnum == 0) {
      OS << "\nThere are no program headers in this file.\n";
    } else {
      printProgramHeaders();
    }
  }

  if (ShouldPrintSectionMapping)
    printSectionMapping();
}

template <class ELFT> void GNUELFDumper<ELFT>::printProgramHeaders() {
  unsigned Bias = ELFT::Is64Bits ? 8 : 0;
  const Elf_Ehdr &Header = this->Obj.getHeader();
  Field Fields[8] = {2,         17,        26,        37 + Bias,
                     48 + Bias, 56 + Bias, 64 + Bias, 68 + Bias};
  OS << "\nElf file type is "
     << enumToString(Header.e_type, ArrayRef(ElfObjectFileType)) << "\n"
     << "Entry point " << format_hex(Header.e_entry, 3) << "\n"
     << "There are " << Header.e_phnum << " program headers,"
     << " starting at offset " << Header.e_phoff << "\n\n"
     << "Program Headers:\n";
  if (ELFT::Is64Bits)
    OS << "  Type           Offset   VirtAddr           PhysAddr         "
       << "  FileSiz  MemSiz   Flg Align\n";
  else
    OS << "  Type           Offset   VirtAddr   PhysAddr   FileSiz "
       << "MemSiz  Flg Align\n";

  unsigned Width = ELFT::Is64Bits ? 18 : 10;
  unsigned SizeWidth = ELFT::Is64Bits ? 8 : 7;

  Expected<ArrayRef<Elf_Phdr>> PhdrsOrErr = this->Obj.program_headers();
  if (!PhdrsOrErr) {
    this->reportUniqueWarning("unable to dump program headers: " +
                              toString(PhdrsOrErr.takeError()));
    return;
  }

  for (const Elf_Phdr &Phdr : *PhdrsOrErr) {
    Fields[0].Str = getGNUPtType(Header.e_machine, Phdr.p_type);
    Fields[1].Str = to_string(format_hex(Phdr.p_offset, 8));
    Fields[2].Str = to_string(format_hex(Phdr.p_vaddr, Width));
    Fields[3].Str = to_string(format_hex(Phdr.p_paddr, Width));
    Fields[4].Str = to_string(format_hex(Phdr.p_filesz, SizeWidth));
    Fields[5].Str = to_string(format_hex(Phdr.p_memsz, SizeWidth));
    Fields[6].Str = printPhdrFlags(Phdr.p_flags);
    Fields[7].Str = to_string(format_hex(Phdr.p_align, 1));
    for (const Field &F : Fields)
      printField(F);
    if (Phdr.p_type == ELF::PT_INTERP) {
      OS << "\n";
      auto ReportBadInterp = [&](const Twine &Msg) {
        this->reportUniqueWarning(
            "unable to read program interpreter name at offset 0x" +
            Twine::utohexstr(Phdr.p_offset) + ": " + Msg);
      };

      if (Phdr.p_offset >= this->Obj.getBufSize()) {
        ReportBadInterp("it goes past the end of the file (0x" +
                        Twine::utohexstr(this->Obj.getBufSize()) + ")");
        continue;
      }

      const char *Data =
          reinterpret_cast<const char *>(this->Obj.base()) + Phdr.p_offset;
      size_t MaxSize = this->Obj.getBufSize() - Phdr.p_offset;
      size_t Len = strnlen(Data, MaxSize);
      if (Len == MaxSize) {
        ReportBadInterp("it is not null-terminated");
        continue;
      }

      OS << "      [Requesting program interpreter: ";
      OS << StringRef(Data, Len) << "]";
    }
    OS << "\n";
  }
}

template <class ELFT> void GNUELFDumper<ELFT>::printSectionMapping() {
  OS << "\n Section to Segment mapping:\n  Segment Sections...\n";
  DenseSet<const Elf_Shdr *> BelongsToSegment;
  int Phnum = 0;

  Expected<ArrayRef<Elf_Phdr>> PhdrsOrErr = this->Obj.program_headers();
  if (!PhdrsOrErr) {
    this->reportUniqueWarning(
        "can't read program headers to build section to segment mapping: " +
        toString(PhdrsOrErr.takeError()));
    return;
  }

  for (const Elf_Phdr &Phdr : *PhdrsOrErr) {
    std::string Sections;
    OS << format("   %2.2d     ", Phnum++);
    // Check if each section is in a segment and then print mapping.
    for (const Elf_Shdr &Sec : cantFail(this->Obj.sections())) {
      if (Sec.sh_type == ELF::SHT_NULL)
        continue;

      // readelf additionally makes sure it does not print zero sized sections
      // at end of segments and for PT_DYNAMIC both start and end of section
      // .tbss must only be shown in PT_TLS section.
      if (isSectionInSegment<ELFT>(Phdr, Sec) &&
          checkTLSSections<ELFT>(Phdr, Sec) &&
          checkPTDynamic<ELFT>(Phdr, Sec)) {
        Sections +=
            unwrapOrError(this->FileName, this->Obj.getSectionName(Sec)).str() +
            " ";
        BelongsToSegment.insert(&Sec);
      }
    }
    OS << Sections << "\n";
    OS.flush();
  }

  // Display sections that do not belong to a segment.
  std::string Sections;
  for (const Elf_Shdr &Sec : cantFail(this->Obj.sections())) {
    if (BelongsToSegment.find(&Sec) == BelongsToSegment.end())
      Sections +=
          unwrapOrError(this->FileName, this->Obj.getSectionName(Sec)).str() +
          ' ';
  }
  if (!Sections.empty()) {
    OS << "   None  " << Sections << '\n';
    OS.flush();
  }
}

namespace {

template <class ELFT>
RelSymbol<ELFT> getSymbolForReloc(const ELFDumper<ELFT> &Dumper,
                                  const Relocation<ELFT> &Reloc) {
  using Elf_Sym = typename ELFT::Sym;
  auto WarnAndReturn = [&](const Elf_Sym *Sym,
                           const Twine &Reason) -> RelSymbol<ELFT> {
    Dumper.reportUniqueWarning(
        "unable to get name of the dynamic symbol with index " +
        Twine(Reloc.Symbol) + ": " + Reason);
    return {Sym, "<corrupt>"};
  };

  ArrayRef<Elf_Sym> Symbols = Dumper.dynamic_symbols();
  const Elf_Sym *FirstSym = Symbols.begin();
  if (!FirstSym)
    return WarnAndReturn(nullptr, "no dynamic symbol table found");

  // We might have an object without a section header. In this case the size of
  // Symbols is zero, because there is no way to know the size of the dynamic
  // table. We should allow this case and not print a warning.
  if (!Symbols.empty() && Reloc.Symbol >= Symbols.size())
    return WarnAndReturn(
        nullptr,
        "index is greater than or equal to the number of dynamic symbols (" +
            Twine(Symbols.size()) + ")");

  const ELFFile<ELFT> &Obj = Dumper.getElfObject().getELFFile();
  const uint64_t FileSize = Obj.getBufSize();
  const uint64_t SymOffset = ((const uint8_t *)FirstSym - Obj.base()) +
                             (uint64_t)Reloc.Symbol * sizeof(Elf_Sym);
  if (SymOffset + sizeof(Elf_Sym) > FileSize)
    return WarnAndReturn(nullptr, "symbol at 0x" + Twine::utohexstr(SymOffset) +
                                      " goes past the end of the file (0x" +
                                      Twine::utohexstr(FileSize) + ")");

  const Elf_Sym *Sym = FirstSym + Reloc.Symbol;
  Expected<StringRef> ErrOrName = Sym->getName(Dumper.getDynamicStringTable());
  if (!ErrOrName)
    return WarnAndReturn(Sym, toString(ErrOrName.takeError()));

  return {Sym == FirstSym ? nullptr : Sym, maybeDemangle(*ErrOrName)};
}
} // namespace

template <class ELFT>
static size_t getMaxDynamicTagSize(const ELFFile<ELFT> &Obj,
                                   typename ELFT::DynRange Tags) {
  size_t Max = 0;
  for (const typename ELFT::Dyn &Dyn : Tags)
    Max = std::max(Max, Obj.getDynamicTagAsString(Dyn.d_tag).size());
  return Max;
}

template <class ELFT> void GNUELFDumper<ELFT>::printDynamicTable() {
  Elf_Dyn_Range Table = this->dynamic_table();
  if (Table.empty())
    return;

  OS << "Dynamic section at offset "
     << format_hex(reinterpret_cast<const uint8_t *>(this->DynamicTable.Addr) -
                       this->Obj.base(),
                   1)
     << " contains " << Table.size() << " entries:\n";

  // The type name is surrounded with round brackets, hence add 2.
  size_t MaxTagSize = getMaxDynamicTagSize(this->Obj, Table) + 2;
  // The "Name/Value" column should be indented from the "Type" column by N
  // spaces, where N = MaxTagSize - length of "Type" (4) + trailing
  // space (1) = 3.
  OS << "  Tag" + std::string(ELFT::Is64Bits ? 16 : 8, ' ') + "Type"
     << std::string(MaxTagSize - 3, ' ') << "Name/Value\n";

  std::string ValueFmt = " %-" + std::to_string(MaxTagSize) + "s ";
  for (auto Entry : Table) {
    uintX_t Tag = Entry.getTag();
    std::string Type =
        std::string("(") + this->Obj.getDynamicTagAsString(Tag) + ")";
    std::string Value = this->getDynamicEntry(Tag, Entry.getVal());
    OS << "  " << format_hex(Tag, ELFT::Is64Bits ? 18 : 10)
       << format(ValueFmt.c_str(), Type.c_str()) << Value << "\n";
  }
}

template <class ELFT> void GNUELFDumper<ELFT>::printDynamicRelocations() {
  this->printDynamicRelocationsHelper();
}

template <class ELFT>
void ELFDumper<ELFT>::printDynamicReloc(const Relocation<ELFT> &R) {
  printRelRelaReloc(R, getSymbolForReloc(*this, R));
}

template <class ELFT>
void ELFDumper<ELFT>::printRelocationsHelper(const Elf_Shdr &Sec) {
  this->forEachRelocationDo(
      Sec, [&](const Relocation<ELFT> &R, unsigned Ndx, const Elf_Shdr &Sec,
               const Elf_Shdr *SymTab) { printReloc(R, Ndx, Sec, SymTab); });
}

template <class ELFT> void ELFDumper<ELFT>::printDynamicRelocationsHelper() {
  const bool IsMips64EL = this->Obj.isMips64EL();
  auto DumpCrelRegion = [&](DynRegionInfo &Region) {
    // While the size is unknown, a valid CREL has at least one byte. We can
    // check whether Addr is in bounds, and then decode CREL until the file
    // end.
    Region.Size = Region.EntSize = 1;
    if (!Region.template getAsArrayRef<uint8_t>().empty()) {
      const uint64_t Offset =
          Region.Addr - reinterpret_cast<const uint8_t *>(
                            ObjF.getMemoryBufferRef().getBufferStart());
      const uint64_t ObjSize = ObjF.getMemoryBufferRef().getBufferSize();
      auto RelsOrRelas =
          Obj.decodeCrel(ArrayRef<uint8_t>(Region.Addr, ObjSize - Offset));
      if (!RelsOrRelas) {
        reportUniqueWarning(toString(RelsOrRelas.takeError()));
      } else {
        for (const Elf_Rel &R : RelsOrRelas->first)
          printDynamicReloc(Relocation<ELFT>(R, false));
        for (const Elf_Rela &R : RelsOrRelas->second)
          printDynamicReloc(Relocation<ELFT>(R, false));
      }
    }
  };

  if (this->DynCrelRegion.Addr) {
    printDynamicRelocHeader(ELF::SHT_CREL, "CREL", this->DynCrelRegion);
    DumpCrelRegion(this->DynCrelRegion);
  }

  if (this->DynRelaRegion.Size > 0) {
    printDynamicRelocHeader(ELF::SHT_RELA, "RELA", this->DynRelaRegion);
    for (const Elf_Rela &Rela :
         this->DynRelaRegion.template getAsArrayRef<Elf_Rela>())
      printDynamicReloc(Relocation<ELFT>(Rela, IsMips64EL));
  }

  if (this->DynRelRegion.Size > 0) {
    printDynamicRelocHeader(ELF::SHT_REL, "REL", this->DynRelRegion);
    for (const Elf_Rel &Rel :
         this->DynRelRegion.template getAsArrayRef<Elf_Rel>())
      printDynamicReloc(Relocation<ELFT>(Rel, IsMips64EL));
  }

  if (this->DynRelrRegion.Size > 0) {
    printDynamicRelocHeader(ELF::SHT_REL, "RELR", this->DynRelrRegion);
    Elf_Relr_Range Relrs =
        this->DynRelrRegion.template getAsArrayRef<Elf_Relr>();
    for (const Elf_Rel &Rel : Obj.decode_relrs(Relrs))
      printDynamicReloc(Relocation<ELFT>(Rel, IsMips64EL));
  }

  if (this->DynPLTRelRegion.Size) {
    if (this->DynPLTRelRegion.EntSize == sizeof(Elf_Rela)) {
      printDynamicRelocHeader(ELF::SHT_RELA, "PLT", this->DynPLTRelRegion);
      for (const Elf_Rela &Rela :
           this->DynPLTRelRegion.template getAsArrayRef<Elf_Rela>())
        printDynamicReloc(Relocation<ELFT>(Rela, IsMips64EL));
    } else if (this->DynPLTRelRegion.EntSize == 1) {
      DumpCrelRegion(this->DynPLTRelRegion);
    } else {
      printDynamicRelocHeader(ELF::SHT_REL, "PLT", this->DynPLTRelRegion);
      for (const Elf_Rel &Rel :
           this->DynPLTRelRegion.template getAsArrayRef<Elf_Rel>())
        printDynamicReloc(Relocation<ELFT>(Rel, IsMips64EL));
    }
  }
}

template <class ELFT>
void GNUELFDumper<ELFT>::printGNUVersionSectionProlog(
    const typename ELFT::Shdr &Sec, const Twine &Label, unsigned EntriesNum) {
  // Don't inline the SecName, because it might report a warning to stderr and
  // corrupt the output.
  StringRef SecName = this->getPrintableSectionName(Sec);
  OS << Label << " section '" << SecName << "' "
     << "contains " << EntriesNum << " entries:\n";

  StringRef LinkedSecName = "<corrupt>";
  if (Expected<const typename ELFT::Shdr *> LinkedSecOrErr =
          this->Obj.getSection(Sec.sh_link))
    LinkedSecName = this->getPrintableSectionName(**LinkedSecOrErr);
  else
    this->reportUniqueWarning("invalid section linked to " +
                              this->describe(Sec) + ": " +
                              toString(LinkedSecOrErr.takeError()));

  OS << " Addr: " << format_hex_no_prefix(Sec.sh_addr, 16)
     << "  Offset: " << format_hex(Sec.sh_offset, 8)
     << "  Link: " << Sec.sh_link << " (" << LinkedSecName << ")\n";
}

template <class ELFT>
void GNUELFDumper<ELFT>::printVersionSymbolSection(const Elf_Shdr *Sec) {
  if (!Sec)
    return;

  printGNUVersionSectionProlog(*Sec, "Version symbols",
                               Sec->sh_size / sizeof(Elf_Versym));
  Expected<ArrayRef<Elf_Versym>> VerTableOrErr =
      this->getVersionTable(*Sec, /*SymTab=*/nullptr,
                            /*StrTab=*/nullptr, /*SymTabSec=*/nullptr);
  if (!VerTableOrErr) {
    this->reportUniqueWarning(VerTableOrErr.takeError());
    return;
  }

  SmallVector<std::optional<VersionEntry>, 0> *VersionMap = nullptr;
  if (Expected<SmallVector<std::optional<VersionEntry>, 0> *> MapOrErr =
          this->getVersionMap())
    VersionMap = *MapOrErr;
  else
    this->reportUniqueWarning(MapOrErr.takeError());

  ArrayRef<Elf_Versym> VerTable = *VerTableOrErr;
  std::vector<StringRef> Versions;
  for (size_t I = 0, E = VerTable.size(); I < E; ++I) {
    unsigned Ndx = VerTable[I].vs_index;
    if (Ndx == VER_NDX_LOCAL || Ndx == VER_NDX_GLOBAL) {
      Versions.emplace_back(Ndx == VER_NDX_LOCAL ? "*local*" : "*global*");
      continue;
    }

    if (!VersionMap) {
      Versions.emplace_back("<corrupt>");
      continue;
    }

    bool IsDefault;
    Expected<StringRef> NameOrErr = this->Obj.getSymbolVersionByIndex(
        Ndx, IsDefault, *VersionMap, /*IsSymHidden=*/std::nullopt);
    if (!NameOrErr) {
      this->reportUniqueWarning("unable to get a version for entry " +
                                Twine(I) + " of " + this->describe(*Sec) +
                                ": " + toString(NameOrErr.takeError()));
      Versions.emplace_back("<corrupt>");
      continue;
    }
    Versions.emplace_back(*NameOrErr);
  }

  // readelf prints 4 entries per line.
  uint64_t Entries = VerTable.size();
  for (uint64_t VersymRow = 0; VersymRow < Entries; VersymRow += 4) {
    OS << "  " << format_hex_no_prefix(VersymRow, 3) << ":";
    for (uint64_t I = 0; (I < 4) && (I + VersymRow) < Entries; ++I) {
      unsigned Ndx = VerTable[VersymRow + I].vs_index;
      OS << format("%4x%c", Ndx & VERSYM_VERSION,
                   Ndx & VERSYM_HIDDEN ? 'h' : ' ');
      OS << left_justify("(" + std::string(Versions[VersymRow + I]) + ")", 13);
    }
    OS << '\n';
  }
  OS << '\n';
}

static std::string versionFlagToString(unsigned Flags) {
  if (Flags == 0)
    return "none";

  std::string Ret;
  auto AddFlag = [&Ret, &Flags](unsigned Flag, StringRef Name) {
    if (!(Flags & Flag))
      return;
    if (!Ret.empty())
      Ret += " | ";
    Ret += Name;
    Flags &= ~Flag;
  };

  AddFlag(VER_FLG_BASE, "BASE");
  AddFlag(VER_FLG_WEAK, "WEAK");
  AddFlag(VER_FLG_INFO, "INFO");
  AddFlag(~0, "<unknown>");
  return Ret;
}

template <class ELFT>
void GNUELFDumper<ELFT>::printVersionDefinitionSection(const Elf_Shdr *Sec) {
  if (!Sec)
    return;

  printGNUVersionSectionProlog(*Sec, "Version definition", Sec->sh_info);

  Expected<std::vector<VerDef>> V = this->Obj.getVersionDefinitions(*Sec);
  if (!V) {
    this->reportUniqueWarning(V.takeError());
    return;
  }

  for (const VerDef &Def : *V) {
    OS << format("  0x%04x: Rev: %u  Flags: %s  Index: %u  Cnt: %u  Name: %s\n",
                 Def.Offset, Def.Version,
                 versionFlagToString(Def.Flags).c_str(), Def.Ndx, Def.Cnt,
                 Def.Name.data());
    unsigned I = 0;
    for (const VerdAux &Aux : Def.AuxV)
      OS << format("  0x%04x: Parent %u: %s\n", Aux.Offset, ++I,
                   Aux.Name.data());
  }

  OS << '\n';
}

template <class ELFT>
void GNUELFDumper<ELFT>::printVersionDependencySection(const Elf_Shdr *Sec) {
  if (!Sec)
    return;

  unsigned VerneedNum = Sec->sh_info;
  printGNUVersionSectionProlog(*Sec, "Version needs", VerneedNum);

  Expected<std::vector<VerNeed>> V =
      this->Obj.getVersionDependencies(*Sec, this->WarningHandler);
  if (!V) {
    this->reportUniqueWarning(V.takeError());
    return;
  }

  for (const VerNeed &VN : *V) {
    OS << format("  0x%04x: Version: %u  File: %s  Cnt: %u\n", VN.Offset,
                 VN.Version, VN.File.data(), VN.Cnt);
    for (const VernAux &Aux : VN.AuxV)
      OS << format("  0x%04x:   Name: %s  Flags: %s  Version: %u\n", Aux.Offset,
                   Aux.Name.data(), versionFlagToString(Aux.Flags).c_str(),
                   Aux.Other);
  }
  OS << '\n';
}

template <class ELFT>
void GNUELFDumper<ELFT>::printHashHistogramStats(size_t NBucket,
                                                 size_t MaxChain,
                                                 size_t TotalSyms,
                                                 ArrayRef<size_t> Count,
                                                 bool IsGnu) const {
  size_t CumulativeNonZero = 0;
  OS << "Histogram for" << (IsGnu ? " `.gnu.hash'" : "")
     << " bucket list length (total of " << NBucket << " buckets)\n"
     << " Length  Number     % of total  Coverage\n";
  for (size_t I = 0; I < MaxChain; ++I) {
    CumulativeNonZero += Count[I] * I;
    OS << format("%7lu  %-10lu (%5.1f%%)     %5.1f%%\n", I, Count[I],
                 (Count[I] * 100.0) / NBucket,
                 (CumulativeNonZero * 100.0) / TotalSyms);
  }
}

template <class ELFT> void GNUELFDumper<ELFT>::printCGProfile() {
  OS << "GNUStyle::printCGProfile not implemented\n";
}

template <class ELFT>
void GNUELFDumper<ELFT>::printBBAddrMaps(bool /*PrettyPGOAnalysis*/) {
  OS << "GNUStyle::printBBAddrMaps not implemented\n";
}

static Expected<std::vector<uint64_t>> toULEB128Array(ArrayRef<uint8_t> Data) {
  std::vector<uint64_t> Ret;
  const uint8_t *Cur = Data.begin();
  const uint8_t *End = Data.end();
  while (Cur != End) {
    unsigned Size;
    const char *Err = nullptr;
    Ret.push_back(decodeULEB128(Cur, &Size, End, &Err));
    if (Err)
      return createError(Err);
    Cur += Size;
  }
  return Ret;
}

template <class ELFT>
static Expected<std::vector<uint64_t>>
decodeAddrsigSection(const ELFFile<ELFT> &Obj, const typename ELFT::Shdr &Sec) {
  Expected<ArrayRef<uint8_t>> ContentsOrErr = Obj.getSectionContents(Sec);
  if (!ContentsOrErr)
    return ContentsOrErr.takeError();

  if (Expected<std::vector<uint64_t>> SymsOrErr =
          toULEB128Array(*ContentsOrErr))
    return *SymsOrErr;
  else
    return createError("unable to decode " + describe(Obj, Sec) + ": " +
                       toString(SymsOrErr.takeError()));
}

template <class ELFT> void GNUELFDumper<ELFT>::printAddrsig() {
  if (!this->DotAddrsigSec)
    return;

  Expected<std::vector<uint64_t>> SymsOrErr =
      decodeAddrsigSection(this->Obj, *this->DotAddrsigSec);
  if (!SymsOrErr) {
    this->reportUniqueWarning(SymsOrErr.takeError());
    return;
  }

  StringRef Name = this->getPrintableSectionName(*this->DotAddrsigSec);
  OS << "\nAddress-significant symbols section '" << Name << "'"
     << " contains " << SymsOrErr->size() << " entries:\n";
  OS << "   Num: Name\n";

  Field Fields[2] = {0, 8};
  size_t SymIndex = 0;
  for (uint64_t Sym : *SymsOrErr) {
    Fields[0].Str = to_string(format_decimal(++SymIndex, 6)) + ":";
    Fields[1].Str = this->getStaticSymbolName(Sym);
    for (const Field &Entry : Fields)
      printField(Entry);
    OS << "\n";
  }
}

template <class ELFT>
static bool printAArch64PAuthABICoreInfo(raw_ostream &OS, uint32_t DataSize,
                                         ArrayRef<uint8_t> Desc) {
  OS << "    AArch64 PAuth ABI core info: ";
  // DataSize - size without padding, Desc.size() - size with padding
  if (DataSize != 16) {
    OS << format("<corrupted size: expected 16, got %d>", DataSize);
    return false;
  }

  uint64_t Platform =
      support::endian::read64<ELFT::Endianness>(Desc.data() + 0);
  uint64_t Version = support::endian::read64<ELFT::Endianness>(Desc.data() + 8);

  const char *PlatformDesc = [Platform]() {
    switch (Platform) {
    case AARCH64_PAUTH_PLATFORM_INVALID:
      return "invalid";
    case AARCH64_PAUTH_PLATFORM_BAREMETAL:
      return "baremetal";
    case AARCH64_PAUTH_PLATFORM_LLVM_LINUX:
      return "llvm_linux";
    default:
      return "unknown";
    }
  }();

  std::string VersionDesc = [Platform, Version]() -> std::string {
    if (Platform != AARCH64_PAUTH_PLATFORM_LLVM_LINUX)
      return "";
    if (Version >= (1 << (AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_LAST + 1)))
      return "unknown";

    std::array<StringRef, AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_LAST + 1>
        Flags;
    Flags[AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INTRINSICS] = "Intrinsics";
    Flags[AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_CALLS] = "Calls";
    Flags[AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_RETURNS] = "Returns";
    Flags[AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_AUTHTRAPS] = "AuthTraps";
    Flags[AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_VPTRADDRDISCR] =
        "VTPtrAddressDiscrimination";
    Flags[AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_VPTRTYPEDISCR] =
        "VTPtrTypeDiscrimination";
    Flags[AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INITFINI] = "InitFini";

    static_assert(AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_INITFINI ==
                      AARCH64_PAUTH_PLATFORM_LLVM_LINUX_VERSION_LAST,
                  "Update when new enum items are defined");

    std::string Desc;
    for (uint32_t I = 0, End = Flags.size(); I < End; ++I) {
      if (!(Version & (1ULL << I)))
        Desc += '!';
      Desc +=
          Twine("PointerAuth" + Flags[I] + (I == End - 1 ? "" : ", ")).str();
    }
    return Desc;
  }();

  OS << format("platform 0x%" PRIx64 " (%s), version 0x%" PRIx64, Platform,
               PlatformDesc, Version);
  if (!VersionDesc.empty())
    OS << format(" (%s)", VersionDesc.c_str());

  return true;
}

template <typename ELFT>
static std::string getGNUProperty(uint32_t Type, uint32_t DataSize,
                                  ArrayRef<uint8_t> Data) {
  std::string str;
  raw_string_ostream OS(str);
  uint32_t PrData;
  auto DumpBit = [&](uint32_t Flag, StringRef Name) {
    if (PrData & Flag) {
      PrData &= ~Flag;
      OS << Name;
      if (PrData)
        OS << ", ";
    }
  };

  switch (Type) {
  default:
    OS << format("<application-specific type 0x%x>", Type);
    return OS.str();
  case GNU_PROPERTY_STACK_SIZE: {
    OS << "stack size: ";
    if (DataSize == sizeof(typename ELFT::uint))
      OS << formatv("{0:x}",
                    (uint64_t)(*(const typename ELFT::Addr *)Data.data()));
    else
      OS << format("<corrupt length: 0x%x>", DataSize);
    return OS.str();
  }
  case GNU_PROPERTY_NO_COPY_ON_PROTECTED:
    OS << "no copy on protected";
    if (DataSize)
      OS << format(" <corrupt length: 0x%x>", DataSize);
    return OS.str();
  case GNU_PROPERTY_AARCH64_FEATURE_1_AND:
  case GNU_PROPERTY_X86_FEATURE_1_AND:
    OS << ((Type == GNU_PROPERTY_AARCH64_FEATURE_1_AND) ? "aarch64 feature: "
                                                        : "x86 feature: ");
    if (DataSize != 4) {
      OS << format("<corrupt length: 0x%x>", DataSize);
      return OS.str();
    }
    PrData = endian::read32<ELFT::Endianness>(Data.data());
    if (PrData == 0) {
      OS << "<None>";
      return OS.str();
    }
    if (Type == GNU_PROPERTY_AARCH64_FEATURE_1_AND) {
      DumpBit(GNU_PROPERTY_AARCH64_FEATURE_1_BTI, "BTI");
      DumpBit(GNU_PROPERTY_AARCH64_FEATURE_1_PAC, "PAC");
      DumpBit(GNU_PROPERTY_AARCH64_FEATURE_1_GCS, "GCS");
    } else {
      DumpBit(GNU_PROPERTY_X86_FEATURE_1_IBT, "IBT");
      DumpBit(GNU_PROPERTY_X86_FEATURE_1_SHSTK, "SHSTK");
    }
    if (PrData)
      OS << format("<unknown flags: 0x%x>", PrData);
    return OS.str();
  case GNU_PROPERTY_AARCH64_FEATURE_PAUTH:
    printAArch64PAuthABICoreInfo<ELFT>(OS, DataSize, Data);
    return OS.str();
  case GNU_PROPERTY_X86_FEATURE_2_NEEDED:
  case GNU_PROPERTY_X86_FEATURE_2_USED:
    OS << "x86 feature "
       << (Type == GNU_PROPERTY_X86_FEATURE_2_NEEDED ? "needed: " : "used: ");
    if (DataSize != 4) {
      OS << format("<corrupt length: 0x%x>", DataSize);
      return OS.str();
    }
    PrData = endian::read32<ELFT::Endianness>(Data.data());
    if (PrData == 0) {
      OS << "<None>";
      return OS.str();
    }
    DumpBit(GNU_PROPERTY_X86_FEATURE_2_X86, "x86");
    DumpBit(GNU_PROPERTY_X86_FEATURE_2_X87, "x87");
    DumpBit(GNU_PROPERTY_X86_FEATURE_2_MMX, "MMX");
    DumpBit(GNU_PROPERTY_X86_FEATURE_2_XMM, "XMM");
    DumpBit(GNU_PROPERTY_X86_FEATURE_2_YMM, "YMM");
    DumpBit(GNU_PROPERTY_X86_FEATURE_2_ZMM, "ZMM");
    DumpBit(GNU_PROPERTY_X86_FEATURE_2_FXSR, "FXSR");
    DumpBit(GNU_PROPERTY_X86_FEATURE_2_XSAVE, "XSAVE");
    DumpBit(GNU_PROPERTY_X86_FEATURE_2_XSAVEOPT, "XSAVEOPT");
    DumpBit(GNU_PROPERTY_X86_FEATURE_2_XSAVEC, "XSAVEC");
    if (PrData)
      OS << format("<unknown flags: 0x%x>", PrData);
    return OS.str();
  case GNU_PROPERTY_X86_ISA_1_NEEDED:
  case GNU_PROPERTY_X86_ISA_1_USED:
    OS << "x86 ISA "
       << (Type == GNU_PROPERTY_X86_ISA_1_NEEDED ? "needed: " : "used: ");
    if (DataSize != 4) {
      OS << format("<corrupt length: 0x%x>", DataSize);
      return OS.str();
    }
    PrData = endian::read32<ELFT::Endianness>(Data.data());
    if (PrData == 0) {
      OS << "<None>";
      return OS.str();
    }
    DumpBit(GNU_PROPERTY_X86_ISA_1_BASELINE, "x86-64-baseline");
    DumpBit(GNU_PROPERTY_X86_ISA_1_V2, "x86-64-v2");
    DumpBit(GNU_PROPERTY_X86_ISA_1_V3, "x86-64-v3");
    DumpBit(GNU_PROPERTY_X86_ISA_1_V4, "x86-64-v4");
    if (PrData)
      OS << format("<unknown flags: 0x%x>", PrData);
    return OS.str();
  }
}

template <typename ELFT>
static SmallVector<std::string, 4> getGNUPropertyList(ArrayRef<uint8_t> Arr) {
  using Elf_Word = typename ELFT::Word;

  SmallVector<std::string, 4> Properties;
  while (Arr.size() >= 8) {
    uint32_t Type = *reinterpret_cast<const Elf_Word *>(Arr.data());
    uint32_t DataSize = *reinterpret_cast<const Elf_Word *>(Arr.data() + 4);
    Arr = Arr.drop_front(8);

    // Take padding size into account if present.
    uint64_t PaddedSize = alignTo(DataSize, sizeof(typename ELFT::uint));
    std::string str;
    raw_string_ostream OS(str);
    if (Arr.size() < PaddedSize) {
      OS << format("<corrupt type (0x%x) datasz: 0x%x>", Type, DataSize);
      Properties.push_back(OS.str());
      break;
    }
    Properties.push_back(
        getGNUProperty<ELFT>(Type, DataSize, Arr.take_front(PaddedSize)));
    Arr = Arr.drop_front(PaddedSize);
  }

  if (!Arr.empty())
    Properties.push_back("<corrupted GNU_PROPERTY_TYPE_0>");

  return Properties;
}

struct GNUAbiTag {
  std::string OSName;
  std::string ABI;
  bool IsValid;
};

template <typename ELFT> static GNUAbiTag getGNUAbiTag(ArrayRef<uint8_t> Desc) {
  typedef typename ELFT::Word Elf_Word;

  ArrayRef<Elf_Word> Words(reinterpret_cast<const Elf_Word *>(Desc.begin()),
                           reinterpret_cast<const Elf_Word *>(Desc.end()));

  if (Words.size() < 4)
    return {"", "", /*IsValid=*/false};

  static const char *OSNames[] = {
      "Linux", "Hurd", "Solaris", "FreeBSD", "NetBSD", "Syllable", "NaCl",
  };
  StringRef OSName = "Unknown";
  if (Words[0] < std::size(OSNames))
    OSName = OSNames[Words[0]];
  uint32_t Major = Words[1], Minor = Words[2], Patch = Words[3];
  std::string str;
  raw_string_ostream ABI(str);
  ABI << Major << "." << Minor << "." << Patch;
  return {std::string(OSName), ABI.str(), /*IsValid=*/true};
}

static std::string getGNUBuildId(ArrayRef<uint8_t> Desc) {
  std::string str;
  raw_string_ostream OS(str);
  for (uint8_t B : Desc)
    OS << format_hex_no_prefix(B, 2);
  return OS.str();
}

static StringRef getDescAsStringRef(ArrayRef<uint8_t> Desc) {
  return StringRef(reinterpret_cast<const char *>(Desc.data()), Desc.size());
}

template <typename ELFT>
static bool printGNUNote(raw_ostream &OS, uint32_t NoteType,
                         ArrayRef<uint8_t> Desc) {
  // Return true if we were able to pretty-print the note, false otherwise.
  switch (NoteType) {
  default:
    return false;
  case ELF::NT_GNU_ABI_TAG: {
    const GNUAbiTag &AbiTag = getGNUAbiTag<ELFT>(Desc);
    if (!AbiTag.IsValid)
      OS << "    <corrupt GNU_ABI_TAG>";
    else
      OS << "    OS: " << AbiTag.OSName << ", ABI: " << AbiTag.ABI;
    break;
  }
  case ELF::NT_GNU_BUILD_ID: {
    OS << "    Build ID: " << getGNUBuildId(Desc);
    break;
  }
  case ELF::NT_GNU_GOLD_VERSION:
    OS << "    Version: " << getDescAsStringRef(Desc);
    break;
  case ELF::NT_GNU_PROPERTY_TYPE_0:
    OS << "    Properties:";
    for (const std::string &Property : getGNUPropertyList<ELFT>(Desc))
      OS << "    " << Property << "\n";
    break;
  }
  OS << '\n';
  return true;
}

using AndroidNoteProperties = std::vector<std::pair<StringRef, std::string>>;
static AndroidNoteProperties getAndroidNoteProperties(uint32_t NoteType,
                                                      ArrayRef<uint8_t> Desc) {
  AndroidNoteProperties Props;
  switch (NoteType) {
  case ELF::NT_ANDROID_TYPE_MEMTAG:
    if (Desc.empty()) {
      Props.emplace_back("Invalid .note.android.memtag", "");
      return Props;
    }

    switch (Desc[0] & NT_MEMTAG_LEVEL_MASK) {
    case NT_MEMTAG_LEVEL_NONE:
      Props.emplace_back("Tagging Mode", "NONE");
      break;
    case NT_MEMTAG_LEVEL_ASYNC:
      Props.emplace_back("Tagging Mode", "ASYNC");
      break;
    case NT_MEMTAG_LEVEL_SYNC:
      Props.emplace_back("Tagging Mode", "SYNC");
      break;
    default:
      Props.emplace_back(
          "Tagging Mode",
          ("Unknown (" + Twine::utohexstr(Desc[0] & NT_MEMTAG_LEVEL_MASK) + ")")
              .str());
      break;
    }
    Props.emplace_back("Heap",
                       (Desc[0] & NT_MEMTAG_HEAP) ? "Enabled" : "Disabled");
    Props.emplace_back("Stack",
                       (Desc[0] & NT_MEMTAG_STACK) ? "Enabled" : "Disabled");
    break;
  default:
    return Props;
  }
  return Props;
}

static bool printAndroidNote(raw_ostream &OS, uint32_t NoteType,
                             ArrayRef<uint8_t> Desc) {
  // Return true if we were able to pretty-print the note, false otherwise.
  AndroidNoteProperties Props = getAndroidNoteProperties(NoteType, Desc);
  if (Props.empty())
    return false;
  for (const auto &KV : Props)
    OS << "    " << KV.first << ": " << KV.second << '\n';
  return true;
}

template <class ELFT>
void GNUELFDumper<ELFT>::printMemtag(
    const ArrayRef<std::pair<std::string, std::string>> DynamicEntries,
    const ArrayRef<uint8_t> AndroidNoteDesc,
    const ArrayRef<std::pair<uint64_t, uint64_t>> Descriptors) {
  OS << "Memtag Dynamic Entries:\n";
  if (DynamicEntries.empty())
    OS << "    < none found >\n";
  for (const auto &DynamicEntryKV : DynamicEntries)
    OS << "    " << DynamicEntryKV.first << ": " << DynamicEntryKV.second
       << "\n";

  if (!AndroidNoteDesc.empty()) {
    OS << "Memtag Android Note:\n";
    printAndroidNote(OS, ELF::NT_ANDROID_TYPE_MEMTAG, AndroidNoteDesc);
  }

  if (Descriptors.empty())
    return;

  OS << "Memtag Global Descriptors:\n";
  for (const auto &[Addr, BytesToTag] : Descriptors) {
    OS << "    0x" << utohexstr(Addr, /*LowerCase=*/true) << ": 0x"
       << utohexstr(BytesToTag, /*LowerCase=*/true) << "\n";
  }
}

template <typename ELFT>
static bool printLLVMOMPOFFLOADNote(raw_ostream &OS, uint32_t NoteType,
                                    ArrayRef<uint8_t> Desc) {
  switch (NoteType) {
  default:
    return false;
  case ELF::NT_LLVM_OPENMP_OFFLOAD_VERSION:
    OS << "    Version: " << getDescAsStringRef(Desc);
    break;
  case ELF::NT_LLVM_OPENMP_OFFLOAD_PRODUCER:
    OS << "    Producer: " << getDescAsStringRef(Desc);
    break;
  case ELF::NT_LLVM_OPENMP_OFFLOAD_PRODUCER_VERSION:
    OS << "    Producer version: " << getDescAsStringRef(Desc);
    break;
  }
  OS << '\n';
  return true;
}

const EnumEntry<unsigned> FreeBSDFeatureCtlFlags[] = {
    {"ASLR_DISABLE", NT_FREEBSD_FCTL_ASLR_DISABLE},
    {"PROTMAX_DISABLE", NT_FREEBSD_FCTL_PROTMAX_DISABLE},
    {"STKGAP_DISABLE", NT_FREEBSD_FCTL_STKGAP_DISABLE},
    {"WXNEEDED", NT_FREEBSD_FCTL_WXNEEDED},
    {"LA48", NT_FREEBSD_FCTL_LA48},
    {"ASG_DISABLE", NT_FREEBSD_FCTL_ASG_DISABLE},
};

struct FreeBSDNote {
  std::string Type;
  std::string Value;
};

template <typename ELFT>
static std::optional<FreeBSDNote>
getFreeBSDNote(uint32_t NoteType, ArrayRef<uint8_t> Desc, bool IsCore) {
  if (IsCore)
    return std::nullopt; // No pretty-printing yet.
  switch (NoteType) {
  case ELF::NT_FREEBSD_ABI_TAG:
    if (Desc.size() != 4)
      return std::nullopt;
    return FreeBSDNote{"ABI tag",
                       utostr(endian::read32<ELFT::Endianness>(Desc.data()))};
  case ELF::NT_FREEBSD_ARCH_TAG:
    return FreeBSDNote{"Arch tag", toStringRef(Desc).str()};
  case ELF::NT_FREEBSD_FEATURE_CTL: {
    if (Desc.size() != 4)
      return std::nullopt;
    unsigned Value = endian::read32<ELFT::Endianness>(Desc.data());
    std::string FlagsStr;
    raw_string_ostream OS(FlagsStr);
    printFlags(Value, ArrayRef(FreeBSDFeatureCtlFlags), OS);
    if (OS.str().empty())
      OS << "0x" << utohexstr(Value);
    else
      OS << "(0x" << utohexstr(Value) << ")";
    return FreeBSDNote{"Feature flags", OS.str()};
  }
  default:
    return std::nullopt;
  }
}

struct AMDNote {
  std::string Type;
  std::string Value;
};

template <typename ELFT>
static AMDNote getAMDNote(uint32_t NoteType, ArrayRef<uint8_t> Desc) {
  switch (NoteType) {
  default:
    return {"", ""};
  case ELF::NT_AMD_HSA_CODE_OBJECT_VERSION: {
    struct CodeObjectVersion {
      support::aligned_ulittle32_t MajorVersion;
      support::aligned_ulittle32_t MinorVersion;
    };
    if (Desc.size() != sizeof(CodeObjectVersion))
      return {"AMD HSA Code Object Version",
              "Invalid AMD HSA Code Object Version"};
    std::string VersionString;
    raw_string_ostream StrOS(VersionString);
    auto Version = reinterpret_cast<const CodeObjectVersion *>(Desc.data());
    StrOS << "[Major: " << Version->MajorVersion
          << ", Minor: " << Version->MinorVersion << "]";
    return {"AMD HSA Code Object Version", VersionString};
  }
  case ELF::NT_AMD_HSA_HSAIL: {
    struct HSAILProperties {
      support::aligned_ulittle32_t HSAILMajorVersion;
      support::aligned_ulittle32_t HSAILMinorVersion;
      uint8_t Profile;
      uint8_t MachineModel;
      uint8_t DefaultFloatRound;
    };
    if (Desc.size() != sizeof(HSAILProperties))
      return {"AMD HSA HSAIL Properties", "Invalid AMD HSA HSAIL Properties"};
    auto Properties = reinterpret_cast<const HSAILProperties *>(Desc.data());
    std::string HSAILPropetiesString;
    raw_string_ostream StrOS(HSAILPropetiesString);
    StrOS << "[HSAIL Major: " << Properties->HSAILMajorVersion
          << ", HSAIL Minor: " << Properties->HSAILMinorVersion
          << ", Profile: " << uint32_t(Properties->Profile)
          << ", Machine Model: " << uint32_t(Properties->MachineModel)
          << ", Default Float Round: "
          << uint32_t(Properties->DefaultFloatRound) << "]";
    return {"AMD HSA HSAIL Properties", HSAILPropetiesString};
  }
  case ELF::NT_AMD_HSA_ISA_VERSION: {
    struct IsaVersion {
      support::aligned_ulittle16_t VendorNameSize;
      support::aligned_ulittle16_t ArchitectureNameSize;
      support::aligned_ulittle32_t Major;
      support::aligned_ulittle32_t Minor;
      support::aligned_ulittle32_t Stepping;
    };
    if (Desc.size() < sizeof(IsaVersion))
      return {"AMD HSA ISA Version", "Invalid AMD HSA ISA Version"};
    auto Isa = reinterpret_cast<const IsaVersion *>(Desc.data());
    if (Desc.size() < sizeof(IsaVersion) +
                          Isa->VendorNameSize + Isa->ArchitectureNameSize ||
        Isa->VendorNameSize == 0 || Isa->ArchitectureNameSize == 0)
      return {"AMD HSA ISA Version", "Invalid AMD HSA ISA Version"};
    std::string IsaString;
    raw_string_ostream StrOS(IsaString);
    StrOS << "[Vendor: "
          << StringRef((const char*)Desc.data() + sizeof(IsaVersion), Isa->VendorNameSize - 1)
          << ", Architecture: "
          << StringRef((const char*)Desc.data() + sizeof(IsaVersion) + Isa->VendorNameSize,
                       Isa->ArchitectureNameSize - 1)
          << ", Major: " << Isa->Major << ", Minor: " << Isa->Minor
          << ", Stepping: " << Isa->Stepping << "]";
    return {"AMD HSA ISA Version", IsaString};
  }
  case ELF::NT_AMD_HSA_METADATA: {
    if (Desc.size() == 0)
      return {"AMD HSA Metadata", ""};
    return {
        "AMD HSA Metadata",
        std::string(reinterpret_cast<const char *>(Desc.data()), Desc.size() - 1)};
  }
  case ELF::NT_AMD_HSA_ISA_NAME: {
    if (Desc.size() == 0)
      return {"AMD HSA ISA Name", ""};
    return {
        "AMD HSA ISA Name",
        std::string(reinterpret_cast<const char *>(Desc.data()), Desc.size())};
  }
  case ELF::NT_AMD_PAL_METADATA: {
    struct PALMetadata {
      support::aligned_ulittle32_t Key;
      support::aligned_ulittle32_t Value;
    };
    if (Desc.size() % sizeof(PALMetadata) != 0)
      return {"AMD PAL Metadata", "Invalid AMD PAL Metadata"};
    auto Isa = reinterpret_cast<const PALMetadata *>(Desc.data());
    std::string MetadataString;
    raw_string_ostream StrOS(MetadataString);
    for (size_t I = 0, E = Desc.size() / sizeof(PALMetadata); I < E; ++I) {
      StrOS << "[" << Isa[I].Key << ": " << Isa[I].Value << "]";
    }
    return {"AMD PAL Metadata", MetadataString};
  }
  }
}

struct AMDGPUNote {
  std::string Type;
  std::string Value;
};

template <typename ELFT>
static AMDGPUNote getAMDGPUNote(uint32_t NoteType, ArrayRef<uint8_t> Desc) {
  switch (NoteType) {
  default:
    return {"", ""};
  case ELF::NT_AMDGPU_METADATA: {
    StringRef MsgPackString =
        StringRef(reinterpret_cast<const char *>(Desc.data()), Desc.size());
    msgpack::Document MsgPackDoc;
    if (!MsgPackDoc.readFromBlob(MsgPackString, /*Multi=*/false))
      return {"", ""};

    std::string MetadataString;

    // FIXME: Metadata Verifier only works with AMDHSA.
    //  This is an ugly workaround to avoid the verifier for other MD
    //  formats (e.g. amdpal)
    if (MsgPackString.contains("amdhsa.")) {
      AMDGPU::HSAMD::V3::MetadataVerifier Verifier(true);
      if (!Verifier.verify(MsgPackDoc.getRoot()))
        MetadataString = "Invalid AMDGPU Metadata\n";
    }

    raw_string_ostream StrOS(MetadataString);
    if (MsgPackDoc.getRoot().isScalar()) {
      // TODO: passing a scalar root to toYAML() asserts:
      // (PolymorphicTraits<T>::getKind(Val) != NodeKind::Scalar &&
      //    "plain scalar documents are not supported")
      // To avoid this crash we print the raw data instead.
      return {"", ""};
    }
    MsgPackDoc.toYAML(StrOS);
    return {"AMDGPU Metadata", StrOS.str()};
  }
  }
}

struct CoreFileMapping {
  uint64_t Start, End, Offset;
  StringRef Filename;
};

struct CoreNote {
  uint64_t PageSize;
  std::vector<CoreFileMapping> Mappings;
};

static Expected<CoreNote> readCoreNote(DataExtractor Desc) {
  // Expected format of the NT_FILE note description:
  // 1. # of file mappings (call it N)
  // 2. Page size
  // 3. N (start, end, offset) triples
  // 4. N packed filenames (null delimited)
  // Each field is an Elf_Addr, except for filenames which are char* strings.

  CoreNote Ret;
  const int Bytes = Desc.getAddressSize();

  if (!Desc.isValidOffsetForAddress(2))
    return createError("the note of size 0x" + Twine::utohexstr(Desc.size()) +
                       " is too short, expected at least 0x" +
                       Twine::utohexstr(Bytes * 2));
  if (Desc.getData().back() != 0)
    return createError("the note is not NUL terminated");

  uint64_t DescOffset = 0;
  uint64_t FileCount = Desc.getAddress(&DescOffset);
  Ret.PageSize = Desc.getAddress(&DescOffset);

  if (!Desc.isValidOffsetForAddress(3 * FileCount * Bytes))
    return createError("unable to read file mappings (found " +
                       Twine(FileCount) + "): the note of size 0x" +
                       Twine::utohexstr(Desc.size()) + " is too short");

  uint64_t FilenamesOffset = 0;
  DataExtractor Filenames(
      Desc.getData().drop_front(DescOffset + 3 * FileCount * Bytes),
      Desc.isLittleEndian(), Desc.getAddressSize());

  Ret.Mappings.resize(FileCount);
  size_t I = 0;
  for (CoreFileMapping &Mapping : Ret.Mappings) {
    ++I;
    if (!Filenames.isValidOffsetForDataOfSize(FilenamesOffset, 1))
      return createError(
          "unable to read the file name for the mapping with index " +
          Twine(I) + ": the note of size 0x" + Twine::utohexstr(Desc.size()) +
          " is truncated");
    Mapping.Start = Desc.getAddress(&DescOffset);
    Mapping.End = Desc.getAddress(&DescOffset);
    Mapping.Offset = Desc.getAddress(&DescOffset);
    Mapping.Filename = Filenames.getCStrRef(&FilenamesOffset);
  }

  return Ret;
}

template <typename ELFT>
static void printCoreNote(raw_ostream &OS, const CoreNote &Note) {
  // Length of "0x<address>" string.
  const int FieldWidth = ELFT::Is64Bits ? 18 : 10;

  OS << "    Page size: " << format_decimal(Note.PageSize, 0) << '\n';
  OS << "    " << right_justify("Start", FieldWidth) << "  "
     << right_justify("End", FieldWidth) << "  "
     << right_justify("Page Offset", FieldWidth) << '\n';
  for (const CoreFileMapping &Mapping : Note.Mappings) {
    OS << "    " << format_hex(Mapping.Start, FieldWidth) << "  "
       << format_hex(Mapping.End, FieldWidth) << "  "
       << format_hex(Mapping.Offset, FieldWidth) << "\n        "
       << Mapping.Filename << '\n';
  }
}

const NoteType GenericNoteTypes[] = {
    {ELF::NT_VERSION, "NT_VERSION (version)"},
    {ELF::NT_ARCH, "NT_ARCH (architecture)"},
    {ELF::NT_GNU_BUILD_ATTRIBUTE_OPEN, "OPEN"},
    {ELF::NT_GNU_BUILD_ATTRIBUTE_FUNC, "func"},
};

const NoteType GNUNoteTypes[] = {
    {ELF::NT_GNU_ABI_TAG, "NT_GNU_ABI_TAG (ABI version tag)"},
    {ELF::NT_GNU_HWCAP, "NT_GNU_HWCAP (DSO-supplied software HWCAP info)"},
    {ELF::NT_GNU_BUILD_ID, "NT_GNU_BUILD_ID (unique build ID bitstring)"},
    {ELF::NT_GNU_GOLD_VERSION, "NT_GNU_GOLD_VERSION (gold version)"},
    {ELF::NT_GNU_PROPERTY_TYPE_0, "NT_GNU_PROPERTY_TYPE_0 (property note)"},
};

const NoteType FreeBSDCoreNoteTypes[] = {
    {ELF::NT_FREEBSD_THRMISC, "NT_THRMISC (thrmisc structure)"},
    {ELF::NT_FREEBSD_PROCSTAT_PROC, "NT_PROCSTAT_PROC (proc data)"},
    {ELF::NT_FREEBSD_PROCSTAT_FILES, "NT_PROCSTAT_FILES (files data)"},
    {ELF::NT_FREEBSD_PROCSTAT_VMMAP, "NT_PROCSTAT_VMMAP (vmmap data)"},
    {ELF::NT_FREEBSD_PROCSTAT_GROUPS, "NT_PROCSTAT_GROUPS (groups data)"},
    {ELF::NT_FREEBSD_PROCSTAT_UMASK, "NT_PROCSTAT_UMASK (umask data)"},
    {ELF::NT_FREEBSD_PROCSTAT_RLIMIT, "NT_PROCSTAT_RLIMIT (rlimit data)"},
    {ELF::NT_FREEBSD_PROCSTAT_OSREL, "NT_PROCSTAT_OSREL (osreldate data)"},
    {ELF::NT_FREEBSD_PROCSTAT_PSSTRINGS,
     "NT_PROCSTAT_PSSTRINGS (ps_strings data)"},
    {ELF::NT_FREEBSD_PROCSTAT_AUXV, "NT_PROCSTAT_AUXV (auxv data)"},
};

const NoteType FreeBSDNoteTypes[] = {
    {ELF::NT_FREEBSD_ABI_TAG, "NT_FREEBSD_ABI_TAG (ABI version tag)"},
    {ELF::NT_FREEBSD_NOINIT_TAG, "NT_FREEBSD_NOINIT_TAG (no .init tag)"},
    {ELF::NT_FREEBSD_ARCH_TAG, "NT_FREEBSD_ARCH_TAG (architecture tag)"},
    {ELF::NT_FREEBSD_FEATURE_CTL,
     "NT_FREEBSD_FEATURE_CTL (FreeBSD feature control)"},
};

const NoteType NetBSDCoreNoteTypes[] = {
    {ELF::NT_NETBSDCORE_PROCINFO,
     "NT_NETBSDCORE_PROCINFO (procinfo structure)"},
    {ELF::NT_NETBSDCORE_AUXV, "NT_NETBSDCORE_AUXV (ELF auxiliary vector data)"},
    {ELF::NT_NETBSDCORE_LWPSTATUS, "PT_LWPSTATUS (ptrace_lwpstatus structure)"},
};

const NoteType OpenBSDCoreNoteTypes[] = {
    {ELF::NT_OPENBSD_PROCINFO, "NT_OPENBSD_PROCINFO (procinfo structure)"},
    {ELF::NT_OPENBSD_AUXV, "NT_OPENBSD_AUXV (ELF auxiliary vector data)"},
    {ELF::NT_OPENBSD_REGS, "NT_OPENBSD_REGS (regular registers)"},
    {ELF::NT_OPENBSD_FPREGS, "NT_OPENBSD_FPREGS (floating point registers)"},
    {ELF::NT_OPENBSD_WCOOKIE, "NT_OPENBSD_WCOOKIE (window cookie)"},
};

const NoteType AMDNoteTypes[] = {
    {ELF::NT_AMD_HSA_CODE_OBJECT_VERSION,
     "NT_AMD_HSA_CODE_OBJECT_VERSION (AMD HSA Code Object Version)"},
    {ELF::NT_AMD_HSA_HSAIL, "NT_AMD_HSA_HSAIL (AMD HSA HSAIL Properties)"},
    {ELF::NT_AMD_HSA_ISA_VERSION, "NT_AMD_HSA_ISA_VERSION (AMD HSA ISA Version)"},
    {ELF::NT_AMD_HSA_METADATA, "NT_AMD_HSA_METADATA (AMD HSA Metadata)"},
    {ELF::NT_AMD_HSA_ISA_NAME, "NT_AMD_HSA_ISA_NAME (AMD HSA ISA Name)"},
    {ELF::NT_AMD_PAL_METADATA, "NT_AMD_PAL_METADATA (AMD PAL Metadata)"},
};

const NoteType AMDGPUNoteTypes[] = {
    {ELF::NT_AMDGPU_METADATA, "NT_AMDGPU_METADATA (AMDGPU Metadata)"},
};

const NoteType LLVMOMPOFFLOADNoteTypes[] = {
    {ELF::NT_LLVM_OPENMP_OFFLOAD_VERSION,
     "NT_LLVM_OPENMP_OFFLOAD_VERSION (image format version)"},
    {ELF::NT_LLVM_OPENMP_OFFLOAD_PRODUCER,
     "NT_LLVM_OPENMP_OFFLOAD_PRODUCER (producing toolchain)"},
    {ELF::NT_LLVM_OPENMP_OFFLOAD_PRODUCER_VERSION,
     "NT_LLVM_OPENMP_OFFLOAD_PRODUCER_VERSION (producing toolchain version)"},
};

const NoteType AndroidNoteTypes[] = {
    {ELF::NT_ANDROID_TYPE_IDENT, "NT_ANDROID_TYPE_IDENT"},
    {ELF::NT_ANDROID_TYPE_KUSER, "NT_ANDROID_TYPE_KUSER"},
    {ELF::NT_ANDROID_TYPE_MEMTAG,
     "NT_ANDROID_TYPE_MEMTAG (Android memory tagging information)"},
};

const NoteType CoreNoteTypes[] = {
    {ELF::NT_PRSTATUS, "NT_PRSTATUS (prstatus structure)"},
    {ELF::NT_FPREGSET, "NT_FPREGSET (floating point registers)"},
    {ELF::NT_PRPSINFO, "NT_PRPSINFO (prpsinfo structure)"},
    {ELF::NT_TASKSTRUCT, "NT_TASKSTRUCT (task structure)"},
    {ELF::NT_AUXV, "NT_AUXV (auxiliary vector)"},
    {ELF::NT_PSTATUS, "NT_PSTATUS (pstatus structure)"},
    {ELF::NT_FPREGS, "NT_FPREGS (floating point registers)"},
    {ELF::NT_PSINFO, "NT_PSINFO (psinfo structure)"},
    {ELF::NT_LWPSTATUS, "NT_LWPSTATUS (lwpstatus_t structure)"},
    {ELF::NT_LWPSINFO, "NT_LWPSINFO (lwpsinfo_t structure)"},
    {ELF::NT_WIN32PSTATUS, "NT_WIN32PSTATUS (win32_pstatus structure)"},

    {ELF::NT_PPC_VMX, "NT_PPC_VMX (ppc Altivec registers)"},
    {ELF::NT_PPC_VSX, "NT_PPC_VSX (ppc VSX registers)"},
    {ELF::NT_PPC_TAR, "NT_PPC_TAR (ppc TAR register)"},
    {ELF::NT_PPC_PPR, "NT_PPC_PPR (ppc PPR register)"},
    {ELF::NT_PPC_DSCR, "NT_PPC_DSCR (ppc DSCR register)"},
    {ELF::NT_PPC_EBB, "NT_PPC_EBB (ppc EBB registers)"},
    {ELF::NT_PPC_PMU, "NT_PPC_PMU (ppc PMU registers)"},
    {ELF::NT_PPC_TM_CGPR, "NT_PPC_TM_CGPR (ppc checkpointed GPR registers)"},
    {ELF::NT_PPC_TM_CFPR,
     "NT_PPC_TM_CFPR (ppc checkpointed floating point registers)"},
    {ELF::NT_PPC_TM_CVMX,
     "NT_PPC_TM_CVMX (ppc checkpointed Altivec registers)"},
    {ELF::NT_PPC_TM_CVSX, "NT_PPC_TM_CVSX (ppc checkpointed VSX registers)"},
    {ELF::NT_PPC_TM_SPR, "NT_PPC_TM_SPR (ppc TM special purpose registers)"},
    {ELF::NT_PPC_TM_CTAR, "NT_PPC_TM_CTAR (ppc checkpointed TAR register)"},
    {ELF::NT_PPC_TM_CPPR, "NT_PPC_TM_CPPR (ppc checkpointed PPR register)"},
    {ELF::NT_PPC_TM_CDSCR, "NT_PPC_TM_CDSCR (ppc checkpointed DSCR register)"},

    {ELF::NT_386_TLS, "NT_386_TLS (x86 TLS information)"},
    {ELF::NT_386_IOPERM, "NT_386_IOPERM (x86 I/O permissions)"},
    {ELF::NT_X86_XSTATE, "NT_X86_XSTATE (x86 XSAVE extended state)"},

    {ELF::NT_S390_HIGH_GPRS, "NT_S390_HIGH_GPRS (s390 upper register halves)"},
    {ELF::NT_S390_TIMER, "NT_S390_TIMER (s390 timer register)"},
    {ELF::NT_S390_TODCMP, "NT_S390_TODCMP (s390 TOD comparator register)"},
    {ELF::NT_S390_TODPREG, "NT_S390_TODPREG (s390 TOD programmable register)"},
    {ELF::NT_S390_CTRS, "NT_S390_CTRS (s390 control registers)"},
    {ELF::NT_S390_PREFIX, "NT_S390_PREFIX (s390 prefix register)"},
    {ELF::NT_S390_LAST_BREAK,
     "NT_S390_LAST_BREAK (s390 last breaking event address)"},
    {ELF::NT_S390_SYSTEM_CALL,
     "NT_S390_SYSTEM_CALL (s390 system call restart data)"},
    {ELF::NT_S390_TDB, "NT_S390_TDB (s390 transaction diagnostic block)"},
    {ELF::NT_S390_VXRS_LOW,
     "NT_S390_VXRS_LOW (s390 vector registers 0-15 upper half)"},
    {ELF::NT_S390_VXRS_HIGH, "NT_S390_VXRS_HIGH (s390 vector registers 16-31)"},
    {ELF::NT_S390_GS_CB, "NT_S390_GS_CB (s390 guarded-storage registers)"},
    {ELF::NT_S390_GS_BC,
     "NT_S390_GS_BC (s390 guarded-storage broadcast control)"},

    {ELF::NT_ARM_VFP, "NT_ARM_VFP (arm VFP registers)"},
    {ELF::NT_ARM_TLS, "NT_ARM_TLS (AArch TLS registers)"},
    {ELF::NT_ARM_HW_BREAK,
     "NT_ARM_HW_BREAK (AArch hardware breakpoint registers)"},
    {ELF::NT_ARM_HW_WATCH,
     "NT_ARM_HW_WATCH (AArch hardware watchpoint registers)"},
    {ELF::NT_ARM_SVE, "NT_ARM_SVE (AArch64 SVE registers)"},
    {ELF::NT_ARM_PAC_MASK,
     "NT_ARM_PAC_MASK (AArch64 Pointer Authentication code masks)"},
    {ELF::NT_ARM_TAGGED_ADDR_CTRL,
     "NT_ARM_TAGGED_ADDR_CTRL (AArch64 Tagged Address Control)"},
    {ELF::NT_ARM_SSVE, "NT_ARM_SSVE (AArch64 Streaming SVE registers)"},
    {ELF::NT_ARM_ZA, "NT_ARM_ZA (AArch64 SME ZA registers)"},
    {ELF::NT_ARM_ZT, "NT_ARM_ZT (AArch64 SME ZT registers)"},

    {ELF::NT_FILE, "NT_FILE (mapped files)"},
    {ELF::NT_PRXFPREG, "NT_PRXFPREG (user_xfpregs structure)"},
    {ELF::NT_SIGINFO, "NT_SIGINFO (siginfo_t data)"},
};

template <class ELFT>
StringRef getNoteTypeName(const typename ELFT::Note &Note, unsigned ELFType) {
  uint32_t Type = Note.getType();
  auto FindNote = [&](ArrayRef<NoteType> V) -> StringRef {
    for (const NoteType &N : V)
      if (N.ID == Type)
        return N.Name;
    return "";
  };

  StringRef Name = Note.getName();
  if (Name == "GNU")
    return FindNote(GNUNoteTypes);
  if (Name == "FreeBSD") {
    if (ELFType == ELF::ET_CORE) {
      // FreeBSD also places the generic core notes in the FreeBSD namespace.
      StringRef Result = FindNote(FreeBSDCoreNoteTypes);
      if (!Result.empty())
        return Result;
      return FindNote(CoreNoteTypes);
    } else {
      return FindNote(FreeBSDNoteTypes);
    }
  }
  if (ELFType == ELF::ET_CORE && Name.starts_with("NetBSD-CORE")) {
    StringRef Result = FindNote(NetBSDCoreNoteTypes);
    if (!Result.empty())
      return Result;
    return FindNote(CoreNoteTypes);
  }
  if (ELFType == ELF::ET_CORE && Name.starts_with("OpenBSD")) {
    // OpenBSD also places the generic core notes in the OpenBSD namespace.
    StringRef Result = FindNote(OpenBSDCoreNoteTypes);
    if (!Result.empty())
      return Result;
    return FindNote(CoreNoteTypes);
  }
  if (Name == "AMD")
    return FindNote(AMDNoteTypes);
  if (Name == "AMDGPU")
    return FindNote(AMDGPUNoteTypes);
  if (Name == "LLVMOMPOFFLOAD")
    return FindNote(LLVMOMPOFFLOADNoteTypes);
  if (Name == "Android")
    return FindNote(AndroidNoteTypes);

  if (ELFType == ELF::ET_CORE)
    return FindNote(CoreNoteTypes);
  return FindNote(GenericNoteTypes);
}

template <class ELFT>
static void processNotesHelper(
    const ELFDumper<ELFT> &Dumper,
    llvm::function_ref<void(std::optional<StringRef>, typename ELFT::Off,
                            typename ELFT::Addr, size_t)>
        StartNotesFn,
    llvm::function_ref<Error(const typename ELFT::Note &, bool)> ProcessNoteFn,
    llvm::function_ref<void()> FinishNotesFn) {
  const ELFFile<ELFT> &Obj = Dumper.getElfObject().getELFFile();
  bool IsCoreFile = Obj.getHeader().e_type == ELF::ET_CORE;

  ArrayRef<typename ELFT::Shdr> Sections = cantFail(Obj.sections());
  if (!IsCoreFile && !Sections.empty()) {
    for (const typename ELFT::Shdr &S : Sections) {
      if (S.sh_type != SHT_NOTE)
        continue;
      StartNotesFn(expectedToStdOptional(Obj.getSectionName(S)), S.sh_offset,
                   S.sh_size, S.sh_addralign);
      Error Err = Error::success();
      size_t I = 0;
      for (const typename ELFT::Note Note : Obj.notes(S, Err)) {
        if (Error E = ProcessNoteFn(Note, IsCoreFile))
          Dumper.reportUniqueWarning(
              "unable to read note with index " + Twine(I) + " from the " +
              describe(Obj, S) + ": " + toString(std::move(E)));
        ++I;
      }
      if (Err)
        Dumper.reportUniqueWarning("unable to read notes from the " +
                                   describe(Obj, S) + ": " +
                                   toString(std::move(Err)));
      FinishNotesFn();
    }
    return;
  }

  Expected<ArrayRef<typename ELFT::Phdr>> PhdrsOrErr = Obj.program_headers();
  if (!PhdrsOrErr) {
    Dumper.reportUniqueWarning(
        "unable to read program headers to locate the PT_NOTE segment: " +
        toString(PhdrsOrErr.takeError()));
    return;
  }

  for (size_t I = 0, E = (*PhdrsOrErr).size(); I != E; ++I) {
    const typename ELFT::Phdr &P = (*PhdrsOrErr)[I];
    if (P.p_type != PT_NOTE)
      continue;
    StartNotesFn(/*SecName=*/std::nullopt, P.p_offset, P.p_filesz, P.p_align);
    Error Err = Error::success();
    size_t Index = 0;
    for (const typename ELFT::Note Note : Obj.notes(P, Err)) {
      if (Error E = ProcessNoteFn(Note, IsCoreFile))
        Dumper.reportUniqueWarning("unable to read note with index " +
                                   Twine(Index) +
                                   " from the PT_NOTE segment with index " +
                                   Twine(I) + ": " + toString(std::move(E)));
      ++Index;
    }
    if (Err)
      Dumper.reportUniqueWarning(
          "unable to read notes from the PT_NOTE segment with index " +
          Twine(I) + ": " + toString(std::move(Err)));
    FinishNotesFn();
  }
}

template <class ELFT> void GNUELFDumper<ELFT>::printNotes() {
  size_t Align = 0;
  bool IsFirstHeader = true;
  auto PrintHeader = [&](std::optional<StringRef> SecName,
                         const typename ELFT::Off Offset,
                         const typename ELFT::Addr Size, size_t Al) {
    Align = std::max<size_t>(Al, 4);
    // Print a newline between notes sections to match GNU readelf.
    if (!IsFirstHeader) {
      OS << '\n';
    } else {
      IsFirstHeader = false;
    }

    OS << "Displaying notes found ";

    if (SecName)
      OS << "in: " << *SecName << "\n";
    else
      OS << "at file offset " << format_hex(Offset, 10) << " with length "
         << format_hex(Size, 10) << ":\n";

    OS << "  Owner                Data size \tDescription\n";
  };

  auto ProcessNote = [&](const Elf_Note &Note, bool IsCore) -> Error {
    StringRef Name = Note.getName();
    ArrayRef<uint8_t> Descriptor = Note.getDesc(Align);
    Elf_Word Type = Note.getType();

    // Print the note owner/type.
    OS << "  " << left_justify(Name, 20) << ' '
       << format_hex(Descriptor.size(), 10) << '\t';

    StringRef NoteType =
        getNoteTypeName<ELFT>(Note, this->Obj.getHeader().e_type);
    if (!NoteType.empty())
      OS << NoteType << '\n';
    else
      OS << "Unknown note type: (" << format_hex(Type, 10) << ")\n";

    // Print the description, or fallback to printing raw bytes for unknown
    // owners/if we fail to pretty-print the contents.
    if (Name == "GNU") {
      if (printGNUNote<ELFT>(OS, Type, Descriptor))
        return Error::success();
    } else if (Name == "FreeBSD") {
      if (std::optional<FreeBSDNote> N =
              getFreeBSDNote<ELFT>(Type, Descriptor, IsCore)) {
        OS << "    " << N->Type << ": " << N->Value << '\n';
        return Error::success();
      }
    } else if (Name == "AMD") {
      const AMDNote N = getAMDNote<ELFT>(Type, Descriptor);
      if (!N.Type.empty()) {
        OS << "    " << N.Type << ":\n        " << N.Value << '\n';
        return Error::success();
      }
    } else if (Name == "AMDGPU") {
      const AMDGPUNote N = getAMDGPUNote<ELFT>(Type, Descriptor);
      if (!N.Type.empty()) {
        OS << "    " << N.Type << ":\n        " << N.Value << '\n';
        return Error::success();
      }
    } else if (Name == "LLVMOMPOFFLOAD") {
      if (printLLVMOMPOFFLOADNote<ELFT>(OS, Type, Descriptor))
        return Error::success();
    } else if (Name == "CORE") {
      if (Type == ELF::NT_FILE) {
        DataExtractor DescExtractor(
            Descriptor, ELFT::Endianness == llvm::endianness::little,
            sizeof(Elf_Addr));
        if (Expected<CoreNote> NoteOrErr = readCoreNote(DescExtractor)) {
          printCoreNote<ELFT>(OS, *NoteOrErr);
          return Error::success();
        } else {
          return NoteOrErr.takeError();
        }
      }
    } else if (Name == "Android") {
      if (printAndroidNote(OS, Type, Descriptor))
        return Error::success();
    }
    if (!Descriptor.empty()) {
      OS << "   description data:";
      for (uint8_t B : Descriptor)
        OS << " " << format("%02x", B);
      OS << '\n';
    }
    return Error::success();
  };

  processNotesHelper(*this, /*StartNotesFn=*/PrintHeader,
                     /*ProcessNoteFn=*/ProcessNote, /*FinishNotesFn=*/[]() {});
}

template <class ELFT>
ArrayRef<uint8_t>
ELFDumper<ELFT>::getMemtagGlobalsSectionContents(uint64_t ExpectedAddr) {
  for (const typename ELFT::Shdr &Sec : cantFail(Obj.sections())) {
    if (Sec.sh_type != SHT_AARCH64_MEMTAG_GLOBALS_DYNAMIC)
      continue;
    if (Sec.sh_addr != ExpectedAddr) {
      reportUniqueWarning(
          "SHT_AARCH64_MEMTAG_GLOBALS_DYNAMIC section was unexpectedly at 0x" +
          Twine::utohexstr(Sec.sh_addr) +
          ", when DT_AARCH64_MEMTAG_GLOBALS says it should be at 0x" +
          Twine::utohexstr(ExpectedAddr));
      return ArrayRef<uint8_t>();
    }
    Expected<ArrayRef<uint8_t>> Contents = Obj.getSectionContents(Sec);
    if (auto E = Contents.takeError()) {
      reportUniqueWarning(
          "couldn't get SHT_AARCH64_MEMTAG_GLOBALS_DYNAMIC section contents: " +
          toString(std::move(E)));
      return ArrayRef<uint8_t>();
    }
    return Contents.get();
  }
  return ArrayRef<uint8_t>();
}

// Reserve the lower three bits of the first byte of the step distance when
// encoding the memtag descriptors. Found to be the best overall size tradeoff
// when compiling Android T with full MTE globals enabled.
constexpr uint64_t MemtagStepVarintReservedBits = 3;
constexpr uint64_t MemtagGranuleSize = 16;

template <typename ELFT> void ELFDumper<ELFT>::printMemtag() {
  if (Obj.getHeader().e_machine != EM_AARCH64) return;
  std::vector<std::pair<std::string, std::string>> DynamicEntries;
  uint64_t MemtagGlobalsSz = 0;
  uint64_t MemtagGlobals = 0;
  for (const typename ELFT::Dyn &Entry : dynamic_table()) {
    uintX_t Tag = Entry.getTag();
    switch (Tag) {
    case DT_AARCH64_MEMTAG_GLOBALSSZ:
      MemtagGlobalsSz = Entry.getVal();
      DynamicEntries.emplace_back(Obj.getDynamicTagAsString(Tag),
                                  getDynamicEntry(Tag, Entry.getVal()));
      break;
    case DT_AARCH64_MEMTAG_GLOBALS:
      MemtagGlobals = Entry.getVal();
      DynamicEntries.emplace_back(Obj.getDynamicTagAsString(Tag),
                                  getDynamicEntry(Tag, Entry.getVal()));
      break;
    case DT_AARCH64_MEMTAG_MODE:
    case DT_AARCH64_MEMTAG_HEAP:
    case DT_AARCH64_MEMTAG_STACK:
      DynamicEntries.emplace_back(Obj.getDynamicTagAsString(Tag),
                                  getDynamicEntry(Tag, Entry.getVal()));
      break;
    }
  }

  ArrayRef<uint8_t> AndroidNoteDesc;
  auto FindAndroidNote = [&](const Elf_Note &Note, bool IsCore) -> Error {
    if (Note.getName() == "Android" &&
        Note.getType() == ELF::NT_ANDROID_TYPE_MEMTAG)
      AndroidNoteDesc = Note.getDesc(4);
    return Error::success();
  };

  processNotesHelper(
      *this,
      /*StartNotesFn=*/
      [](std::optional<StringRef>, const typename ELFT::Off,
         const typename ELFT::Addr, size_t) {},
      /*ProcessNoteFn=*/FindAndroidNote, /*FinishNotesFn=*/[]() {});

  ArrayRef<uint8_t> Contents = getMemtagGlobalsSectionContents(MemtagGlobals);
  if (Contents.size() != MemtagGlobalsSz) {
    reportUniqueWarning(
        "mismatch between DT_AARCH64_MEMTAG_GLOBALSSZ (0x" +
        Twine::utohexstr(MemtagGlobalsSz) +
        ") and SHT_AARCH64_MEMTAG_GLOBALS_DYNAMIC section size (0x" +
        Twine::utohexstr(Contents.size()) + ")");
    Contents = ArrayRef<uint8_t>();
  }

  std::vector<std::pair<uint64_t, uint64_t>> GlobalDescriptors;
  uint64_t Address = 0;
  // See the AArch64 MemtagABI document for a description of encoding scheme:
  // https://github.com/ARM-software/abi-aa/blob/main/memtagabielf64/memtagabielf64.rst#83encoding-of-sht_aarch64_memtag_globals_dynamic
  for (size_t I = 0; I < Contents.size();) {
    const char *Error = nullptr;
    unsigned DecodedBytes = 0;
    uint64_t Value = decodeULEB128(Contents.data() + I, &DecodedBytes,
                                   Contents.end(), &Error);
    I += DecodedBytes;
    if (Error) {
      reportUniqueWarning(
          "error decoding distance uleb, " + Twine(DecodedBytes) +
          " byte(s) into SHT_AARCH64_MEMTAG_GLOBALS_DYNAMIC: " + Twine(Error));
      GlobalDescriptors.clear();
      break;
    }
    uint64_t Distance = Value >> MemtagStepVarintReservedBits;
    uint64_t GranulesToTag = Value & ((1 << MemtagStepVarintReservedBits) - 1);
    if (GranulesToTag == 0) {
      GranulesToTag = decodeULEB128(Contents.data() + I, &DecodedBytes,
                                    Contents.end(), &Error) +
                      1;
      I += DecodedBytes;
      if (Error) {
        reportUniqueWarning(
            "error decoding size-only uleb, " + Twine(DecodedBytes) +
            " byte(s) into SHT_AARCH64_MEMTAG_GLOBALS_DYNAMIC: " + Twine(Error));
        GlobalDescriptors.clear();
        break;
      }
    }
    Address += Distance * MemtagGranuleSize;
    GlobalDescriptors.emplace_back(Address, GranulesToTag * MemtagGranuleSize);
    Address += GranulesToTag * MemtagGranuleSize;
  }

  printMemtag(DynamicEntries, AndroidNoteDesc, GlobalDescriptors);
}

template <class ELFT> void GNUELFDumper<ELFT>::printELFLinkerOptions() {
  OS << "printELFLinkerOptions not implemented!\n";
}

template <class ELFT>
void ELFDumper<ELFT>::printDependentLibsHelper(
    function_ref<void(const Elf_Shdr &)> OnSectionStart,
    function_ref<void(StringRef, uint64_t)> OnLibEntry) {
  auto Warn = [this](unsigned SecNdx, StringRef Msg) {
    this->reportUniqueWarning("SHT_LLVM_DEPENDENT_LIBRARIES section at index " +
                              Twine(SecNdx) + " is broken: " + Msg);
  };

  unsigned I = -1;
  for (const Elf_Shdr &Shdr : cantFail(Obj.sections())) {
    ++I;
    if (Shdr.sh_type != ELF::SHT_LLVM_DEPENDENT_LIBRARIES)
      continue;

    OnSectionStart(Shdr);

    Expected<ArrayRef<uint8_t>> ContentsOrErr = Obj.getSectionContents(Shdr);
    if (!ContentsOrErr) {
      Warn(I, toString(ContentsOrErr.takeError()));
      continue;
    }

    ArrayRef<uint8_t> Contents = *ContentsOrErr;
    if (!Contents.empty() && Contents.back() != 0) {
      Warn(I, "the content is not null-terminated");
      continue;
    }

    for (const uint8_t *I = Contents.begin(), *E = Contents.end(); I < E;) {
      StringRef Lib((const char *)I);
      OnLibEntry(Lib, I - Contents.begin());
      I += Lib.size() + 1;
    }
  }
}

template <class ELFT>
void ELFDumper<ELFT>::forEachRelocationDo(
    const Elf_Shdr &Sec,
    llvm::function_ref<void(const Relocation<ELFT> &, unsigned,
                            const Elf_Shdr &, const Elf_Shdr *)>
        RelRelaFn) {
  auto Warn = [&](Error &&E,
                  const Twine &Prefix = "unable to read relocations from") {
    this->reportUniqueWarning(Prefix + " " + describe(Sec) + ": " +
                              toString(std::move(E)));
  };

  // SHT_RELR/SHT_ANDROID_RELR/SHT_AARCH64_AUTH_RELR sections do not have an
  // associated symbol table. For them we should not treat the value of the
  // sh_link field as an index of a symbol table.
  const Elf_Shdr *SymTab;
  if (Sec.sh_type != ELF::SHT_RELR && Sec.sh_type != ELF::SHT_ANDROID_RELR &&
      !(Obj.getHeader().e_machine == EM_AARCH64 &&
        Sec.sh_type == ELF::SHT_AARCH64_AUTH_RELR)) {
    Expected<const Elf_Shdr *> SymTabOrErr = Obj.getSection(Sec.sh_link);
    if (!SymTabOrErr) {
      Warn(SymTabOrErr.takeError(), "unable to locate a symbol table for");
      return;
    }
    SymTab = *SymTabOrErr;
  }

  unsigned RelNdx = 0;
  const bool IsMips64EL = this->Obj.isMips64EL();
  switch (Sec.sh_type) {
  case ELF::SHT_REL:
    if (Expected<Elf_Rel_Range> RangeOrErr = Obj.rels(Sec)) {
      for (const Elf_Rel &R : *RangeOrErr)
        RelRelaFn(Relocation<ELFT>(R, IsMips64EL), RelNdx++, Sec, SymTab);
    } else {
      Warn(RangeOrErr.takeError());
    }
    break;
  case ELF::SHT_RELA:
    if (Expected<Elf_Rela_Range> RangeOrErr = Obj.relas(Sec)) {
      for (const Elf_Rela &R : *RangeOrErr)
        RelRelaFn(Relocation<ELFT>(R, IsMips64EL), RelNdx++, Sec, SymTab);
    } else {
      Warn(RangeOrErr.takeError());
    }
    break;
  case ELF::SHT_AARCH64_AUTH_RELR:
    if (Obj.getHeader().e_machine != EM_AARCH64)
      break;
    [[fallthrough]];
  case ELF::SHT_RELR:
  case ELF::SHT_ANDROID_RELR: {
    Expected<Elf_Relr_Range> RangeOrErr = Obj.relrs(Sec);
    if (!RangeOrErr) {
      Warn(RangeOrErr.takeError());
      break;
    }

    for (const Elf_Rel &R : Obj.decode_relrs(*RangeOrErr))
      RelRelaFn(Relocation<ELFT>(R, IsMips64EL), RelNdx++, Sec,
                /*SymTab=*/nullptr);
    break;
  }
  case ELF::SHT_CREL: {
    if (auto RelsOrRelas = Obj.crels(Sec)) {
      for (const Elf_Rel &R : RelsOrRelas->first)
        RelRelaFn(Relocation<ELFT>(R, false), RelNdx++, Sec, SymTab);
      for (const Elf_Rela &R : RelsOrRelas->second)
        RelRelaFn(Relocation<ELFT>(R, false), RelNdx++, Sec, SymTab);
    } else {
      Warn(RelsOrRelas.takeError());
    }
    break;
  }
  case ELF::SHT_ANDROID_REL:
  case ELF::SHT_ANDROID_RELA:
    if (Expected<std::vector<Elf_Rela>> RelasOrErr = Obj.android_relas(Sec)) {
      for (const Elf_Rela &R : *RelasOrErr)
        RelRelaFn(Relocation<ELFT>(R, IsMips64EL), RelNdx++, Sec, SymTab);
    } else {
      Warn(RelasOrErr.takeError());
    }
    break;
  }
}

template <class ELFT>
StringRef ELFDumper<ELFT>::getPrintableSectionName(const Elf_Shdr &Sec) const {
  StringRef Name = "<?>";
  if (Expected<StringRef> SecNameOrErr =
          Obj.getSectionName(Sec, this->WarningHandler))
    Name = *SecNameOrErr;
  else
    this->reportUniqueWarning("unable to get the name of " + describe(Sec) +
                              ": " + toString(SecNameOrErr.takeError()));
  return Name;
}

template <class ELFT> void GNUELFDumper<ELFT>::printDependentLibs() {
  bool SectionStarted = false;
  struct NameOffset {
    StringRef Name;
    uint64_t Offset;
  };
  std::vector<NameOffset> SecEntries;
  NameOffset Current;
  auto PrintSection = [&]() {
    OS << "Dependent libraries section " << Current.Name << " at offset "
       << format_hex(Current.Offset, 1) << " contains " << SecEntries.size()
       << " entries:\n";
    for (NameOffset Entry : SecEntries)
      OS << "  [" << format("%6" PRIx64, Entry.Offset) << "]  " << Entry.Name
         << "\n";
    OS << "\n";
    SecEntries.clear();
  };

  auto OnSectionStart = [&](const Elf_Shdr &Shdr) {
    if (SectionStarted)
      PrintSection();
    SectionStarted = true;
    Current.Offset = Shdr.sh_offset;
    Current.Name = this->getPrintableSectionName(Shdr);
  };
  auto OnLibEntry = [&](StringRef Lib, uint64_t Offset) {
    SecEntries.push_back(NameOffset{Lib, Offset});
  };

  this->printDependentLibsHelper(OnSectionStart, OnLibEntry);
  if (SectionStarted)
    PrintSection();
}

template <class ELFT>
SmallVector<uint32_t> ELFDumper<ELFT>::getSymbolIndexesForFunctionAddress(
    uint64_t SymValue, std::optional<const Elf_Shdr *> FunctionSec) {
  SmallVector<uint32_t> SymbolIndexes;
  if (!this->AddressToIndexMap) {
    // Populate the address to index map upon the first invocation of this
    // function.
    this->AddressToIndexMap.emplace();
    if (this->DotSymtabSec) {
      if (Expected<Elf_Sym_Range> SymsOrError =
              Obj.symbols(this->DotSymtabSec)) {
        uint32_t Index = (uint32_t)-1;
        for (const Elf_Sym &Sym : *SymsOrError) {
          ++Index;

          if (Sym.st_shndx == ELF::SHN_UNDEF || Sym.getType() != ELF::STT_FUNC)
            continue;

          Expected<uint64_t> SymAddrOrErr =
              ObjF.toSymbolRef(this->DotSymtabSec, Index).getAddress();
          if (!SymAddrOrErr) {
            std::string Name = this->getStaticSymbolName(Index);
            reportUniqueWarning("unable to get address of symbol '" + Name +
                                "': " + toString(SymAddrOrErr.takeError()));
            return SymbolIndexes;
          }

          (*this->AddressToIndexMap)[*SymAddrOrErr].push_back(Index);
        }
      } else {
        reportUniqueWarning("unable to read the symbol table: " +
                            toString(SymsOrError.takeError()));
      }
    }
  }

  auto Symbols = this->AddressToIndexMap->find(SymValue);
  if (Symbols == this->AddressToIndexMap->end())
    return SymbolIndexes;

  for (uint32_t Index : Symbols->second) {
    // Check if the symbol is in the right section. FunctionSec == None
    // means "any section".
    if (FunctionSec) {
      const Elf_Sym &Sym = *cantFail(Obj.getSymbol(this->DotSymtabSec, Index));
      if (Expected<const Elf_Shdr *> SecOrErr =
              Obj.getSection(Sym, this->DotSymtabSec,
                             this->getShndxTable(this->DotSymtabSec))) {
        if (*FunctionSec != *SecOrErr)
          continue;
      } else {
        std::string Name = this->getStaticSymbolName(Index);
        // Note: it is impossible to trigger this error currently, it is
        // untested.
        reportUniqueWarning("unable to get section of symbol '" + Name +
                            "': " + toString(SecOrErr.takeError()));
        return SymbolIndexes;
      }
    }

    SymbolIndexes.push_back(Index);
  }

  return SymbolIndexes;
}

template <class ELFT>
bool ELFDumper<ELFT>::printFunctionStackSize(
    uint64_t SymValue, std::optional<const Elf_Shdr *> FunctionSec,
    const Elf_Shdr &StackSizeSec, DataExtractor Data, uint64_t *Offset) {
  SmallVector<uint32_t> FuncSymIndexes =
      this->getSymbolIndexesForFunctionAddress(SymValue, FunctionSec);
  if (FuncSymIndexes.empty())
    reportUniqueWarning(
        "could not identify function symbol for stack size entry in " +
        describe(StackSizeSec));

  // Extract the size. The expectation is that Offset is pointing to the right
  // place, i.e. past the function address.
  Error Err = Error::success();
  uint64_t StackSize = Data.getULEB128(Offset, &Err);
  if (Err) {
    reportUniqueWarning("could not extract a valid stack size from " +
                        describe(StackSizeSec) + ": " +
                        toString(std::move(Err)));
    return false;
  }

  if (FuncSymIndexes.empty()) {
    printStackSizeEntry(StackSize, {"?"});
  } else {
    SmallVector<std::string> FuncSymNames;
    for (uint32_t Index : FuncSymIndexes)
      FuncSymNames.push_back(this->getStaticSymbolName(Index));
    printStackSizeEntry(StackSize, FuncSymNames);
  }

  return true;
}

template <class ELFT>
void GNUELFDumper<ELFT>::printStackSizeEntry(uint64_t Size,
                                             ArrayRef<std::string> FuncNames) {
  OS.PadToColumn(2);
  OS << format_decimal(Size, 11);
  OS.PadToColumn(18);

  OS << join(FuncNames.begin(), FuncNames.end(), ", ") << "\n";
}

template <class ELFT>
void ELFDumper<ELFT>::printStackSize(const Relocation<ELFT> &R,
                                     const Elf_Shdr &RelocSec, unsigned Ndx,
                                     const Elf_Shdr *SymTab,
                                     const Elf_Shdr *FunctionSec,
                                     const Elf_Shdr &StackSizeSec,
                                     const RelocationResolver &Resolver,
                                     DataExtractor Data) {
  // This function ignores potentially erroneous input, unless it is directly
  // related to stack size reporting.
  const Elf_Sym *Sym = nullptr;
  Expected<RelSymbol<ELFT>> TargetOrErr = this->getRelocationTarget(R, SymTab);
  if (!TargetOrErr)
    reportUniqueWarning("unable to get the target of relocation with index " +
                        Twine(Ndx) + " in " + describe(RelocSec) + ": " +
                        toString(TargetOrErr.takeError()));
  else
    Sym = TargetOrErr->Sym;

  uint64_t RelocSymValue = 0;
  if (Sym) {
    Expected<const Elf_Shdr *> SectionOrErr =
        this->Obj.getSection(*Sym, SymTab, this->getShndxTable(SymTab));
    if (!SectionOrErr) {
      reportUniqueWarning(
          "cannot identify the section for relocation symbol '" +
          (*TargetOrErr).Name + "': " + toString(SectionOrErr.takeError()));
    } else if (*SectionOrErr != FunctionSec) {
      reportUniqueWarning("relocation symbol '" + (*TargetOrErr).Name +
                          "' is not in the expected section");
      // Pretend that the symbol is in the correct section and report its
      // stack size anyway.
      FunctionSec = *SectionOrErr;
    }

    RelocSymValue = Sym->st_value;
  }

  uint64_t Offset = R.Offset;
  if (!Data.isValidOffsetForDataOfSize(Offset, sizeof(Elf_Addr) + 1)) {
    reportUniqueWarning("found invalid relocation offset (0x" +
                        Twine::utohexstr(Offset) + ") into " +
                        describe(StackSizeSec) +
                        " while trying to extract a stack size entry");
    return;
  }

  uint64_t SymValue = Resolver(R.Type, Offset, RelocSymValue,
                               Data.getAddress(&Offset), R.Addend.value_or(0));
  this->printFunctionStackSize(SymValue, FunctionSec, StackSizeSec, Data,
                               &Offset);
}

template <class ELFT>
void ELFDumper<ELFT>::printNonRelocatableStackSizes(
    std::function<void()> PrintHeader) {
  // This function ignores potentially erroneous input, unless it is directly
  // related to stack size reporting.
  for (const Elf_Shdr &Sec : cantFail(Obj.sections())) {
    if (this->getPrintableSectionName(Sec) != ".stack_sizes")
      continue;
    PrintHeader();
    ArrayRef<uint8_t> Contents =
        unwrapOrError(this->FileName, Obj.getSectionContents(Sec));
    DataExtractor Data(Contents, Obj.isLE(), sizeof(Elf_Addr));
    uint64_t Offset = 0;
    while (Offset < Contents.size()) {
      // The function address is followed by a ULEB representing the stack
      // size. Check for an extra byte before we try to process the entry.
      if (!Data.isValidOffsetForDataOfSize(Offset, sizeof(Elf_Addr) + 1)) {
        reportUniqueWarning(
            describe(Sec) +
            " ended while trying to extract a stack size entry");
        break;
      }
      uint64_t SymValue = Data.getAddress(&Offset);
      if (!printFunctionStackSize(SymValue, /*FunctionSec=*/std::nullopt, Sec,
                                  Data, &Offset))
        break;
    }
  }
}

template <class ELFT>
void ELFDumper<ELFT>::printRelocatableStackSizes(
    std::function<void()> PrintHeader) {
  // Build a map between stack size sections and their corresponding relocation
  // sections.
  auto IsMatch = [&](const Elf_Shdr &Sec) -> bool {
    StringRef SectionName;
    if (Expected<StringRef> NameOrErr = Obj.getSectionName(Sec))
      SectionName = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());

    return SectionName == ".stack_sizes";
  };

  Expected<MapVector<const Elf_Shdr *, const Elf_Shdr *>>
      StackSizeRelocMapOrErr = Obj.getSectionAndRelocations(IsMatch);
  if (!StackSizeRelocMapOrErr) {
    reportUniqueWarning("unable to get stack size map section(s): " +
                        toString(StackSizeRelocMapOrErr.takeError()));
    return;
  }

  for (const auto &StackSizeMapEntry : *StackSizeRelocMapOrErr) {
    PrintHeader();
    const Elf_Shdr *StackSizesELFSec = StackSizeMapEntry.first;
    const Elf_Shdr *RelocSec = StackSizeMapEntry.second;

    // Warn about stack size sections without a relocation section.
    if (!RelocSec) {
      reportWarning(createError(".stack_sizes (" + describe(*StackSizesELFSec) +
                                ") does not have a corresponding "
                                "relocation section"),
                    FileName);
      continue;
    }

    // A .stack_sizes section header's sh_link field is supposed to point
    // to the section that contains the functions whose stack sizes are
    // described in it.
    const Elf_Shdr *FunctionSec = unwrapOrError(
        this->FileName, Obj.getSection(StackSizesELFSec->sh_link));

    SupportsRelocation IsSupportedFn;
    RelocationResolver Resolver;
    std::tie(IsSupportedFn, Resolver) = getRelocationResolver(this->ObjF);
    ArrayRef<uint8_t> Contents =
        unwrapOrError(this->FileName, Obj.getSectionContents(*StackSizesELFSec));
    DataExtractor Data(Contents, Obj.isLE(), sizeof(Elf_Addr));

    forEachRelocationDo(
        *RelocSec, [&](const Relocation<ELFT> &R, unsigned Ndx,
                       const Elf_Shdr &Sec, const Elf_Shdr *SymTab) {
          if (!IsSupportedFn || !IsSupportedFn(R.Type)) {
            reportUniqueWarning(
                describe(*RelocSec) +
                " contains an unsupported relocation with index " + Twine(Ndx) +
                ": " + Obj.getRelocationTypeName(R.Type));
            return;
          }

          this->printStackSize(R, *RelocSec, Ndx, SymTab, FunctionSec,
                               *StackSizesELFSec, Resolver, Data);
        });
  }
}

template <class ELFT>
void GNUELFDumper<ELFT>::printStackSizes() {
  bool HeaderHasBeenPrinted = false;
  auto PrintHeader = [&]() {
    if (HeaderHasBeenPrinted)
      return;
    OS << "\nStack Sizes:\n";
    OS.PadToColumn(9);
    OS << "Size";
    OS.PadToColumn(18);
    OS << "Functions\n";
    HeaderHasBeenPrinted = true;
  };

  // For non-relocatable objects, look directly for sections whose name starts
  // with .stack_sizes and process the contents.
  if (this->Obj.getHeader().e_type == ELF::ET_REL)
    this->printRelocatableStackSizes(PrintHeader);
  else
    this->printNonRelocatableStackSizes(PrintHeader);
}

template <class ELFT>
void GNUELFDumper<ELFT>::printMipsGOT(const MipsGOTParser<ELFT> &Parser) {
  size_t Bias = ELFT::Is64Bits ? 8 : 0;
  auto PrintEntry = [&](const Elf_Addr *E, StringRef Purpose) {
    OS.PadToColumn(2);
    OS << format_hex_no_prefix(Parser.getGotAddress(E), 8 + Bias);
    OS.PadToColumn(11 + Bias);
    OS << format_decimal(Parser.getGotOffset(E), 6) << "(gp)";
    OS.PadToColumn(22 + Bias);
    OS << format_hex_no_prefix(*E, 8 + Bias);
    OS.PadToColumn(31 + 2 * Bias);
    OS << Purpose << "\n";
  };

  OS << (Parser.IsStatic ? "Static GOT:\n" : "Primary GOT:\n");
  OS << " Canonical gp value: "
     << format_hex_no_prefix(Parser.getGp(), 8 + Bias) << "\n\n";

  OS << " Reserved entries:\n";
  if (ELFT::Is64Bits)
    OS << "           Address     Access          Initial Purpose\n";
  else
    OS << "   Address     Access  Initial Purpose\n";
  PrintEntry(Parser.getGotLazyResolver(), "Lazy resolver");
  if (Parser.getGotModulePointer())
    PrintEntry(Parser.getGotModulePointer(), "Module pointer (GNU extension)");

  if (!Parser.getLocalEntries().empty()) {
    OS << "\n";
    OS << " Local entries:\n";
    if (ELFT::Is64Bits)
      OS << "           Address     Access          Initial\n";
    else
      OS << "   Address     Access  Initial\n";
    for (auto &E : Parser.getLocalEntries())
      PrintEntry(&E, "");
  }

  if (Parser.IsStatic)
    return;

  if (!Parser.getGlobalEntries().empty()) {
    OS << "\n";
    OS << " Global entries:\n";
    if (ELFT::Is64Bits)
      OS << "           Address     Access          Initial         Sym.Val."
         << " Type    Ndx Name\n";
    else
      OS << "   Address     Access  Initial Sym.Val. Type    Ndx Name\n";

    DataRegion<Elf_Word> ShndxTable(
        (const Elf_Word *)this->DynSymTabShndxRegion.Addr, this->Obj.end());
    for (auto &E : Parser.getGlobalEntries()) {
      const Elf_Sym &Sym = *Parser.getGotSym(&E);
      const Elf_Sym &FirstSym = this->dynamic_symbols()[0];
      std::string SymName = this->getFullSymbolName(
          Sym, &Sym - &FirstSym, ShndxTable, this->DynamicStringTable, false);

      OS.PadToColumn(2);
      OS << to_string(format_hex_no_prefix(Parser.getGotAddress(&E), 8 + Bias));
      OS.PadToColumn(11 + Bias);
      OS << to_string(format_decimal(Parser.getGotOffset(&E), 6)) + "(gp)";
      OS.PadToColumn(22 + Bias);
      OS << to_string(format_hex_no_prefix(E, 8 + Bias));
      OS.PadToColumn(31 + 2 * Bias);
      OS << to_string(format_hex_no_prefix(Sym.st_value, 8 + Bias));
      OS.PadToColumn(40 + 3 * Bias);
      OS << enumToString(Sym.getType(), ArrayRef(ElfSymbolTypes));
      OS.PadToColumn(48 + 3 * Bias);
      OS << getSymbolSectionNdx(Sym, &Sym - this->dynamic_symbols().begin(),
                                ShndxTable);
      OS.PadToColumn(52 + 3 * Bias);
      OS << SymName << "\n";
    }
  }

  if (!Parser.getOtherEntries().empty())
    OS << "\n Number of TLS and multi-GOT entries "
       << Parser.getOtherEntries().size() << "\n";
}

template <class ELFT>
void GNUELFDumper<ELFT>::printMipsPLT(const MipsGOTParser<ELFT> &Parser) {
  size_t Bias = ELFT::Is64Bits ? 8 : 0;
  auto PrintEntry = [&](const Elf_Addr *E, StringRef Purpose) {
    OS.PadToColumn(2);
    OS << format_hex_no_prefix(Parser.getPltAddress(E), 8 + Bias);
    OS.PadToColumn(11 + Bias);
    OS << format_hex_no_prefix(*E, 8 + Bias);
    OS.PadToColumn(20 + 2 * Bias);
    OS << Purpose << "\n";
  };

  OS << "PLT GOT:\n\n";

  OS << " Reserved entries:\n";
  OS << "   Address  Initial Purpose\n";
  PrintEntry(Parser.getPltLazyResolver(), "PLT lazy resolver");
  if (Parser.getPltModulePointer())
    PrintEntry(Parser.getPltModulePointer(), "Module pointer");

  if (!Parser.getPltEntries().empty()) {
    OS << "\n";
    OS << " Entries:\n";
    OS << "   Address  Initial Sym.Val. Type    Ndx Name\n";
    DataRegion<Elf_Word> ShndxTable(
        (const Elf_Word *)this->DynSymTabShndxRegion.Addr, this->Obj.end());
    for (auto &E : Parser.getPltEntries()) {
      const Elf_Sym &Sym = *Parser.getPltSym(&E);
      const Elf_Sym &FirstSym = *cantFail(
          this->Obj.template getEntry<Elf_Sym>(*Parser.getPltSymTable(), 0));
      std::string SymName = this->getFullSymbolName(
          Sym, &Sym - &FirstSym, ShndxTable, this->DynamicStringTable, false);

      OS.PadToColumn(2);
      OS << to_string(format_hex_no_prefix(Parser.getPltAddress(&E), 8 + Bias));
      OS.PadToColumn(11 + Bias);
      OS << to_string(format_hex_no_prefix(E, 8 + Bias));
      OS.PadToColumn(20 + 2 * Bias);
      OS << to_string(format_hex_no_prefix(Sym.st_value, 8 + Bias));
      OS.PadToColumn(29 + 3 * Bias);
      OS << enumToString(Sym.getType(), ArrayRef(ElfSymbolTypes));
      OS.PadToColumn(37 + 3 * Bias);
      OS << getSymbolSectionNdx(Sym, &Sym - this->dynamic_symbols().begin(),
                                ShndxTable);
      OS.PadToColumn(41 + 3 * Bias);
      OS << SymName << "\n";
    }
  }
}

template <class ELFT>
Expected<const Elf_Mips_ABIFlags<ELFT> *>
getMipsAbiFlagsSection(const ELFDumper<ELFT> &Dumper) {
  const typename ELFT::Shdr *Sec = Dumper.findSectionByName(".MIPS.abiflags");
  if (Sec == nullptr)
    return nullptr;

  constexpr StringRef ErrPrefix = "unable to read the .MIPS.abiflags section: ";
  Expected<ArrayRef<uint8_t>> DataOrErr =
      Dumper.getElfObject().getELFFile().getSectionContents(*Sec);
  if (!DataOrErr)
    return createError(ErrPrefix + toString(DataOrErr.takeError()));

  if (DataOrErr->size() != sizeof(Elf_Mips_ABIFlags<ELFT>))
    return createError(ErrPrefix + "it has a wrong size (" +
        Twine(DataOrErr->size()) + ")");
  return reinterpret_cast<const Elf_Mips_ABIFlags<ELFT> *>(DataOrErr->data());
}

template <class ELFT> void GNUELFDumper<ELFT>::printMipsABIFlags() {
  const Elf_Mips_ABIFlags<ELFT> *Flags = nullptr;
  if (Expected<const Elf_Mips_ABIFlags<ELFT> *> SecOrErr =
          getMipsAbiFlagsSection(*this))
    Flags = *SecOrErr;
  else
    this->reportUniqueWarning(SecOrErr.takeError());
  if (!Flags)
    return;

  OS << "MIPS ABI Flags Version: " << Flags->version << "\n\n";
  OS << "ISA: MIPS" << int(Flags->isa_level);
  if (Flags->isa_rev > 1)
    OS << "r" << int(Flags->isa_rev);
  OS << "\n";
  OS << "GPR size: " << getMipsRegisterSize(Flags->gpr_size) << "\n";
  OS << "CPR1 size: " << getMipsRegisterSize(Flags->cpr1_size) << "\n";
  OS << "CPR2 size: " << getMipsRegisterSize(Flags->cpr2_size) << "\n";
  OS << "FP ABI: " << enumToString(Flags->fp_abi, ArrayRef(ElfMipsFpABIType))
     << "\n";
  OS << "ISA Extension: "
     << enumToString(Flags->isa_ext, ArrayRef(ElfMipsISAExtType)) << "\n";
  if (Flags->ases == 0)
    OS << "ASEs: None\n";
  else
    // FIXME: Print each flag on a separate line.
    OS << "ASEs: " << printFlags(Flags->ases, ArrayRef(ElfMipsASEFlags))
       << "\n";
  OS << "FLAGS 1: " << format_hex_no_prefix(Flags->flags1, 8, false) << "\n";
  OS << "FLAGS 2: " << format_hex_no_prefix(Flags->flags2, 8, false) << "\n";
  OS << "\n";
}

template <class ELFT> void LLVMELFDumper<ELFT>::printFileHeaders() {
  const Elf_Ehdr &E = this->Obj.getHeader();
  {
    DictScope D(W, "ElfHeader");
    {
      DictScope D(W, "Ident");
      W.printBinary("Magic",
                    ArrayRef<unsigned char>(E.e_ident).slice(ELF::EI_MAG0, 4));
      W.printEnum("Class", E.e_ident[ELF::EI_CLASS], ArrayRef(ElfClass));
      W.printEnum("DataEncoding", E.e_ident[ELF::EI_DATA],
                  ArrayRef(ElfDataEncoding));
      W.printNumber("FileVersion", E.e_ident[ELF::EI_VERSION]);

      auto OSABI = ArrayRef(ElfOSABI);
      if (E.e_ident[ELF::EI_OSABI] >= ELF::ELFOSABI_FIRST_ARCH &&
          E.e_ident[ELF::EI_OSABI] <= ELF::ELFOSABI_LAST_ARCH) {
        switch (E.e_machine) {
        case ELF::EM_AMDGPU:
          OSABI = ArrayRef(AMDGPUElfOSABI);
          break;
        case ELF::EM_ARM:
          OSABI = ArrayRef(ARMElfOSABI);
          break;
        case ELF::EM_TI_C6000:
          OSABI = ArrayRef(C6000ElfOSABI);
          break;
        }
      }
      W.printEnum("OS/ABI", E.e_ident[ELF::EI_OSABI], OSABI);
      W.printNumber("ABIVersion", E.e_ident[ELF::EI_ABIVERSION]);
      W.printBinary("Unused",
                    ArrayRef<unsigned char>(E.e_ident).slice(ELF::EI_PAD));
    }

    std::string TypeStr;
    if (const EnumEntry<unsigned> *Ent = getObjectFileEnumEntry(E.e_type)) {
      TypeStr = Ent->Name.str();
    } else {
      if (E.e_type >= ET_LOPROC)
        TypeStr = "Processor Specific";
      else if (E.e_type >= ET_LOOS)
        TypeStr = "OS Specific";
      else
        TypeStr = "Unknown";
    }
    W.printString("Type", TypeStr + " (0x" + utohexstr(E.e_type) + ")");

    W.printEnum("Machine", E.e_machine, ArrayRef(ElfMachineType));
    W.printNumber("Version", E.e_version);
    W.printHex("Entry", E.e_entry);
    W.printHex("ProgramHeaderOffset", E.e_phoff);
    W.printHex("SectionHeaderOffset", E.e_shoff);
    if (E.e_machine == EM_MIPS)
      W.printFlags("Flags", E.e_flags, ArrayRef(ElfHeaderMipsFlags),
                   unsigned(ELF::EF_MIPS_ARCH), unsigned(ELF::EF_MIPS_ABI),
                   unsigned(ELF::EF_MIPS_MACH));
    else if (E.e_machine == EM_AMDGPU) {
      switch (E.e_ident[ELF::EI_ABIVERSION]) {
      default:
        W.printHex("Flags", E.e_flags);
        break;
      case 0:
        // ELFOSABI_AMDGPU_PAL, ELFOSABI_AMDGPU_MESA3D support *_V3 flags.
        [[fallthrough]];
      case ELF::ELFABIVERSION_AMDGPU_HSA_V3:
        W.printFlags("Flags", E.e_flags,
                     ArrayRef(ElfHeaderAMDGPUFlagsABIVersion3),
                     unsigned(ELF::EF_AMDGPU_MACH));
        break;
      case ELF::ELFABIVERSION_AMDGPU_HSA_V4:
      case ELF::ELFABIVERSION_AMDGPU_HSA_V5:
        W.printFlags("Flags", E.e_flags,
                     ArrayRef(ElfHeaderAMDGPUFlagsABIVersion4),
                     unsigned(ELF::EF_AMDGPU_MACH),
                     unsigned(ELF::EF_AMDGPU_FEATURE_XNACK_V4),
                     unsigned(ELF::EF_AMDGPU_FEATURE_SRAMECC_V4));
        break;
      case ELF::ELFABIVERSION_AMDGPU_HSA_V6: {
        std::optional<FlagEntry> VerFlagEntry;
        // The string needs to remain alive from the moment we create a
        // FlagEntry until printFlags is done.
        std::string FlagStr;
        if (auto VersionFlag = E.e_flags & ELF::EF_AMDGPU_GENERIC_VERSION) {
          unsigned Version =
              VersionFlag >> ELF::EF_AMDGPU_GENERIC_VERSION_OFFSET;
          FlagStr = "EF_AMDGPU_GENERIC_VERSION_V" + std::to_string(Version);
          VerFlagEntry = FlagEntry(FlagStr, VersionFlag);
        }
        W.printFlags(
            "Flags", E.e_flags, ArrayRef(ElfHeaderAMDGPUFlagsABIVersion4),
            unsigned(ELF::EF_AMDGPU_MACH),
            unsigned(ELF::EF_AMDGPU_FEATURE_XNACK_V4),
            unsigned(ELF::EF_AMDGPU_FEATURE_SRAMECC_V4),
            VerFlagEntry ? ArrayRef(*VerFlagEntry) : ArrayRef<FlagEntry>());
        break;
      }
      }
    } else if (E.e_machine == EM_RISCV)
      W.printFlags("Flags", E.e_flags, ArrayRef(ElfHeaderRISCVFlags));
    else if (E.e_machine == EM_AVR)
      W.printFlags("Flags", E.e_flags, ArrayRef(ElfHeaderAVRFlags),
                   unsigned(ELF::EF_AVR_ARCH_MASK));
    else if (E.e_machine == EM_LOONGARCH)
      W.printFlags("Flags", E.e_flags, ArrayRef(ElfHeaderLoongArchFlags),
                   unsigned(ELF::EF_LOONGARCH_ABI_MODIFIER_MASK),
                   unsigned(ELF::EF_LOONGARCH_OBJABI_MASK));
    else if (E.e_machine == EM_XTENSA)
      W.printFlags("Flags", E.e_flags, ArrayRef(ElfHeaderXtensaFlags),
                   unsigned(ELF::EF_XTENSA_MACH));
    else if (E.e_machine == EM_CUDA)
      W.printFlags("Flags", E.e_flags, ArrayRef(ElfHeaderNVPTXFlags),
                   unsigned(ELF::EF_CUDA_SM));
    else
      W.printFlags("Flags", E.e_flags);
    W.printNumber("HeaderSize", E.e_ehsize);
    W.printNumber("ProgramHeaderEntrySize", E.e_phentsize);
    W.printNumber("ProgramHeaderCount", E.e_phnum);
    W.printNumber("SectionHeaderEntrySize", E.e_shentsize);
    W.printString("SectionHeaderCount",
                  getSectionHeadersNumString(this->Obj, this->FileName));
    W.printString("StringTableSectionIndex",
                  getSectionHeaderTableIndexString(this->Obj, this->FileName));
  }
}

template <class ELFT> void LLVMELFDumper<ELFT>::printGroupSections() {
  DictScope Lists(W, "Groups");
  std::vector<GroupSection> V = this->getGroups();
  DenseMap<uint64_t, const GroupSection *> Map = mapSectionsToGroups(V);
  for (const GroupSection &G : V) {
    DictScope D(W, "Group");
    W.printNumber("Name", G.Name, G.ShName);
    W.printNumber("Index", G.Index);
    W.printNumber("Link", G.Link);
    W.printNumber("Info", G.Info);
    W.printHex("Type", getGroupType(G.Type), G.Type);
    W.printString("Signature", G.Signature);

    ListScope L(W, getGroupSectionHeaderName());
    for (const GroupMember &GM : G.Members) {
      const GroupSection *MainGroup = Map[GM.Index];
      if (MainGroup != &G)
        this->reportUniqueWarning(
            "section with index " + Twine(GM.Index) +
            ", included in the group section with index " +
            Twine(MainGroup->Index) +
            ", was also found in the group section with index " +
            Twine(G.Index));
      printSectionGroupMembers(GM.Name, GM.Index);
    }
  }

  if (V.empty())
    printEmptyGroupMessage();
}

template <class ELFT>
std::string LLVMELFDumper<ELFT>::getGroupSectionHeaderName() const {
  return "Section(s) in group";
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printSectionGroupMembers(StringRef Name,
                                                   uint64_t Idx) const {
  W.startLine() << Name << " (" << Idx << ")\n";
}

template <class ELFT> void LLVMELFDumper<ELFT>::printRelocations() {
  ListScope D(W, "Relocations");

  for (const Elf_Shdr &Sec : cantFail(this->Obj.sections())) {
    if (!isRelocationSec<ELFT>(Sec, this->Obj.getHeader()))
      continue;

    StringRef Name = this->getPrintableSectionName(Sec);
    unsigned SecNdx = &Sec - &cantFail(this->Obj.sections()).front();
    printRelocationSectionInfo(Sec, Name, SecNdx);
  }
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printExpandedRelRelaReloc(const Relocation<ELFT> &R,
                                                    StringRef SymbolName,
                                                    StringRef RelocName) {
  DictScope Group(W, "Relocation");
  W.printHex("Offset", R.Offset);
  W.printNumber("Type", RelocName, R.Type);
  W.printNumber("Symbol", !SymbolName.empty() ? SymbolName : "-", R.Symbol);
  if (R.Addend)
    W.printHex("Addend", (uintX_t)*R.Addend);
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printDefaultRelRelaReloc(const Relocation<ELFT> &R,
                                                   StringRef SymbolName,
                                                   StringRef RelocName) {
  raw_ostream &OS = W.startLine();
  OS << W.hex(R.Offset) << " " << RelocName << " "
     << (!SymbolName.empty() ? SymbolName : "-");
  if (R.Addend)
    OS << " " << W.hex((uintX_t)*R.Addend);
  OS << "\n";
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printRelocationSectionInfo(const Elf_Shdr &Sec,
                                                     StringRef Name,
                                                     const unsigned SecNdx) {
  DictScope D(W, (Twine("Section (") + Twine(SecNdx) + ") " + Name).str());
  this->printRelocationsHelper(Sec);
}

template <class ELFT> void LLVMELFDumper<ELFT>::printEmptyGroupMessage() const {
  W.startLine() << "There are no group sections in the file.\n";
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printRelRelaReloc(const Relocation<ELFT> &R,
                                            const RelSymbol<ELFT> &RelSym) {
  StringRef SymbolName = RelSym.Name;
  if (RelSym.Sym && RelSym.Name.empty())
    SymbolName = "<null>";
  SmallString<32> RelocName;
  this->Obj.getRelocationTypeName(R.Type, RelocName);

  if (opts::ExpandRelocs) {
    printExpandedRelRelaReloc(R, SymbolName, RelocName);
  } else {
    printDefaultRelRelaReloc(R, SymbolName, RelocName);
  }
}

template <class ELFT> void LLVMELFDumper<ELFT>::printSectionHeaders() {
  ListScope SectionsD(W, "Sections");

  int SectionIndex = -1;
  std::vector<EnumEntry<unsigned>> FlagsList =
      getSectionFlagsForTarget(this->Obj.getHeader().e_ident[ELF::EI_OSABI],
                               this->Obj.getHeader().e_machine);
  for (const Elf_Shdr &Sec : cantFail(this->Obj.sections())) {
    DictScope SectionD(W, "Section");
    W.printNumber("Index", ++SectionIndex);
    W.printNumber("Name", this->getPrintableSectionName(Sec), Sec.sh_name);
    W.printHex("Type",
               object::getELFSectionTypeName(this->Obj.getHeader().e_machine,
                                             Sec.sh_type),
               Sec.sh_type);
    W.printFlags("Flags", Sec.sh_flags, ArrayRef(FlagsList));
    W.printHex("Address", Sec.sh_addr);
    W.printHex("Offset", Sec.sh_offset);
    W.printNumber("Size", Sec.sh_size);
    W.printNumber("Link", Sec.sh_link);
    W.printNumber("Info", Sec.sh_info);
    W.printNumber("AddressAlignment", Sec.sh_addralign);
    W.printNumber("EntrySize", Sec.sh_entsize);

    if (opts::SectionRelocations) {
      ListScope D(W, "Relocations");
      this->printRelocationsHelper(Sec);
    }

    if (opts::SectionSymbols) {
      ListScope D(W, "Symbols");
      if (this->DotSymtabSec) {
        StringRef StrTable = unwrapOrError(
            this->FileName,
            this->Obj.getStringTableForSymtab(*this->DotSymtabSec));
        ArrayRef<Elf_Word> ShndxTable = this->getShndxTable(this->DotSymtabSec);

        typename ELFT::SymRange Symbols = unwrapOrError(
            this->FileName, this->Obj.symbols(this->DotSymtabSec));
        for (const Elf_Sym &Sym : Symbols) {
          const Elf_Shdr *SymSec = unwrapOrError(
              this->FileName,
              this->Obj.getSection(Sym, this->DotSymtabSec, ShndxTable));
          if (SymSec == &Sec)
            printSymbol(Sym, &Sym - &Symbols[0], ShndxTable, StrTable, false,
                        /*NonVisibilityBitsUsed=*/false,
                        /*ExtraSymInfo=*/false);
        }
      }
    }

    if (opts::SectionData && Sec.sh_type != ELF::SHT_NOBITS) {
      ArrayRef<uint8_t> Data =
          unwrapOrError(this->FileName, this->Obj.getSectionContents(Sec));
      W.printBinaryBlock(
          "SectionData",
          StringRef(reinterpret_cast<const char *>(Data.data()), Data.size()));
    }
  }
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printSymbolSection(
    const Elf_Sym &Symbol, unsigned SymIndex,
    DataRegion<Elf_Word> ShndxTable) const {
  auto GetSectionSpecialType = [&]() -> std::optional<StringRef> {
    if (Symbol.isUndefined())
      return StringRef("Undefined");
    if (Symbol.isProcessorSpecific())
      return StringRef("Processor Specific");
    if (Symbol.isOSSpecific())
      return StringRef("Operating System Specific");
    if (Symbol.isAbsolute())
      return StringRef("Absolute");
    if (Symbol.isCommon())
      return StringRef("Common");
    if (Symbol.isReserved() && Symbol.st_shndx != SHN_XINDEX)
      return StringRef("Reserved");
    return std::nullopt;
  };

  if (std::optional<StringRef> Type = GetSectionSpecialType()) {
    W.printHex("Section", *Type, Symbol.st_shndx);
    return;
  }

  Expected<unsigned> SectionIndex =
      this->getSymbolSectionIndex(Symbol, SymIndex, ShndxTable);
  if (!SectionIndex) {
    assert(Symbol.st_shndx == SHN_XINDEX &&
           "getSymbolSectionIndex should only fail due to an invalid "
           "SHT_SYMTAB_SHNDX table/reference");
    this->reportUniqueWarning(SectionIndex.takeError());
    W.printHex("Section", "Reserved", SHN_XINDEX);
    return;
  }

  Expected<StringRef> SectionName =
      this->getSymbolSectionName(Symbol, *SectionIndex);
  if (!SectionName) {
    // Don't report an invalid section name if the section headers are missing.
    // In such situations, all sections will be "invalid".
    if (!this->ObjF.sections().empty())
      this->reportUniqueWarning(SectionName.takeError());
    else
      consumeError(SectionName.takeError());
    W.printHex("Section", "<?>", *SectionIndex);
  } else {
    W.printHex("Section", *SectionName, *SectionIndex);
  }
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printSymbolOtherField(const Elf_Sym &Symbol) const {
  std::vector<EnumEntry<unsigned>> SymOtherFlags =
      this->getOtherFlagsFromSymbol(this->Obj.getHeader(), Symbol);
  W.printFlags("Other", Symbol.st_other, ArrayRef(SymOtherFlags), 0x3u);
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printZeroSymbolOtherField(
    const Elf_Sym &Symbol) const {
  assert(Symbol.st_other == 0 && "non-zero Other Field");
  // Usually st_other flag is zero. Do not pollute the output
  // by flags enumeration in that case.
  W.printNumber("Other", 0);
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printSymbol(const Elf_Sym &Symbol, unsigned SymIndex,
                                      DataRegion<Elf_Word> ShndxTable,
                                      std::optional<StringRef> StrTable,
                                      bool IsDynamic,
                                      bool /*NonVisibilityBitsUsed*/,
                                      bool /*ExtraSymInfo*/) const {
  std::string FullSymbolName = this->getFullSymbolName(
      Symbol, SymIndex, ShndxTable, StrTable, IsDynamic);
  unsigned char SymbolType = Symbol.getType();

  DictScope D(W, "Symbol");
  W.printNumber("Name", FullSymbolName, Symbol.st_name);
  W.printHex("Value", Symbol.st_value);
  W.printNumber("Size", Symbol.st_size);
  W.printEnum("Binding", Symbol.getBinding(), ArrayRef(ElfSymbolBindings));
  if (this->Obj.getHeader().e_machine == ELF::EM_AMDGPU &&
      SymbolType >= ELF::STT_LOOS && SymbolType < ELF::STT_HIOS)
    W.printEnum("Type", SymbolType, ArrayRef(AMDGPUSymbolTypes));
  else
    W.printEnum("Type", SymbolType, ArrayRef(ElfSymbolTypes));
  if (Symbol.st_other == 0)
    printZeroSymbolOtherField(Symbol);
  else
    printSymbolOtherField(Symbol);
  printSymbolSection(Symbol, SymIndex, ShndxTable);
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printSymbols(bool PrintSymbols,
                                       bool PrintDynamicSymbols,
                                       bool ExtraSymInfo) {
  if (PrintSymbols) {
    ListScope Group(W, "Symbols");
    this->printSymbolsHelper(false, ExtraSymInfo);
  }
  if (PrintDynamicSymbols) {
    ListScope Group(W, "DynamicSymbols");
    this->printSymbolsHelper(true, ExtraSymInfo);
  }
}

template <class ELFT> void LLVMELFDumper<ELFT>::printDynamicTable() {
  Elf_Dyn_Range Table = this->dynamic_table();
  if (Table.empty())
    return;

  W.startLine() << "DynamicSection [ (" << Table.size() << " entries)\n";

  size_t MaxTagSize = getMaxDynamicTagSize(this->Obj, Table);
  // The "Name/Value" column should be indented from the "Type" column by N
  // spaces, where N = MaxTagSize - length of "Type" (4) + trailing
  // space (1) = -3.
  W.startLine() << "  Tag" << std::string(ELFT::Is64Bits ? 16 : 8, ' ')
                << "Type" << std::string(MaxTagSize - 3, ' ') << "Name/Value\n";

  std::string ValueFmt = "%-" + std::to_string(MaxTagSize) + "s ";
  for (auto Entry : Table) {
    uintX_t Tag = Entry.getTag();
    std::string Value = this->getDynamicEntry(Tag, Entry.getVal());
    W.startLine() << "  " << format_hex(Tag, ELFT::Is64Bits ? 18 : 10, true)
                  << " "
                  << format(ValueFmt.c_str(),
                            this->Obj.getDynamicTagAsString(Tag).c_str())
                  << Value << "\n";
  }
  W.startLine() << "]\n";
}

template <class ELFT>
void JSONELFDumper<ELFT>::printAuxillaryDynamicTableEntryInfo(
    const Elf_Dyn &Entry) {
  auto FormatFlags = [this, Value = Entry.getVal()](auto Flags) {
    ListScope L(this->W, "Flags");
    for (const auto &Flag : Flags) {
      if (Flag.Value != 0 && (Value & Flag.Value) == Flag.Value)
        this->W.printString(Flag.Name);
    }
  };
  switch (Entry.getTag()) {
  case DT_SONAME:
    this->W.printString("Name", this->getDynamicString(Entry.getVal()));
    break;
  case DT_AUXILIARY:
  case DT_FILTER:
  case DT_NEEDED:
    this->W.printString("Library", this->getDynamicString(Entry.getVal()));
    break;
  case DT_USED:
    this->W.printString("Object", this->getDynamicString(Entry.getVal()));
    break;
  case DT_RPATH:
  case DT_RUNPATH: {
    StringRef Value = this->getDynamicString(Entry.getVal());
    ListScope L(this->W, "Path");
    while (!Value.empty()) {
      auto [Front, Back] = Value.split(':');
      this->W.printString(Front);
      Value = Back;
    }
    break;
  }
  case DT_FLAGS:
    FormatFlags(ArrayRef(ElfDynamicDTFlags));
    break;
  case DT_FLAGS_1:
    FormatFlags(ArrayRef(ElfDynamicDTFlags1));
    break;
  default:
    return;
  }
}

template <class ELFT> void JSONELFDumper<ELFT>::printDynamicTable() {
  Elf_Dyn_Range Table = this->dynamic_table();
  ListScope L(this->W, "DynamicSection");
  for (const auto &Entry : Table) {
    DictScope D(this->W);
    uintX_t Tag = Entry.getTag();
    this->W.printHex("Tag", Tag);
    this->W.printString("Type", this->Obj.getDynamicTagAsString(Tag));
    this->W.printHex("Value", Entry.getVal());
    this->printAuxillaryDynamicTableEntryInfo(Entry);
  }
}

template <class ELFT> void LLVMELFDumper<ELFT>::printDynamicRelocations() {
  W.startLine() << "Dynamic Relocations {\n";
  W.indent();
  this->printDynamicRelocationsHelper();
  W.unindent();
  W.startLine() << "}\n";
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printProgramHeaders(
    bool PrintProgramHeaders, cl::boolOrDefault PrintSectionMapping) {
  if (PrintProgramHeaders)
    printProgramHeaders();
  if (PrintSectionMapping == cl::BOU_TRUE)
    printSectionMapping();
}

template <class ELFT> void LLVMELFDumper<ELFT>::printProgramHeaders() {
  ListScope L(W, "ProgramHeaders");

  Expected<ArrayRef<Elf_Phdr>> PhdrsOrErr = this->Obj.program_headers();
  if (!PhdrsOrErr) {
    this->reportUniqueWarning("unable to dump program headers: " +
                              toString(PhdrsOrErr.takeError()));
    return;
  }

  for (const Elf_Phdr &Phdr : *PhdrsOrErr) {
    DictScope P(W, "ProgramHeader");
    StringRef Type =
        segmentTypeToString(this->Obj.getHeader().e_machine, Phdr.p_type);

    W.printHex("Type", Type.empty() ? "Unknown" : Type, Phdr.p_type);
    W.printHex("Offset", Phdr.p_offset);
    W.printHex("VirtualAddress", Phdr.p_vaddr);
    W.printHex("PhysicalAddress", Phdr.p_paddr);
    W.printNumber("FileSize", Phdr.p_filesz);
    W.printNumber("MemSize", Phdr.p_memsz);
    W.printFlags("Flags", Phdr.p_flags, ArrayRef(ElfSegmentFlags));
    W.printNumber("Alignment", Phdr.p_align);
  }
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printVersionSymbolSection(const Elf_Shdr *Sec) {
  ListScope SS(W, "VersionSymbols");
  if (!Sec)
    return;

  StringRef StrTable;
  ArrayRef<Elf_Sym> Syms;
  const Elf_Shdr *SymTabSec;
  Expected<ArrayRef<Elf_Versym>> VerTableOrErr =
      this->getVersionTable(*Sec, &Syms, &StrTable, &SymTabSec);
  if (!VerTableOrErr) {
    this->reportUniqueWarning(VerTableOrErr.takeError());
    return;
  }

  if (StrTable.empty() || Syms.empty() || Syms.size() != VerTableOrErr->size())
    return;

  ArrayRef<Elf_Word> ShNdxTable = this->getShndxTable(SymTabSec);
  for (size_t I = 0, E = Syms.size(); I < E; ++I) {
    DictScope S(W, "Symbol");
    W.printNumber("Version", (*VerTableOrErr)[I].vs_index & VERSYM_VERSION);
    W.printString("Name",
                  this->getFullSymbolName(Syms[I], I, ShNdxTable, StrTable,
                                          /*IsDynamic=*/true));
  }
}

const EnumEntry<unsigned> SymVersionFlags[] = {
    {"Base", "BASE", VER_FLG_BASE},
    {"Weak", "WEAK", VER_FLG_WEAK},
    {"Info", "INFO", VER_FLG_INFO}};

template <class ELFT>
void LLVMELFDumper<ELFT>::printVersionDefinitionSection(const Elf_Shdr *Sec) {
  ListScope SD(W, "VersionDefinitions");
  if (!Sec)
    return;

  Expected<std::vector<VerDef>> V = this->Obj.getVersionDefinitions(*Sec);
  if (!V) {
    this->reportUniqueWarning(V.takeError());
    return;
  }

  for (const VerDef &D : *V) {
    DictScope Def(W, "Definition");
    W.printNumber("Version", D.Version);
    W.printFlags("Flags", D.Flags, ArrayRef(SymVersionFlags));
    W.printNumber("Index", D.Ndx);
    W.printNumber("Hash", D.Hash);
    W.printString("Name", D.Name.c_str());
    W.printList(
        "Predecessors", D.AuxV,
        [](raw_ostream &OS, const VerdAux &Aux) { OS << Aux.Name.c_str(); });
  }
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printVersionDependencySection(const Elf_Shdr *Sec) {
  ListScope SD(W, "VersionRequirements");
  if (!Sec)
    return;

  Expected<std::vector<VerNeed>> V =
      this->Obj.getVersionDependencies(*Sec, this->WarningHandler);
  if (!V) {
    this->reportUniqueWarning(V.takeError());
    return;
  }

  for (const VerNeed &VN : *V) {
    DictScope Entry(W, "Dependency");
    W.printNumber("Version", VN.Version);
    W.printNumber("Count", VN.Cnt);
    W.printString("FileName", VN.File.c_str());

    ListScope L(W, "Entries");
    for (const VernAux &Aux : VN.AuxV) {
      DictScope Entry(W, "Entry");
      W.printNumber("Hash", Aux.Hash);
      W.printFlags("Flags", Aux.Flags, ArrayRef(SymVersionFlags));
      W.printNumber("Index", Aux.Other);
      W.printString("Name", Aux.Name.c_str());
    }
  }
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printHashHistogramStats(size_t NBucket,
                                                  size_t MaxChain,
                                                  size_t TotalSyms,
                                                  ArrayRef<size_t> Count,
                                                  bool IsGnu) const {
  StringRef HistName = IsGnu ? "GnuHashHistogram" : "HashHistogram";
  StringRef BucketName = IsGnu ? "Bucket" : "Chain";
  StringRef ListName = IsGnu ? "Buckets" : "Chains";
  DictScope Outer(W, HistName);
  W.printNumber("TotalBuckets", NBucket);
  ListScope Buckets(W, ListName);
  size_t CumulativeNonZero = 0;
  for (size_t I = 0; I < MaxChain; ++I) {
    CumulativeNonZero += Count[I] * I;
    DictScope Bucket(W, BucketName);
    W.printNumber("Length", I);
    W.printNumber("Count", Count[I]);
    W.printNumber("Percentage", (float)(Count[I] * 100.0) / NBucket);
    W.printNumber("Coverage", (float)(CumulativeNonZero * 100.0) / TotalSyms);
  }
}

// Returns true if rel/rela section exists, and populates SymbolIndices.
// Otherwise returns false.
template <class ELFT>
static bool getSymbolIndices(const typename ELFT::Shdr *CGRelSection,
                             const ELFFile<ELFT> &Obj,
                             const LLVMELFDumper<ELFT> *Dumper,
                             SmallVector<uint32_t, 128> &SymbolIndices) {
  if (!CGRelSection) {
    Dumper->reportUniqueWarning(
        "relocation section for a call graph section doesn't exist");
    return false;
  }

  if (CGRelSection->sh_type == SHT_REL) {
    typename ELFT::RelRange CGProfileRel;
    Expected<typename ELFT::RelRange> CGProfileRelOrError =
        Obj.rels(*CGRelSection);
    if (!CGProfileRelOrError) {
      Dumper->reportUniqueWarning("unable to load relocations for "
                                  "SHT_LLVM_CALL_GRAPH_PROFILE section: " +
                                  toString(CGProfileRelOrError.takeError()));
      return false;
    }

    CGProfileRel = *CGProfileRelOrError;
    for (const typename ELFT::Rel &Rel : CGProfileRel)
      SymbolIndices.push_back(Rel.getSymbol(Obj.isMips64EL()));
  } else {
    // MC unconditionally produces SHT_REL, but GNU strip/objcopy may convert
    // the format to SHT_RELA
    // (https://sourceware.org/bugzilla/show_bug.cgi?id=28035)
    typename ELFT::RelaRange CGProfileRela;
    Expected<typename ELFT::RelaRange> CGProfileRelaOrError =
        Obj.relas(*CGRelSection);
    if (!CGProfileRelaOrError) {
      Dumper->reportUniqueWarning("unable to load relocations for "
                                  "SHT_LLVM_CALL_GRAPH_PROFILE section: " +
                                  toString(CGProfileRelaOrError.takeError()));
      return false;
    }

    CGProfileRela = *CGProfileRelaOrError;
    for (const typename ELFT::Rela &Rela : CGProfileRela)
      SymbolIndices.push_back(Rela.getSymbol(Obj.isMips64EL()));
  }

  return true;
}

template <class ELFT> void LLVMELFDumper<ELFT>::printCGProfile() {
  auto IsMatch = [](const Elf_Shdr &Sec) -> bool {
    return Sec.sh_type == ELF::SHT_LLVM_CALL_GRAPH_PROFILE;
  };

  Expected<MapVector<const Elf_Shdr *, const Elf_Shdr *>> SecToRelocMapOrErr =
      this->Obj.getSectionAndRelocations(IsMatch);
  if (!SecToRelocMapOrErr) {
    this->reportUniqueWarning("unable to get CG Profile section(s): " +
                              toString(SecToRelocMapOrErr.takeError()));
    return;
  }

  for (const auto &CGMapEntry : *SecToRelocMapOrErr) {
    const Elf_Shdr *CGSection = CGMapEntry.first;
    const Elf_Shdr *CGRelSection = CGMapEntry.second;

    Expected<ArrayRef<Elf_CGProfile>> CGProfileOrErr =
        this->Obj.template getSectionContentsAsArray<Elf_CGProfile>(*CGSection);
    if (!CGProfileOrErr) {
      this->reportUniqueWarning(
          "unable to load the SHT_LLVM_CALL_GRAPH_PROFILE section: " +
          toString(CGProfileOrErr.takeError()));
      return;
    }

    SmallVector<uint32_t, 128> SymbolIndices;
    bool UseReloc =
        getSymbolIndices<ELFT>(CGRelSection, this->Obj, this, SymbolIndices);
    if (UseReloc && SymbolIndices.size() != CGProfileOrErr->size() * 2) {
      this->reportUniqueWarning(
          "number of from/to pairs does not match number of frequencies");
      UseReloc = false;
    }

    ListScope L(W, "CGProfile");
    for (uint32_t I = 0, Size = CGProfileOrErr->size(); I != Size; ++I) {
      const Elf_CGProfile &CGPE = (*CGProfileOrErr)[I];
      DictScope D(W, "CGProfileEntry");
      if (UseReloc) {
        uint32_t From = SymbolIndices[I * 2];
        uint32_t To = SymbolIndices[I * 2 + 1];
        W.printNumber("From", this->getStaticSymbolName(From), From);
        W.printNumber("To", this->getStaticSymbolName(To), To);
      }
      W.printNumber("Weight", CGPE.cgp_weight);
    }
  }
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printBBAddrMaps(bool PrettyPGOAnalysis) {
  bool IsRelocatable = this->Obj.getHeader().e_type == ELF::ET_REL;
  using Elf_Shdr = typename ELFT::Shdr;
  auto IsMatch = [](const Elf_Shdr &Sec) -> bool {
    return Sec.sh_type == ELF::SHT_LLVM_BB_ADDR_MAP;
  };
  Expected<MapVector<const Elf_Shdr *, const Elf_Shdr *>> SecRelocMapOrErr =
      this->Obj.getSectionAndRelocations(IsMatch);
  if (!SecRelocMapOrErr) {
    this->reportUniqueWarning(
        "failed to get SHT_LLVM_BB_ADDR_MAP section(s): " +
        toString(SecRelocMapOrErr.takeError()));
    return;
  }
  for (auto const &[Sec, RelocSec] : *SecRelocMapOrErr) {
    std::optional<const Elf_Shdr *> FunctionSec;
    if (IsRelocatable)
      FunctionSec =
          unwrapOrError(this->FileName, this->Obj.getSection(Sec->sh_link));
    ListScope L(W, "BBAddrMap");
    if (IsRelocatable && !RelocSec) {
      this->reportUniqueWarning("unable to get relocation section for " +
                                this->describe(*Sec));
      continue;
    }
    std::vector<PGOAnalysisMap> PGOAnalyses;
    Expected<std::vector<BBAddrMap>> BBAddrMapOrErr =
        this->Obj.decodeBBAddrMap(*Sec, RelocSec, &PGOAnalyses);
    if (!BBAddrMapOrErr) {
      this->reportUniqueWarning("unable to dump " + this->describe(*Sec) +
                                ": " + toString(BBAddrMapOrErr.takeError()));
      continue;
    }
    for (const auto &[AM, PAM] : zip_equal(*BBAddrMapOrErr, PGOAnalyses)) {
      DictScope D(W, "Function");
      W.printHex("At", AM.getFunctionAddress());
      SmallVector<uint32_t> FuncSymIndex =
          this->getSymbolIndexesForFunctionAddress(AM.getFunctionAddress(),
                                                   FunctionSec);
      std::string FuncName = "<?>";
      if (FuncSymIndex.empty())
        this->reportUniqueWarning(
            "could not identify function symbol for address (0x" +
            Twine::utohexstr(AM.getFunctionAddress()) + ") in " +
            this->describe(*Sec));
      else
        FuncName = this->getStaticSymbolName(FuncSymIndex.front());
      W.printString("Name", FuncName);
      {
        ListScope BBRL(W, "BB Ranges");
        for (const BBAddrMap::BBRangeEntry &BBR : AM.BBRanges) {
          DictScope BBRD(W);
          W.printHex("Base Address", BBR.BaseAddress);
          ListScope BBEL(W, "BB Entries");
          for (const BBAddrMap::BBEntry &BBE : BBR.BBEntries) {
            DictScope BBED(W);
            W.printNumber("ID", BBE.ID);
            W.printHex("Offset", BBE.Offset);
            W.printHex("Size", BBE.Size);
            W.printBoolean("HasReturn", BBE.hasReturn());
            W.printBoolean("HasTailCall", BBE.hasTailCall());
            W.printBoolean("IsEHPad", BBE.isEHPad());
            W.printBoolean("CanFallThrough", BBE.canFallThrough());
            W.printBoolean("HasIndirectBranch", BBE.hasIndirectBranch());
          }
        }
      }

      if (PAM.FeatEnable.hasPGOAnalysis()) {
        DictScope PD(W, "PGO analyses");

        if (PAM.FeatEnable.FuncEntryCount)
          W.printNumber("FuncEntryCount", PAM.FuncEntryCount);

        if (PAM.FeatEnable.hasPGOAnalysisBBData()) {
          ListScope L(W, "PGO BB entries");
          for (const PGOAnalysisMap::PGOBBEntry &PBBE : PAM.BBEntries) {
            DictScope L(W);

            if (PAM.FeatEnable.BBFreq) {
              if (PrettyPGOAnalysis) {
                std::string BlockFreqStr;
                raw_string_ostream SS(BlockFreqStr);
                printRelativeBlockFreq(SS, PAM.BBEntries.front().BlockFreq,
                                       PBBE.BlockFreq);
                W.printString("Frequency", BlockFreqStr);
              } else {
                W.printNumber("Frequency", PBBE.BlockFreq.getFrequency());
              }
            }

            if (PAM.FeatEnable.BrProb) {
              ListScope L(W, "Successors");
              for (const auto &Succ : PBBE.Successors) {
                DictScope L(W);
                W.printNumber("ID", Succ.ID);
                if (PrettyPGOAnalysis) {
                  W.printObject("Probability", Succ.Prob);
                } else {
                  W.printHex("Probability", Succ.Prob.getNumerator());
                }
              }
            }
          }
        }
      }
    }
  }
}

template <class ELFT> void LLVMELFDumper<ELFT>::printAddrsig() {
  ListScope L(W, "Addrsig");
  if (!this->DotAddrsigSec)
    return;

  Expected<std::vector<uint64_t>> SymsOrErr =
      decodeAddrsigSection(this->Obj, *this->DotAddrsigSec);
  if (!SymsOrErr) {
    this->reportUniqueWarning(SymsOrErr.takeError());
    return;
  }

  for (uint64_t Sym : *SymsOrErr)
    W.printNumber("Sym", this->getStaticSymbolName(Sym), Sym);
}

template <typename ELFT>
static bool printGNUNoteLLVMStyle(uint32_t NoteType, ArrayRef<uint8_t> Desc,
                                  ScopedPrinter &W) {
  // Return true if we were able to pretty-print the note, false otherwise.
  switch (NoteType) {
  default:
    return false;
  case ELF::NT_GNU_ABI_TAG: {
    const GNUAbiTag &AbiTag = getGNUAbiTag<ELFT>(Desc);
    if (!AbiTag.IsValid) {
      W.printString("ABI", "<corrupt GNU_ABI_TAG>");
      return false;
    } else {
      W.printString("OS", AbiTag.OSName);
      W.printString("ABI", AbiTag.ABI);
    }
    break;
  }
  case ELF::NT_GNU_BUILD_ID: {
    W.printString("Build ID", getGNUBuildId(Desc));
    break;
  }
  case ELF::NT_GNU_GOLD_VERSION:
    W.printString("Version", getDescAsStringRef(Desc));
    break;
  case ELF::NT_GNU_PROPERTY_TYPE_0:
    ListScope D(W, "Property");
    for (const std::string &Property : getGNUPropertyList<ELFT>(Desc))
      W.printString(Property);
    break;
  }
  return true;
}

static bool printAndroidNoteLLVMStyle(uint32_t NoteType, ArrayRef<uint8_t> Desc,
                                      ScopedPrinter &W) {
  // Return true if we were able to pretty-print the note, false otherwise.
  AndroidNoteProperties Props = getAndroidNoteProperties(NoteType, Desc);
  if (Props.empty())
    return false;
  for (const auto &KV : Props)
    W.printString(KV.first, KV.second);
  return true;
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printMemtag(
    const ArrayRef<std::pair<std::string, std::string>> DynamicEntries,
    const ArrayRef<uint8_t> AndroidNoteDesc,
    const ArrayRef<std::pair<uint64_t, uint64_t>> Descriptors) {
  {
    ListScope L(W, "Memtag Dynamic Entries:");
    if (DynamicEntries.empty())
      W.printString("< none found >");
    for (const auto &DynamicEntryKV : DynamicEntries)
      W.printString(DynamicEntryKV.first, DynamicEntryKV.second);
  }

  if (!AndroidNoteDesc.empty()) {
    ListScope L(W, "Memtag Android Note:");
    printAndroidNoteLLVMStyle(ELF::NT_ANDROID_TYPE_MEMTAG, AndroidNoteDesc, W);
  }

  if (Descriptors.empty())
    return;

  {
    ListScope L(W, "Memtag Global Descriptors:");
    for (const auto &[Addr, BytesToTag] : Descriptors) {
      W.printHex("0x" + utohexstr(Addr), BytesToTag);
    }
  }
}

template <typename ELFT>
static bool printLLVMOMPOFFLOADNoteLLVMStyle(uint32_t NoteType,
                                             ArrayRef<uint8_t> Desc,
                                             ScopedPrinter &W) {
  switch (NoteType) {
  default:
    return false;
  case ELF::NT_LLVM_OPENMP_OFFLOAD_VERSION:
    W.printString("Version", getDescAsStringRef(Desc));
    break;
  case ELF::NT_LLVM_OPENMP_OFFLOAD_PRODUCER:
    W.printString("Producer", getDescAsStringRef(Desc));
    break;
  case ELF::NT_LLVM_OPENMP_OFFLOAD_PRODUCER_VERSION:
    W.printString("Producer version", getDescAsStringRef(Desc));
    break;
  }
  return true;
}

static void printCoreNoteLLVMStyle(const CoreNote &Note, ScopedPrinter &W) {
  W.printNumber("Page Size", Note.PageSize);
  ListScope D(W, "Mappings");
  for (const CoreFileMapping &Mapping : Note.Mappings) {
    DictScope D(W);
    W.printHex("Start", Mapping.Start);
    W.printHex("End", Mapping.End);
    W.printHex("Offset", Mapping.Offset);
    W.printString("Filename", Mapping.Filename);
  }
}

template <class ELFT> void LLVMELFDumper<ELFT>::printNotes() {
  ListScope L(W, "NoteSections");

  std::unique_ptr<DictScope> NoteSectionScope;
  std::unique_ptr<ListScope> NotesScope;
  size_t Align = 0;
  auto StartNotes = [&](std::optional<StringRef> SecName,
                        const typename ELFT::Off Offset,
                        const typename ELFT::Addr Size, size_t Al) {
    Align = std::max<size_t>(Al, 4);
    NoteSectionScope = std::make_unique<DictScope>(W, "NoteSection");
    W.printString("Name", SecName ? *SecName : "<?>");
    W.printHex("Offset", Offset);
    W.printHex("Size", Size);
    NotesScope = std::make_unique<ListScope>(W, "Notes");
  };

  auto EndNotes = [&] {
    NotesScope.reset();
    NoteSectionScope.reset();
  };

  auto ProcessNote = [&](const Elf_Note &Note, bool IsCore) -> Error {
    DictScope D2(W);
    StringRef Name = Note.getName();
    ArrayRef<uint8_t> Descriptor = Note.getDesc(Align);
    Elf_Word Type = Note.getType();

    // Print the note owner/type.
    W.printString("Owner", Name);
    W.printHex("Data size", Descriptor.size());

    StringRef NoteType =
        getNoteTypeName<ELFT>(Note, this->Obj.getHeader().e_type);
    if (!NoteType.empty())
      W.printString("Type", NoteType);
    else
      W.printString("Type",
                    "Unknown (" + to_string(format_hex(Type, 10)) + ")");

    // Print the description, or fallback to printing raw bytes for unknown
    // owners/if we fail to pretty-print the contents.
    if (Name == "GNU") {
      if (printGNUNoteLLVMStyle<ELFT>(Type, Descriptor, W))
        return Error::success();
    } else if (Name == "FreeBSD") {
      if (std::optional<FreeBSDNote> N =
              getFreeBSDNote<ELFT>(Type, Descriptor, IsCore)) {
        W.printString(N->Type, N->Value);
        return Error::success();
      }
    } else if (Name == "AMD") {
      const AMDNote N = getAMDNote<ELFT>(Type, Descriptor);
      if (!N.Type.empty()) {
        W.printString(N.Type, N.Value);
        return Error::success();
      }
    } else if (Name == "AMDGPU") {
      const AMDGPUNote N = getAMDGPUNote<ELFT>(Type, Descriptor);
      if (!N.Type.empty()) {
        W.printString(N.Type, N.Value);
        return Error::success();
      }
    } else if (Name == "LLVMOMPOFFLOAD") {
      if (printLLVMOMPOFFLOADNoteLLVMStyle<ELFT>(Type, Descriptor, W))
        return Error::success();
    } else if (Name == "CORE") {
      if (Type == ELF::NT_FILE) {
        DataExtractor DescExtractor(
            Descriptor, ELFT::Endianness == llvm::endianness::little,
            sizeof(Elf_Addr));
        if (Expected<CoreNote> N = readCoreNote(DescExtractor)) {
          printCoreNoteLLVMStyle(*N, W);
          return Error::success();
        } else {
          return N.takeError();
        }
      }
    } else if (Name == "Android") {
      if (printAndroidNoteLLVMStyle(Type, Descriptor, W))
        return Error::success();
    }
    if (!Descriptor.empty()) {
      W.printBinaryBlock("Description data", Descriptor);
    }
    return Error::success();
  };

  processNotesHelper(*this, /*StartNotesFn=*/StartNotes,
                     /*ProcessNoteFn=*/ProcessNote, /*FinishNotesFn=*/EndNotes);
}

template <class ELFT> void LLVMELFDumper<ELFT>::printELFLinkerOptions() {
  ListScope L(W, "LinkerOptions");

  unsigned I = -1;
  for (const Elf_Shdr &Shdr : cantFail(this->Obj.sections())) {
    ++I;
    if (Shdr.sh_type != ELF::SHT_LLVM_LINKER_OPTIONS)
      continue;

    Expected<ArrayRef<uint8_t>> ContentsOrErr =
        this->Obj.getSectionContents(Shdr);
    if (!ContentsOrErr) {
      this->reportUniqueWarning("unable to read the content of the "
                                "SHT_LLVM_LINKER_OPTIONS section: " +
                                toString(ContentsOrErr.takeError()));
      continue;
    }
    if (ContentsOrErr->empty())
      continue;

    if (ContentsOrErr->back() != 0) {
      this->reportUniqueWarning("SHT_LLVM_LINKER_OPTIONS section at index " +
                                Twine(I) +
                                " is broken: the "
                                "content is not null-terminated");
      continue;
    }

    SmallVector<StringRef, 16> Strings;
    toStringRef(ContentsOrErr->drop_back()).split(Strings, '\0');
    if (Strings.size() % 2 != 0) {
      this->reportUniqueWarning(
          "SHT_LLVM_LINKER_OPTIONS section at index " + Twine(I) +
          " is broken: an incomplete "
          "key-value pair was found. The last possible key was: \"" +
          Strings.back() + "\"");
      continue;
    }

    for (size_t I = 0; I < Strings.size(); I += 2)
      W.printString(Strings[I], Strings[I + 1]);
  }
}

template <class ELFT> void LLVMELFDumper<ELFT>::printDependentLibs() {
  ListScope L(W, "DependentLibs");
  this->printDependentLibsHelper(
      [](const Elf_Shdr &) {},
      [this](StringRef Lib, uint64_t) { W.printString(Lib); });
}

template <class ELFT> void LLVMELFDumper<ELFT>::printStackSizes() {
  ListScope L(W, "StackSizes");
  if (this->Obj.getHeader().e_type == ELF::ET_REL)
    this->printRelocatableStackSizes([]() {});
  else
    this->printNonRelocatableStackSizes([]() {});
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printStackSizeEntry(uint64_t Size,
                                              ArrayRef<std::string> FuncNames) {
  DictScope D(W, "Entry");
  W.printList("Functions", FuncNames);
  W.printHex("Size", Size);
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printMipsGOT(const MipsGOTParser<ELFT> &Parser) {
  auto PrintEntry = [&](const Elf_Addr *E) {
    W.printHex("Address", Parser.getGotAddress(E));
    W.printNumber("Access", Parser.getGotOffset(E));
    W.printHex("Initial", *E);
  };

  DictScope GS(W, Parser.IsStatic ? "Static GOT" : "Primary GOT");

  W.printHex("Canonical gp value", Parser.getGp());
  {
    ListScope RS(W, "Reserved entries");
    {
      DictScope D(W, "Entry");
      PrintEntry(Parser.getGotLazyResolver());
      W.printString("Purpose", StringRef("Lazy resolver"));
    }

    if (Parser.getGotModulePointer()) {
      DictScope D(W, "Entry");
      PrintEntry(Parser.getGotModulePointer());
      W.printString("Purpose", StringRef("Module pointer (GNU extension)"));
    }
  }
  {
    ListScope LS(W, "Local entries");
    for (auto &E : Parser.getLocalEntries()) {
      DictScope D(W, "Entry");
      PrintEntry(&E);
    }
  }

  if (Parser.IsStatic)
    return;

  {
    ListScope GS(W, "Global entries");
    for (auto &E : Parser.getGlobalEntries()) {
      DictScope D(W, "Entry");

      PrintEntry(&E);

      const Elf_Sym &Sym = *Parser.getGotSym(&E);
      W.printHex("Value", Sym.st_value);
      W.printEnum("Type", Sym.getType(), ArrayRef(ElfSymbolTypes));

      const unsigned SymIndex = &Sym - this->dynamic_symbols().begin();
      DataRegion<Elf_Word> ShndxTable(
          (const Elf_Word *)this->DynSymTabShndxRegion.Addr, this->Obj.end());
      printSymbolSection(Sym, SymIndex, ShndxTable);

      std::string SymName = this->getFullSymbolName(
          Sym, SymIndex, ShndxTable, this->DynamicStringTable, true);
      W.printNumber("Name", SymName, Sym.st_name);
    }
  }

  W.printNumber("Number of TLS and multi-GOT entries",
                uint64_t(Parser.getOtherEntries().size()));
}

template <class ELFT>
void LLVMELFDumper<ELFT>::printMipsPLT(const MipsGOTParser<ELFT> &Parser) {
  auto PrintEntry = [&](const Elf_Addr *E) {
    W.printHex("Address", Parser.getPltAddress(E));
    W.printHex("Initial", *E);
  };

  DictScope GS(W, "PLT GOT");

  {
    ListScope RS(W, "Reserved entries");
    {
      DictScope D(W, "Entry");
      PrintEntry(Parser.getPltLazyResolver());
      W.printString("Purpose", StringRef("PLT lazy resolver"));
    }

    if (auto E = Parser.getPltModulePointer()) {
      DictScope D(W, "Entry");
      PrintEntry(E);
      W.printString("Purpose", StringRef("Module pointer"));
    }
  }
  {
    ListScope LS(W, "Entries");
    DataRegion<Elf_Word> ShndxTable(
        (const Elf_Word *)this->DynSymTabShndxRegion.Addr, this->Obj.end());
    for (auto &E : Parser.getPltEntries()) {
      DictScope D(W, "Entry");
      PrintEntry(&E);

      const Elf_Sym &Sym = *Parser.getPltSym(&E);
      W.printHex("Value", Sym.st_value);
      W.printEnum("Type", Sym.getType(), ArrayRef(ElfSymbolTypes));
      printSymbolSection(Sym, &Sym - this->dynamic_symbols().begin(),
                         ShndxTable);

      const Elf_Sym *FirstSym = cantFail(
          this->Obj.template getEntry<Elf_Sym>(*Parser.getPltSymTable(), 0));
      std::string SymName = this->getFullSymbolName(
          Sym, &Sym - FirstSym, ShndxTable, Parser.getPltStrTable(), true);
      W.printNumber("Name", SymName, Sym.st_name);
    }
  }
}

template <class ELFT> void LLVMELFDumper<ELFT>::printMipsABIFlags() {
  const Elf_Mips_ABIFlags<ELFT> *Flags;
  if (Expected<const Elf_Mips_ABIFlags<ELFT> *> SecOrErr =
          getMipsAbiFlagsSection(*this)) {
    Flags = *SecOrErr;
    if (!Flags) {
      W.startLine() << "There is no .MIPS.abiflags section in the file.\n";
      return;
    }
  } else {
    this->reportUniqueWarning(SecOrErr.takeError());
    return;
  }

  raw_ostream &OS = W.getOStream();
  DictScope GS(W, "MIPS ABI Flags");

  W.printNumber("Version", Flags->version);
  W.startLine() << "ISA: ";
  if (Flags->isa_rev <= 1)
    OS << format("MIPS%u", Flags->isa_level);
  else
    OS << format("MIPS%ur%u", Flags->isa_level, Flags->isa_rev);
  OS << "\n";
  W.printEnum("ISA Extension", Flags->isa_ext, ArrayRef(ElfMipsISAExtType));
  W.printFlags("ASEs", Flags->ases, ArrayRef(ElfMipsASEFlags));
  W.printEnum("FP ABI", Flags->fp_abi, ArrayRef(ElfMipsFpABIType));
  W.printNumber("GPR size", getMipsRegisterSize(Flags->gpr_size));
  W.printNumber("CPR1 size", getMipsRegisterSize(Flags->cpr1_size));
  W.printNumber("CPR2 size", getMipsRegisterSize(Flags->cpr2_size));
  W.printFlags("Flags 1", Flags->flags1, ArrayRef(ElfMipsFlags1));
  W.printHex("Flags 2", Flags->flags2);
}

template <class ELFT>
void JSONELFDumper<ELFT>::printFileSummary(StringRef FileStr, ObjectFile &Obj,
                                           ArrayRef<std::string> InputFilenames,
                                           const Archive *A) {
  FileScope = std::make_unique<DictScope>(this->W);
  DictScope D(this->W, "FileSummary");
  this->W.printString("File", FileStr);
  this->W.printString("Format", Obj.getFileFormatName());
  this->W.printString("Arch", Triple::getArchTypeName(Obj.getArch()));
  this->W.printString(
      "AddressSize",
      std::string(formatv("{0}bit", 8 * Obj.getBytesInAddress())));
  this->printLoadName();
}

template <class ELFT>
void JSONELFDumper<ELFT>::printZeroSymbolOtherField(
    const Elf_Sym &Symbol) const {
  // We want the JSON format to be uniform, since it is machine readable, so
  // always print the `Other` field the same way.
  this->printSymbolOtherField(Symbol);
}

template <class ELFT>
void JSONELFDumper<ELFT>::printDefaultRelRelaReloc(const Relocation<ELFT> &R,
                                                   StringRef SymbolName,
                                                   StringRef RelocName) {
  this->printExpandedRelRelaReloc(R, SymbolName, RelocName);
}

template <class ELFT>
void JSONELFDumper<ELFT>::printRelocationSectionInfo(const Elf_Shdr &Sec,
                                                     StringRef Name,
                                                     const unsigned SecNdx) {
  DictScope Group(this->W);
  this->W.printNumber("SectionIndex", SecNdx);
  ListScope D(this->W, "Relocs");
  this->printRelocationsHelper(Sec);
}

template <class ELFT>
std::string JSONELFDumper<ELFT>::getGroupSectionHeaderName() const {
  return "GroupSections";
}

template <class ELFT>
void JSONELFDumper<ELFT>::printSectionGroupMembers(StringRef Name,
                                                   uint64_t Idx) const {
  DictScope Grp(this->W);
  this->W.printString("Name", Name);
  this->W.printNumber("Index", Idx);
}

template <class ELFT> void JSONELFDumper<ELFT>::printEmptyGroupMessage() const {
  // JSON output does not need to print anything for empty groups
}
