//===- ELF.h - ELF object file implementation -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ELFFile template class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ELF_H
#define LLVM_OBJECT_ELF_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace llvm {
namespace object {

struct VerdAux {
  unsigned Offset;
  std::string Name;
};

struct VerDef {
  unsigned Offset;
  unsigned Version;
  unsigned Flags;
  unsigned Ndx;
  unsigned Cnt;
  unsigned Hash;
  std::string Name;
  std::vector<VerdAux> AuxV;
};

struct VernAux {
  unsigned Hash;
  unsigned Flags;
  unsigned Other;
  unsigned Offset;
  std::string Name;
};

struct VerNeed {
  unsigned Version;
  unsigned Cnt;
  unsigned Offset;
  std::string File;
  std::vector<VernAux> AuxV;
};

struct VersionEntry {
  std::string Name;
  bool IsVerDef;
};

StringRef getELFRelocationTypeName(uint32_t Machine, uint32_t Type);
uint32_t getELFRelativeRelocationType(uint32_t Machine);
StringRef getELFSectionTypeName(uint32_t Machine, uint32_t Type);

// Subclasses of ELFFile may need this for template instantiation
inline std::pair<unsigned char, unsigned char>
getElfArchType(StringRef Object) {
  if (Object.size() < ELF::EI_NIDENT)
    return std::make_pair((uint8_t)ELF::ELFCLASSNONE,
                          (uint8_t)ELF::ELFDATANONE);
  return std::make_pair((uint8_t)Object[ELF::EI_CLASS],
                        (uint8_t)Object[ELF::EI_DATA]);
}

enum PPCInstrMasks : uint64_t {
  PADDI_R12_NO_DISP = 0x0610000039800000,
  ADDIS_R12_TO_R2_NO_DISP = 0x3D820000,
  ADDI_R12_TO_R2_NO_DISP = 0x39820000,
  ADDI_R12_TO_R12_NO_DISP = 0x398C0000,
  PLD_R12_NO_DISP = 0x04100000E5800000,
  MTCTR_R12 = 0x7D8903A6,
  BCTR = 0x4E800420,
};

template <class ELFT> class ELFFile;

template <class T> struct DataRegion {
  // This constructor is used when we know the start and the size of a data
  // region. We assume that Arr does not go past the end of the file.
  DataRegion(ArrayRef<T> Arr) : First(Arr.data()), Size(Arr.size()) {}

  // Sometimes we only know the start of a data region. We still don't want to
  // read past the end of the file, so we provide the end of a buffer.
  DataRegion(const T *Data, const uint8_t *BufferEnd)
      : First(Data), BufEnd(BufferEnd) {}

  Expected<T> operator[](uint64_t N) {
    assert(Size || BufEnd);
    if (Size) {
      if (N >= *Size)
        return createError(
            "the index is greater than or equal to the number of entries (" +
            Twine(*Size) + ")");
    } else {
      const uint8_t *EntryStart = (const uint8_t *)First + N * sizeof(T);
      if (EntryStart + sizeof(T) > BufEnd)
        return createError("can't read past the end of the file");
    }
    return *(First + N);
  }

  const T *First;
  std::optional<uint64_t> Size;
  const uint8_t *BufEnd = nullptr;
};

template <class ELFT>
std::string getSecIndexForError(const ELFFile<ELFT> &Obj,
                                const typename ELFT::Shdr &Sec) {
  auto TableOrErr = Obj.sections();
  if (TableOrErr)
    return "[index " + std::to_string(&Sec - &TableOrErr->front()) + "]";
  // To make this helper be more convenient for error reporting purposes we
  // drop the error. But really it should never be triggered. Before this point,
  // our code should have called 'sections()' and reported a proper error on
  // failure.
  llvm::consumeError(TableOrErr.takeError());
  return "[unknown index]";
}

template <class ELFT>
static std::string describe(const ELFFile<ELFT> &Obj,
                            const typename ELFT::Shdr &Sec) {
  unsigned SecNdx = &Sec - &cantFail(Obj.sections()).front();
  return (object::getELFSectionTypeName(Obj.getHeader().e_machine,
                                        Sec.sh_type) +
          " section with index " + Twine(SecNdx))
      .str();
}

template <class ELFT>
std::string getPhdrIndexForError(const ELFFile<ELFT> &Obj,
                                 const typename ELFT::Phdr &Phdr) {
  auto Headers = Obj.program_headers();
  if (Headers)
    return ("[index " + Twine(&Phdr - &Headers->front()) + "]").str();
  // See comment in the getSecIndexForError() above.
  llvm::consumeError(Headers.takeError());
  return "[unknown index]";
}

static inline Error defaultWarningHandler(const Twine &Msg) {
  return createError(Msg);
}

template <class ELFT>
bool checkSectionOffsets(const typename ELFT::Phdr &Phdr,
                         const typename ELFT::Shdr &Sec) {
  // SHT_NOBITS sections don't need to have an offset inside the segment.
  if (Sec.sh_type == ELF::SHT_NOBITS)
    return true;

  if (Sec.sh_offset < Phdr.p_offset)
    return false;

  // Only non-empty sections can be at the end of a segment.
  if (Sec.sh_size == 0)
    return (Sec.sh_offset + 1 <= Phdr.p_offset + Phdr.p_filesz);
  return Sec.sh_offset + Sec.sh_size <= Phdr.p_offset + Phdr.p_filesz;
}

// Check that an allocatable section belongs to a virtual address
// space of a segment.
template <class ELFT>
bool checkSectionVMA(const typename ELFT::Phdr &Phdr,
                     const typename ELFT::Shdr &Sec) {
  if (!(Sec.sh_flags & ELF::SHF_ALLOC))
    return true;

  if (Sec.sh_addr < Phdr.p_vaddr)
    return false;

  bool IsTbss =
      (Sec.sh_type == ELF::SHT_NOBITS) && ((Sec.sh_flags & ELF::SHF_TLS) != 0);
  // .tbss is special, it only has memory in PT_TLS and has NOBITS properties.
  bool IsTbssInNonTLS = IsTbss && Phdr.p_type != ELF::PT_TLS;
  // Only non-empty sections can be at the end of a segment.
  if (Sec.sh_size == 0 || IsTbssInNonTLS)
    return Sec.sh_addr + 1 <= Phdr.p_vaddr + Phdr.p_memsz;
  return Sec.sh_addr + Sec.sh_size <= Phdr.p_vaddr + Phdr.p_memsz;
}

template <class ELFT>
bool isSectionInSegment(const typename ELFT::Phdr &Phdr,
                        const typename ELFT::Shdr &Sec) {
  return checkSectionOffsets<ELFT>(Phdr, Sec) &&
         checkSectionVMA<ELFT>(Phdr, Sec);
}

// HdrHandler is called once with the number of relocations and whether the
// relocations have addends. EntryHandler is called once per decoded relocation.
template <bool Is64>
Error decodeCrel(
    ArrayRef<uint8_t> Content,
    function_ref<void(uint64_t /*relocation count*/, bool /*explicit addends*/)>
        HdrHandler,
    function_ref<void(Elf_Crel_Impl<Is64>)> EntryHandler) {
  DataExtractor Data(Content, true, 8); // endian and address size are unused
  DataExtractor::Cursor Cur(0);
  const uint64_t Hdr = Data.getULEB128(Cur);
  size_t Count = Hdr / 8;
  const size_t FlagBits = Hdr & ELF::CREL_HDR_ADDEND ? 3 : 2;
  const size_t Shift = Hdr % ELF::CREL_HDR_ADDEND;
  using uint = typename Elf_Crel_Impl<Is64>::uint;
  uint Offset = 0, Addend = 0;
  HdrHandler(Count, Hdr & ELF::CREL_HDR_ADDEND);
  uint32_t SymIdx = 0, Type = 0;
  for (; Count; --Count) {
    // The delta offset and flags member may be larger than uint64_t. Special
    // case the first byte (2 or 3 flag bits; the rest are offset bits). Other
    // ULEB128 bytes encode the remaining delta offset bits.
    const uint8_t B = Data.getU8(Cur);
    Offset += B >> FlagBits;
    if (B >= 0x80)
      Offset += (Data.getULEB128(Cur) << (7 - FlagBits)) - (0x80 >> FlagBits);
    // Delta symidx/type/addend members (SLEB128).
    if (B & 1)
      SymIdx += Data.getSLEB128(Cur);
    if (B & 2)
      Type += Data.getSLEB128(Cur);
    if (B & 4 & Hdr)
      Addend += Data.getSLEB128(Cur);
    if (!Cur)
      break;
    EntryHandler(
        {Offset << Shift, SymIdx, Type, std::make_signed_t<uint>(Addend)});
  }
  return Cur.takeError();
}

template <class ELFT>
class ELFFile {
public:
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

  // This is a callback that can be passed to a number of functions.
  // It can be used to ignore non-critical errors (warnings), which is
  // useful for dumpers, like llvm-readobj.
  // It accepts a warning message string and returns a success
  // when the warning should be ignored or an error otherwise.
  using WarningHandler = llvm::function_ref<Error(const Twine &Msg)>;

  const uint8_t *base() const { return Buf.bytes_begin(); }
  const uint8_t *end() const { return base() + getBufSize(); }

  size_t getBufSize() const { return Buf.size(); }

private:
  StringRef Buf;
  std::vector<Elf_Shdr> FakeSections;
  SmallString<0> FakeSectionStrings;

  ELFFile(StringRef Object);

public:
  const Elf_Ehdr &getHeader() const {
    return *reinterpret_cast<const Elf_Ehdr *>(base());
  }

  template <typename T>
  Expected<const T *> getEntry(uint32_t Section, uint32_t Entry) const;
  template <typename T>
  Expected<const T *> getEntry(const Elf_Shdr &Section, uint32_t Entry) const;

  Expected<std::vector<VerDef>>
  getVersionDefinitions(const Elf_Shdr &Sec) const;
  Expected<std::vector<VerNeed>> getVersionDependencies(
      const Elf_Shdr &Sec,
      WarningHandler WarnHandler = &defaultWarningHandler) const;
  Expected<StringRef> getSymbolVersionByIndex(
      uint32_t SymbolVersionIndex, bool &IsDefault,
      SmallVector<std::optional<VersionEntry>, 0> &VersionMap,
      std::optional<bool> IsSymHidden) const;

  Expected<StringRef>
  getStringTable(const Elf_Shdr &Section,
                 WarningHandler WarnHandler = &defaultWarningHandler) const;
  Expected<StringRef> getStringTableForSymtab(const Elf_Shdr &Section) const;
  Expected<StringRef> getStringTableForSymtab(const Elf_Shdr &Section,
                                              Elf_Shdr_Range Sections) const;
  Expected<StringRef> getLinkAsStrtab(const typename ELFT::Shdr &Sec) const;

  Expected<ArrayRef<Elf_Word>> getSHNDXTable(const Elf_Shdr &Section) const;
  Expected<ArrayRef<Elf_Word>> getSHNDXTable(const Elf_Shdr &Section,
                                             Elf_Shdr_Range Sections) const;

  Expected<uint64_t> getDynSymtabSize() const;

  StringRef getRelocationTypeName(uint32_t Type) const;
  void getRelocationTypeName(uint32_t Type,
                             SmallVectorImpl<char> &Result) const;
  uint32_t getRelativeRelocationType() const;

  std::string getDynamicTagAsString(unsigned Arch, uint64_t Type) const;
  std::string getDynamicTagAsString(uint64_t Type) const;

  /// Get the symbol for a given relocation.
  Expected<const Elf_Sym *> getRelocationSymbol(const Elf_Rel &Rel,
                                                const Elf_Shdr *SymTab) const;

  Expected<SmallVector<std::optional<VersionEntry>, 0>>
  loadVersionMap(const Elf_Shdr *VerNeedSec, const Elf_Shdr *VerDefSec) const;

  static Expected<ELFFile> create(StringRef Object);

  bool isLE() const {
    return getHeader().getDataEncoding() == ELF::ELFDATA2LSB;
  }

  bool isMipsELF64() const {
    return getHeader().e_machine == ELF::EM_MIPS &&
           getHeader().getFileClass() == ELF::ELFCLASS64;
  }

  bool isMips64EL() const { return isMipsELF64() && isLE(); }

  Expected<Elf_Shdr_Range> sections() const;

  Expected<Elf_Dyn_Range> dynamicEntries() const;

  Expected<const uint8_t *>
  toMappedAddr(uint64_t VAddr,
               WarningHandler WarnHandler = &defaultWarningHandler) const;

  Expected<Elf_Sym_Range> symbols(const Elf_Shdr *Sec) const {
    if (!Sec)
      return ArrayRef<Elf_Sym>(nullptr, nullptr);
    return getSectionContentsAsArray<Elf_Sym>(*Sec);
  }

  Expected<Elf_Rela_Range> relas(const Elf_Shdr &Sec) const {
    return getSectionContentsAsArray<Elf_Rela>(Sec);
  }

  Expected<Elf_Rel_Range> rels(const Elf_Shdr &Sec) const {
    return getSectionContentsAsArray<Elf_Rel>(Sec);
  }

  Expected<Elf_Relr_Range> relrs(const Elf_Shdr &Sec) const {
    return getSectionContentsAsArray<Elf_Relr>(Sec);
  }

  std::vector<Elf_Rel> decode_relrs(Elf_Relr_Range relrs) const;

  Expected<uint64_t> getCrelHeader(ArrayRef<uint8_t> Content) const;
  using RelsOrRelas = std::pair<std::vector<Elf_Rel>, std::vector<Elf_Rela>>;
  Expected<RelsOrRelas> decodeCrel(ArrayRef<uint8_t> Content) const;
  Expected<RelsOrRelas> crels(const Elf_Shdr &Sec) const;

  Expected<std::vector<Elf_Rela>> android_relas(const Elf_Shdr &Sec) const;

  /// Iterate over program header table.
  Expected<Elf_Phdr_Range> program_headers() const {
    if (getHeader().e_phnum && getHeader().e_phentsize != sizeof(Elf_Phdr))
      return createError("invalid e_phentsize: " +
                         Twine(getHeader().e_phentsize));

    uint64_t HeadersSize =
        (uint64_t)getHeader().e_phnum * getHeader().e_phentsize;
    uint64_t PhOff = getHeader().e_phoff;
    if (PhOff + HeadersSize < PhOff || PhOff + HeadersSize > getBufSize())
      return createError("program headers are longer than binary of size " +
                         Twine(getBufSize()) + ": e_phoff = 0x" +
                         Twine::utohexstr(getHeader().e_phoff) +
                         ", e_phnum = " + Twine(getHeader().e_phnum) +
                         ", e_phentsize = " + Twine(getHeader().e_phentsize));

    auto *Begin = reinterpret_cast<const Elf_Phdr *>(base() + PhOff);
    return ArrayRef(Begin, Begin + getHeader().e_phnum);
  }

  /// Get an iterator over notes in a program header.
  ///
  /// The program header must be of type \c PT_NOTE.
  ///
  /// \param Phdr the program header to iterate over.
  /// \param Err [out] an error to support fallible iteration, which should
  ///  be checked after iteration ends.
  Elf_Note_Iterator notes_begin(const Elf_Phdr &Phdr, Error &Err) const {
    assert(Phdr.p_type == ELF::PT_NOTE && "Phdr is not of type PT_NOTE");
    ErrorAsOutParameter ErrAsOutParam(&Err);
    if (Phdr.p_offset + Phdr.p_filesz > getBufSize()) {
      Err =
          createError("invalid offset (0x" + Twine::utohexstr(Phdr.p_offset) +
                      ") or size (0x" + Twine::utohexstr(Phdr.p_filesz) + ")");
      return Elf_Note_Iterator(Err);
    }
    // Allow 4, 8, and (for Linux core dumps) 0.
    // TODO: Disallow 1 after all tests are fixed.
    if (Phdr.p_align != 0 && Phdr.p_align != 1 && Phdr.p_align != 4 &&
        Phdr.p_align != 8) {
      Err =
          createError("alignment (" + Twine(Phdr.p_align) + ") is not 4 or 8");
      return Elf_Note_Iterator(Err);
    }
    return Elf_Note_Iterator(base() + Phdr.p_offset, Phdr.p_filesz,
                             std::max<size_t>(Phdr.p_align, 4), Err);
  }

  /// Get an iterator over notes in a section.
  ///
  /// The section must be of type \c SHT_NOTE.
  ///
  /// \param Shdr the section to iterate over.
  /// \param Err [out] an error to support fallible iteration, which should
  ///  be checked after iteration ends.
  Elf_Note_Iterator notes_begin(const Elf_Shdr &Shdr, Error &Err) const {
    assert(Shdr.sh_type == ELF::SHT_NOTE && "Shdr is not of type SHT_NOTE");
    ErrorAsOutParameter ErrAsOutParam(&Err);
    if (Shdr.sh_offset + Shdr.sh_size > getBufSize()) {
      Err =
          createError("invalid offset (0x" + Twine::utohexstr(Shdr.sh_offset) +
                      ") or size (0x" + Twine::utohexstr(Shdr.sh_size) + ")");
      return Elf_Note_Iterator(Err);
    }
    // TODO: Allow just 4 and 8 after all tests are fixed.
    if (Shdr.sh_addralign != 0 && Shdr.sh_addralign != 1 &&
        Shdr.sh_addralign != 4 && Shdr.sh_addralign != 8) {
      Err = createError("alignment (" + Twine(Shdr.sh_addralign) +
                        ") is not 4 or 8");
      return Elf_Note_Iterator(Err);
    }
    return Elf_Note_Iterator(base() + Shdr.sh_offset, Shdr.sh_size,
                             std::max<size_t>(Shdr.sh_addralign, 4), Err);
  }

  /// Get the end iterator for notes.
  Elf_Note_Iterator notes_end() const {
    return Elf_Note_Iterator();
  }

  /// Get an iterator range over notes of a program header.
  ///
  /// The program header must be of type \c PT_NOTE.
  ///
  /// \param Phdr the program header to iterate over.
  /// \param Err [out] an error to support fallible iteration, which should
  ///  be checked after iteration ends.
  iterator_range<Elf_Note_Iterator> notes(const Elf_Phdr &Phdr,
                                          Error &Err) const {
    return make_range(notes_begin(Phdr, Err), notes_end());
  }

  /// Get an iterator range over notes of a section.
  ///
  /// The section must be of type \c SHT_NOTE.
  ///
  /// \param Shdr the section to iterate over.
  /// \param Err [out] an error to support fallible iteration, which should
  ///  be checked after iteration ends.
  iterator_range<Elf_Note_Iterator> notes(const Elf_Shdr &Shdr,
                                          Error &Err) const {
    return make_range(notes_begin(Shdr, Err), notes_end());
  }

  Expected<StringRef> getSectionStringTable(
      Elf_Shdr_Range Sections,
      WarningHandler WarnHandler = &defaultWarningHandler) const;
  Expected<uint32_t> getSectionIndex(const Elf_Sym &Sym, Elf_Sym_Range Syms,
                                     DataRegion<Elf_Word> ShndxTable) const;
  Expected<const Elf_Shdr *> getSection(const Elf_Sym &Sym,
                                        const Elf_Shdr *SymTab,
                                        DataRegion<Elf_Word> ShndxTable) const;
  Expected<const Elf_Shdr *> getSection(const Elf_Sym &Sym,
                                        Elf_Sym_Range Symtab,
                                        DataRegion<Elf_Word> ShndxTable) const;
  Expected<const Elf_Shdr *> getSection(uint32_t Index) const;

  Expected<const Elf_Sym *> getSymbol(const Elf_Shdr *Sec,
                                      uint32_t Index) const;

  Expected<StringRef>
  getSectionName(const Elf_Shdr &Section,
                 WarningHandler WarnHandler = &defaultWarningHandler) const;
  Expected<StringRef> getSectionName(const Elf_Shdr &Section,
                                     StringRef DotShstrtab) const;
  template <typename T>
  Expected<ArrayRef<T>> getSectionContentsAsArray(const Elf_Shdr &Sec) const;
  Expected<ArrayRef<uint8_t>> getSectionContents(const Elf_Shdr &Sec) const;
  Expected<ArrayRef<uint8_t>> getSegmentContents(const Elf_Phdr &Phdr) const;

  /// Returns a vector of BBAddrMap structs corresponding to each function
  /// within the text section that the SHT_LLVM_BB_ADDR_MAP section \p Sec
  /// is associated with. If the current ELFFile is relocatable, a corresponding
  /// \p RelaSec must be passed in as an argument.
  /// Optional out variable to collect all PGO Analyses. New elements are only
  /// added if no error occurs. If not provided, the PGO Analyses are decoded
  /// then ignored.
  Expected<std::vector<BBAddrMap>>
  decodeBBAddrMap(const Elf_Shdr &Sec, const Elf_Shdr *RelaSec = nullptr,
                  std::vector<PGOAnalysisMap> *PGOAnalyses = nullptr) const;

  /// Returns a map from every section matching \p IsMatch to its relocation
  /// section, or \p nullptr if it has no relocation section. This function
  /// returns an error if any of the \p IsMatch calls fail or if it fails to
  /// retrieve the content section of any relocation section.
  Expected<MapVector<const Elf_Shdr *, const Elf_Shdr *>>
  getSectionAndRelocations(
      std::function<Expected<bool>(const Elf_Shdr &)> IsMatch) const;

  void createFakeSections();
};

using ELF32LEFile = ELFFile<ELF32LE>;
using ELF64LEFile = ELFFile<ELF64LE>;
using ELF32BEFile = ELFFile<ELF32BE>;
using ELF64BEFile = ELFFile<ELF64BE>;

template <class ELFT>
inline Expected<const typename ELFT::Shdr *>
getSection(typename ELFT::ShdrRange Sections, uint32_t Index) {
  if (Index >= Sections.size())
    return createError("invalid section index: " + Twine(Index));
  return &Sections[Index];
}

template <class ELFT>
inline Expected<uint32_t>
getExtendedSymbolTableIndex(const typename ELFT::Sym &Sym, unsigned SymIndex,
                            DataRegion<typename ELFT::Word> ShndxTable) {
  assert(Sym.st_shndx == ELF::SHN_XINDEX);
  if (!ShndxTable.First)
    return createError(
        "found an extended symbol index (" + Twine(SymIndex) +
        "), but unable to locate the extended symbol index table");

  Expected<typename ELFT::Word> TableOrErr = ShndxTable[SymIndex];
  if (!TableOrErr)
    return createError("unable to read an extended symbol table at index " +
                       Twine(SymIndex) + ": " +
                       toString(TableOrErr.takeError()));
  return *TableOrErr;
}

template <class ELFT>
Expected<uint32_t>
ELFFile<ELFT>::getSectionIndex(const Elf_Sym &Sym, Elf_Sym_Range Syms,
                               DataRegion<Elf_Word> ShndxTable) const {
  uint32_t Index = Sym.st_shndx;
  if (Index == ELF::SHN_XINDEX) {
    Expected<uint32_t> ErrorOrIndex =
        getExtendedSymbolTableIndex<ELFT>(Sym, &Sym - Syms.begin(), ShndxTable);
    if (!ErrorOrIndex)
      return ErrorOrIndex.takeError();
    return *ErrorOrIndex;
  }
  if (Index == ELF::SHN_UNDEF || Index >= ELF::SHN_LORESERVE)
    return 0;
  return Index;
}

template <class ELFT>
Expected<const typename ELFT::Shdr *>
ELFFile<ELFT>::getSection(const Elf_Sym &Sym, const Elf_Shdr *SymTab,
                          DataRegion<Elf_Word> ShndxTable) const {
  auto SymsOrErr = symbols(SymTab);
  if (!SymsOrErr)
    return SymsOrErr.takeError();
  return getSection(Sym, *SymsOrErr, ShndxTable);
}

template <class ELFT>
Expected<const typename ELFT::Shdr *>
ELFFile<ELFT>::getSection(const Elf_Sym &Sym, Elf_Sym_Range Symbols,
                          DataRegion<Elf_Word> ShndxTable) const {
  auto IndexOrErr = getSectionIndex(Sym, Symbols, ShndxTable);
  if (!IndexOrErr)
    return IndexOrErr.takeError();
  uint32_t Index = *IndexOrErr;
  if (Index == 0)
    return nullptr;
  return getSection(Index);
}

template <class ELFT>
Expected<const typename ELFT::Sym *>
ELFFile<ELFT>::getSymbol(const Elf_Shdr *Sec, uint32_t Index) const {
  auto SymsOrErr = symbols(Sec);
  if (!SymsOrErr)
    return SymsOrErr.takeError();

  Elf_Sym_Range Symbols = *SymsOrErr;
  if (Index >= Symbols.size())
    return createError("unable to get symbol from section " +
                       getSecIndexForError(*this, *Sec) +
                       ": invalid symbol index (" + Twine(Index) + ")");
  return &Symbols[Index];
}

template <class ELFT>
template <typename T>
Expected<ArrayRef<T>>
ELFFile<ELFT>::getSectionContentsAsArray(const Elf_Shdr &Sec) const {
  if (Sec.sh_entsize != sizeof(T) && sizeof(T) != 1)
    return createError("section " + getSecIndexForError(*this, Sec) +
                       " has invalid sh_entsize: expected " + Twine(sizeof(T)) +
                       ", but got " + Twine(Sec.sh_entsize));

  uintX_t Offset = Sec.sh_offset;
  uintX_t Size = Sec.sh_size;

  if (Size % sizeof(T))
    return createError("section " + getSecIndexForError(*this, Sec) +
                       " has an invalid sh_size (" + Twine(Size) +
                       ") which is not a multiple of its sh_entsize (" +
                       Twine(Sec.sh_entsize) + ")");
  if (std::numeric_limits<uintX_t>::max() - Offset < Size)
    return createError("section " + getSecIndexForError(*this, Sec) +
                       " has a sh_offset (0x" + Twine::utohexstr(Offset) +
                       ") + sh_size (0x" + Twine::utohexstr(Size) +
                       ") that cannot be represented");
  if (Offset + Size > Buf.size())
    return createError("section " + getSecIndexForError(*this, Sec) +
                       " has a sh_offset (0x" + Twine::utohexstr(Offset) +
                       ") + sh_size (0x" + Twine::utohexstr(Size) +
                       ") that is greater than the file size (0x" +
                       Twine::utohexstr(Buf.size()) + ")");

  if (Offset % alignof(T))
    // TODO: this error is untested.
    return createError("unaligned data");

  const T *Start = reinterpret_cast<const T *>(base() + Offset);
  return ArrayRef(Start, Size / sizeof(T));
}

template <class ELFT>
Expected<ArrayRef<uint8_t>>
ELFFile<ELFT>::getSegmentContents(const Elf_Phdr &Phdr) const {
  uintX_t Offset = Phdr.p_offset;
  uintX_t Size = Phdr.p_filesz;

  if (std::numeric_limits<uintX_t>::max() - Offset < Size)
    return createError("program header " + getPhdrIndexForError(*this, Phdr) +
                       " has a p_offset (0x" + Twine::utohexstr(Offset) +
                       ") + p_filesz (0x" + Twine::utohexstr(Size) +
                       ") that cannot be represented");
  if (Offset + Size > Buf.size())
    return createError("program header  " + getPhdrIndexForError(*this, Phdr) +
                       " has a p_offset (0x" + Twine::utohexstr(Offset) +
                       ") + p_filesz (0x" + Twine::utohexstr(Size) +
                       ") that is greater than the file size (0x" +
                       Twine::utohexstr(Buf.size()) + ")");
  return ArrayRef(base() + Offset, Size);
}

template <class ELFT>
Expected<ArrayRef<uint8_t>>
ELFFile<ELFT>::getSectionContents(const Elf_Shdr &Sec) const {
  return getSectionContentsAsArray<uint8_t>(Sec);
}

template <class ELFT>
StringRef ELFFile<ELFT>::getRelocationTypeName(uint32_t Type) const {
  return getELFRelocationTypeName(getHeader().e_machine, Type);
}

template <class ELFT>
void ELFFile<ELFT>::getRelocationTypeName(uint32_t Type,
                                          SmallVectorImpl<char> &Result) const {
  if (!isMipsELF64()) {
    StringRef Name = getRelocationTypeName(Type);
    Result.append(Name.begin(), Name.end());
  } else {
    // The Mips N64 ABI allows up to three operations to be specified per
    // relocation record. Unfortunately there's no easy way to test for the
    // presence of N64 ELFs as they have no special flag that identifies them
    // as being N64. We can safely assume at the moment that all Mips
    // ELFCLASS64 ELFs are N64. New Mips64 ABIs should provide enough
    // information to disambiguate between old vs new ABIs.
    uint8_t Type1 = (Type >> 0) & 0xFF;
    uint8_t Type2 = (Type >> 8) & 0xFF;
    uint8_t Type3 = (Type >> 16) & 0xFF;

    // Concat all three relocation type names.
    StringRef Name = getRelocationTypeName(Type1);
    Result.append(Name.begin(), Name.end());

    Name = getRelocationTypeName(Type2);
    Result.append(1, '/');
    Result.append(Name.begin(), Name.end());

    Name = getRelocationTypeName(Type3);
    Result.append(1, '/');
    Result.append(Name.begin(), Name.end());
  }
}

template <class ELFT>
uint32_t ELFFile<ELFT>::getRelativeRelocationType() const {
  return getELFRelativeRelocationType(getHeader().e_machine);
}

template <class ELFT>
Expected<SmallVector<std::optional<VersionEntry>, 0>>
ELFFile<ELFT>::loadVersionMap(const Elf_Shdr *VerNeedSec,
                              const Elf_Shdr *VerDefSec) const {
  SmallVector<std::optional<VersionEntry>, 0> VersionMap;

  // The first two version indexes are reserved.
  // Index 0 is VER_NDX_LOCAL, index 1 is VER_NDX_GLOBAL.
  VersionMap.push_back(VersionEntry());
  VersionMap.push_back(VersionEntry());

  auto InsertEntry = [&](unsigned N, StringRef Version, bool IsVerdef) {
    if (N >= VersionMap.size())
      VersionMap.resize(N + 1);
    VersionMap[N] = {std::string(Version), IsVerdef};
  };

  if (VerDefSec) {
    Expected<std::vector<VerDef>> Defs = getVersionDefinitions(*VerDefSec);
    if (!Defs)
      return Defs.takeError();
    for (const VerDef &Def : *Defs)
      InsertEntry(Def.Ndx & ELF::VERSYM_VERSION, Def.Name, true);
  }

  if (VerNeedSec) {
    Expected<std::vector<VerNeed>> Deps = getVersionDependencies(*VerNeedSec);
    if (!Deps)
      return Deps.takeError();
    for (const VerNeed &Dep : *Deps)
      for (const VernAux &Aux : Dep.AuxV)
        InsertEntry(Aux.Other & ELF::VERSYM_VERSION, Aux.Name, false);
  }

  return VersionMap;
}

template <class ELFT>
Expected<const typename ELFT::Sym *>
ELFFile<ELFT>::getRelocationSymbol(const Elf_Rel &Rel,
                                   const Elf_Shdr *SymTab) const {
  uint32_t Index = Rel.getSymbol(isMips64EL());
  if (Index == 0)
    return nullptr;
  return getEntry<Elf_Sym>(*SymTab, Index);
}

template <class ELFT>
Expected<StringRef>
ELFFile<ELFT>::getSectionStringTable(Elf_Shdr_Range Sections,
                                     WarningHandler WarnHandler) const {
  uint32_t Index = getHeader().e_shstrndx;
  if (Index == ELF::SHN_XINDEX) {
    // If the section name string table section index is greater than
    // or equal to SHN_LORESERVE, then the actual index of the section name
    // string table section is contained in the sh_link field of the section
    // header at index 0.
    if (Sections.empty())
      return createError(
          "e_shstrndx == SHN_XINDEX, but the section header table is empty");

    Index = Sections[0].sh_link;
  }

  // There is no section name string table. Return FakeSectionStrings which
  // is non-empty if we have created fake sections.
  if (!Index)
    return FakeSectionStrings;

  if (Index >= Sections.size())
    return createError("section header string table index " + Twine(Index) +
                       " does not exist");
  return getStringTable(Sections[Index], WarnHandler);
}

/// This function finds the number of dynamic symbols using a GNU hash table.
///
/// @param Table The GNU hash table for .dynsym.
template <class ELFT>
static Expected<uint64_t>
getDynSymtabSizeFromGnuHash(const typename ELFT::GnuHash &Table,
                            const void *BufEnd) {
  using Elf_Word = typename ELFT::Word;
  if (Table.nbuckets == 0)
    return Table.symndx + 1;
  uint64_t LastSymIdx = 0;
  // Find the index of the first symbol in the last chain.
  for (Elf_Word Val : Table.buckets())
    LastSymIdx = std::max(LastSymIdx, (uint64_t)Val);
  const Elf_Word *It =
      reinterpret_cast<const Elf_Word *>(Table.values(LastSymIdx).end());
  // Locate the end of the chain to find the last symbol index.
  while (It < BufEnd && (*It & 1) == 0) {
    ++LastSymIdx;
    ++It;
  }
  if (It >= BufEnd) {
    return createStringError(
        object_error::parse_failed,
        "no terminator found for GNU hash section before buffer end");
  }
  return LastSymIdx + 1;
}

/// This function determines the number of dynamic symbols. It reads section
/// headers first. If section headers are not available, the number of
/// symbols will be inferred by parsing dynamic hash tables.
template <class ELFT>
Expected<uint64_t> ELFFile<ELFT>::getDynSymtabSize() const {
  // Read .dynsym section header first if available.
  Expected<Elf_Shdr_Range> SectionsOrError = sections();
  if (!SectionsOrError)
    return SectionsOrError.takeError();
  for (const Elf_Shdr &Sec : *SectionsOrError) {
    if (Sec.sh_type == ELF::SHT_DYNSYM) {
      if (Sec.sh_size % Sec.sh_entsize != 0) {
        return createStringError(object_error::parse_failed,
                                 "SHT_DYNSYM section has sh_size (" +
                                     Twine(Sec.sh_size) + ") % sh_entsize (" +
                                     Twine(Sec.sh_entsize) + ") that is not 0");
      }
      return Sec.sh_size / Sec.sh_entsize;
    }
  }

  if (!SectionsOrError->empty()) {
    // Section headers are available but .dynsym header is not found.
    // Return 0 as .dynsym does not exist.
    return 0;
  }

  // Section headers do not exist. Falling back to infer
  // upper bound of .dynsym from .gnu.hash and .hash.
  Expected<Elf_Dyn_Range> DynTable = dynamicEntries();
  if (!DynTable)
    return DynTable.takeError();
  std::optional<uint64_t> ElfHash;
  std::optional<uint64_t> ElfGnuHash;
  for (const Elf_Dyn &Entry : *DynTable) {
    switch (Entry.d_tag) {
    case ELF::DT_HASH:
      ElfHash = Entry.d_un.d_ptr;
      break;
    case ELF::DT_GNU_HASH:
      ElfGnuHash = Entry.d_un.d_ptr;
      break;
    }
  }
  if (ElfGnuHash) {
    Expected<const uint8_t *> TablePtr = toMappedAddr(*ElfGnuHash);
    if (!TablePtr)
      return TablePtr.takeError();
    const Elf_GnuHash *Table =
        reinterpret_cast<const Elf_GnuHash *>(TablePtr.get());
    return getDynSymtabSizeFromGnuHash<ELFT>(*Table, this->Buf.bytes_end());
  }

  // Search SYSV hash table to try to find the upper bound of dynsym.
  if (ElfHash) {
    Expected<const uint8_t *> TablePtr = toMappedAddr(*ElfHash);
    if (!TablePtr)
      return TablePtr.takeError();
    const Elf_Hash *Table = reinterpret_cast<const Elf_Hash *>(TablePtr.get());
    return Table->nchain;
  }
  return 0;
}

template <class ELFT> ELFFile<ELFT>::ELFFile(StringRef Object) : Buf(Object) {}

template <class ELFT>
Expected<ELFFile<ELFT>> ELFFile<ELFT>::create(StringRef Object) {
  if (sizeof(Elf_Ehdr) > Object.size())
    return createError("invalid buffer: the size (" + Twine(Object.size()) +
                       ") is smaller than an ELF header (" +
                       Twine(sizeof(Elf_Ehdr)) + ")");
  return ELFFile(Object);
}

/// Used by llvm-objdump -d (which needs sections for disassembly) to
/// disassemble objects without a section header table (e.g. ET_CORE objects
/// analyzed by linux perf or ET_EXEC with llvm-strip --strip-sections).
template <class ELFT> void ELFFile<ELFT>::createFakeSections() {
  if (!FakeSections.empty())
    return;
  auto PhdrsOrErr = program_headers();
  if (!PhdrsOrErr)
    return;

  FakeSectionStrings += '\0';
  for (auto [Idx, Phdr] : llvm::enumerate(*PhdrsOrErr)) {
    if (Phdr.p_type != ELF::PT_LOAD || !(Phdr.p_flags & ELF::PF_X))
      continue;
    Elf_Shdr FakeShdr = {};
    FakeShdr.sh_type = ELF::SHT_PROGBITS;
    FakeShdr.sh_flags = ELF::SHF_ALLOC | ELF::SHF_EXECINSTR;
    FakeShdr.sh_addr = Phdr.p_vaddr;
    FakeShdr.sh_size = Phdr.p_memsz;
    FakeShdr.sh_offset = Phdr.p_offset;
    // Create a section name based on the p_type and index.
    FakeShdr.sh_name = FakeSectionStrings.size();
    FakeSectionStrings += ("PT_LOAD#" + Twine(Idx)).str();
    FakeSectionStrings += '\0';
    FakeSections.push_back(FakeShdr);
  }
}

template <class ELFT>
Expected<typename ELFT::ShdrRange> ELFFile<ELFT>::sections() const {
  const uintX_t SectionTableOffset = getHeader().e_shoff;
  if (SectionTableOffset == 0) {
    if (!FakeSections.empty())
      return ArrayRef(FakeSections.data(), FakeSections.size());
    return ArrayRef<Elf_Shdr>();
  }

  if (getHeader().e_shentsize != sizeof(Elf_Shdr))
    return createError("invalid e_shentsize in ELF header: " +
                       Twine(getHeader().e_shentsize));

  const uint64_t FileSize = Buf.size();
  if (SectionTableOffset + sizeof(Elf_Shdr) > FileSize ||
      SectionTableOffset + (uintX_t)sizeof(Elf_Shdr) < SectionTableOffset)
    return createError(
        "section header table goes past the end of the file: e_shoff = 0x" +
        Twine::utohexstr(SectionTableOffset));

  // Invalid address alignment of section headers
  if (SectionTableOffset & (alignof(Elf_Shdr) - 1))
    // TODO: this error is untested.
    return createError("invalid alignment of section headers");

  const Elf_Shdr *First =
      reinterpret_cast<const Elf_Shdr *>(base() + SectionTableOffset);

  uintX_t NumSections = getHeader().e_shnum;
  if (NumSections == 0)
    NumSections = First->sh_size;

  if (NumSections > UINT64_MAX / sizeof(Elf_Shdr))
    return createError("invalid number of sections specified in the NULL "
                       "section's sh_size field (" +
                       Twine(NumSections) + ")");

  const uint64_t SectionTableSize = NumSections * sizeof(Elf_Shdr);
  if (SectionTableOffset + SectionTableSize < SectionTableOffset)
    return createError(
        "invalid section header table offset (e_shoff = 0x" +
        Twine::utohexstr(SectionTableOffset) +
        ") or invalid number of sections specified in the first section "
        "header's sh_size field (0x" +
        Twine::utohexstr(NumSections) + ")");

  // Section table goes past end of file!
  if (SectionTableOffset + SectionTableSize > FileSize)
    return createError("section table goes past the end of file");
  return ArrayRef(First, NumSections);
}

template <class ELFT>
template <typename T>
Expected<const T *> ELFFile<ELFT>::getEntry(uint32_t Section,
                                            uint32_t Entry) const {
  auto SecOrErr = getSection(Section);
  if (!SecOrErr)
    return SecOrErr.takeError();
  return getEntry<T>(**SecOrErr, Entry);
}

template <class ELFT>
template <typename T>
Expected<const T *> ELFFile<ELFT>::getEntry(const Elf_Shdr &Section,
                                            uint32_t Entry) const {
  Expected<ArrayRef<T>> EntriesOrErr = getSectionContentsAsArray<T>(Section);
  if (!EntriesOrErr)
    return EntriesOrErr.takeError();

  ArrayRef<T> Arr = *EntriesOrErr;
  if (Entry >= Arr.size())
    return createError(
        "can't read an entry at 0x" +
        Twine::utohexstr(Entry * static_cast<uint64_t>(sizeof(T))) +
        ": it goes past the end of the section (0x" +
        Twine::utohexstr(Section.sh_size) + ")");
  return &Arr[Entry];
}

template <typename ELFT>
Expected<StringRef> ELFFile<ELFT>::getSymbolVersionByIndex(
    uint32_t SymbolVersionIndex, bool &IsDefault,
    SmallVector<std::optional<VersionEntry>, 0> &VersionMap,
    std::optional<bool> IsSymHidden) const {
  size_t VersionIndex = SymbolVersionIndex & llvm::ELF::VERSYM_VERSION;

  // Special markers for unversioned symbols.
  if (VersionIndex == llvm::ELF::VER_NDX_LOCAL ||
      VersionIndex == llvm::ELF::VER_NDX_GLOBAL) {
    IsDefault = false;
    return "";
  }

  // Lookup this symbol in the version table.
  if (VersionIndex >= VersionMap.size() || !VersionMap[VersionIndex])
    return createError("SHT_GNU_versym section refers to a version index " +
                       Twine(VersionIndex) + " which is missing");

  const VersionEntry &Entry = *VersionMap[VersionIndex];
  // A default version (@@) is only available for defined symbols.
  if (!Entry.IsVerDef || IsSymHidden.value_or(false))
    IsDefault = false;
  else
    IsDefault = !(SymbolVersionIndex & llvm::ELF::VERSYM_HIDDEN);
  return Entry.Name.c_str();
}

template <class ELFT>
Expected<std::vector<VerDef>>
ELFFile<ELFT>::getVersionDefinitions(const Elf_Shdr &Sec) const {
  Expected<StringRef> StrTabOrErr = getLinkAsStrtab(Sec);
  if (!StrTabOrErr)
    return StrTabOrErr.takeError();

  Expected<ArrayRef<uint8_t>> ContentsOrErr = getSectionContents(Sec);
  if (!ContentsOrErr)
    return createError("cannot read content of " + describe(*this, Sec) + ": " +
                       toString(ContentsOrErr.takeError()));

  const uint8_t *Start = ContentsOrErr->data();
  const uint8_t *End = Start + ContentsOrErr->size();

  auto ExtractNextAux = [&](const uint8_t *&VerdauxBuf,
                            unsigned VerDefNdx) -> Expected<VerdAux> {
    if (VerdauxBuf + sizeof(Elf_Verdaux) > End)
      return createError("invalid " + describe(*this, Sec) +
                         ": version definition " + Twine(VerDefNdx) +
                         " refers to an auxiliary entry that goes past the end "
                         "of the section");

    auto *Verdaux = reinterpret_cast<const Elf_Verdaux *>(VerdauxBuf);
    VerdauxBuf += Verdaux->vda_next;

    VerdAux Aux;
    Aux.Offset = VerdauxBuf - Start;
    if (Verdaux->vda_name <= StrTabOrErr->size())
      Aux.Name = std::string(StrTabOrErr->drop_front(Verdaux->vda_name));
    else
      Aux.Name = ("<invalid vda_name: " + Twine(Verdaux->vda_name) + ">").str();
    return Aux;
  };

  std::vector<VerDef> Ret;
  const uint8_t *VerdefBuf = Start;
  for (unsigned I = 1; I <= /*VerDefsNum=*/Sec.sh_info; ++I) {
    if (VerdefBuf + sizeof(Elf_Verdef) > End)
      return createError("invalid " + describe(*this, Sec) +
                         ": version definition " + Twine(I) +
                         " goes past the end of the section");

    if (reinterpret_cast<uintptr_t>(VerdefBuf) % sizeof(uint32_t) != 0)
      return createError(
          "invalid " + describe(*this, Sec) +
          ": found a misaligned version definition entry at offset 0x" +
          Twine::utohexstr(VerdefBuf - Start));

    unsigned Version = *reinterpret_cast<const Elf_Half *>(VerdefBuf);
    if (Version != 1)
      return createError("unable to dump " + describe(*this, Sec) +
                         ": version " + Twine(Version) +
                         " is not yet supported");

    const Elf_Verdef *D = reinterpret_cast<const Elf_Verdef *>(VerdefBuf);
    VerDef &VD = *Ret.emplace(Ret.end());
    VD.Offset = VerdefBuf - Start;
    VD.Version = D->vd_version;
    VD.Flags = D->vd_flags;
    VD.Ndx = D->vd_ndx;
    VD.Cnt = D->vd_cnt;
    VD.Hash = D->vd_hash;

    const uint8_t *VerdauxBuf = VerdefBuf + D->vd_aux;
    for (unsigned J = 0; J < D->vd_cnt; ++J) {
      if (reinterpret_cast<uintptr_t>(VerdauxBuf) % sizeof(uint32_t) != 0)
        return createError("invalid " + describe(*this, Sec) +
                           ": found a misaligned auxiliary entry at offset 0x" +
                           Twine::utohexstr(VerdauxBuf - Start));

      Expected<VerdAux> AuxOrErr = ExtractNextAux(VerdauxBuf, I);
      if (!AuxOrErr)
        return AuxOrErr.takeError();

      if (J == 0)
        VD.Name = AuxOrErr->Name;
      else
        VD.AuxV.push_back(*AuxOrErr);
    }

    VerdefBuf += D->vd_next;
  }

  return Ret;
}

template <class ELFT>
Expected<std::vector<VerNeed>>
ELFFile<ELFT>::getVersionDependencies(const Elf_Shdr &Sec,
                                      WarningHandler WarnHandler) const {
  StringRef StrTab;
  Expected<StringRef> StrTabOrErr = getLinkAsStrtab(Sec);
  if (!StrTabOrErr) {
    if (Error E = WarnHandler(toString(StrTabOrErr.takeError())))
      return std::move(E);
  } else {
    StrTab = *StrTabOrErr;
  }

  Expected<ArrayRef<uint8_t>> ContentsOrErr = getSectionContents(Sec);
  if (!ContentsOrErr)
    return createError("cannot read content of " + describe(*this, Sec) + ": " +
                       toString(ContentsOrErr.takeError()));

  const uint8_t *Start = ContentsOrErr->data();
  const uint8_t *End = Start + ContentsOrErr->size();
  const uint8_t *VerneedBuf = Start;

  std::vector<VerNeed> Ret;
  for (unsigned I = 1; I <= /*VerneedNum=*/Sec.sh_info; ++I) {
    if (VerneedBuf + sizeof(Elf_Verdef) > End)
      return createError("invalid " + describe(*this, Sec) +
                         ": version dependency " + Twine(I) +
                         " goes past the end of the section");

    if (reinterpret_cast<uintptr_t>(VerneedBuf) % sizeof(uint32_t) != 0)
      return createError(
          "invalid " + describe(*this, Sec) +
          ": found a misaligned version dependency entry at offset 0x" +
          Twine::utohexstr(VerneedBuf - Start));

    unsigned Version = *reinterpret_cast<const Elf_Half *>(VerneedBuf);
    if (Version != 1)
      return createError("unable to dump " + describe(*this, Sec) +
                         ": version " + Twine(Version) +
                         " is not yet supported");

    const Elf_Verneed *Verneed =
        reinterpret_cast<const Elf_Verneed *>(VerneedBuf);

    VerNeed &VN = *Ret.emplace(Ret.end());
    VN.Version = Verneed->vn_version;
    VN.Cnt = Verneed->vn_cnt;
    VN.Offset = VerneedBuf - Start;

    if (Verneed->vn_file < StrTab.size())
      VN.File = std::string(StrTab.data() + Verneed->vn_file);
    else
      VN.File = ("<corrupt vn_file: " + Twine(Verneed->vn_file) + ">").str();

    const uint8_t *VernauxBuf = VerneedBuf + Verneed->vn_aux;
    for (unsigned J = 0; J < Verneed->vn_cnt; ++J) {
      if (reinterpret_cast<uintptr_t>(VernauxBuf) % sizeof(uint32_t) != 0)
        return createError("invalid " + describe(*this, Sec) +
                           ": found a misaligned auxiliary entry at offset 0x" +
                           Twine::utohexstr(VernauxBuf - Start));

      if (VernauxBuf + sizeof(Elf_Vernaux) > End)
        return createError(
            "invalid " + describe(*this, Sec) + ": version dependency " +
            Twine(I) +
            " refers to an auxiliary entry that goes past the end "
            "of the section");

      const Elf_Vernaux *Vernaux =
          reinterpret_cast<const Elf_Vernaux *>(VernauxBuf);

      VernAux &Aux = *VN.AuxV.emplace(VN.AuxV.end());
      Aux.Hash = Vernaux->vna_hash;
      Aux.Flags = Vernaux->vna_flags;
      Aux.Other = Vernaux->vna_other;
      Aux.Offset = VernauxBuf - Start;
      if (StrTab.size() <= Vernaux->vna_name)
        Aux.Name = "<corrupt>";
      else
        Aux.Name = std::string(StrTab.drop_front(Vernaux->vna_name));

      VernauxBuf += Vernaux->vna_next;
    }
    VerneedBuf += Verneed->vn_next;
  }
  return Ret;
}

template <class ELFT>
Expected<const typename ELFT::Shdr *>
ELFFile<ELFT>::getSection(uint32_t Index) const {
  auto TableOrErr = sections();
  if (!TableOrErr)
    return TableOrErr.takeError();
  return object::getSection<ELFT>(*TableOrErr, Index);
}

template <class ELFT>
Expected<StringRef>
ELFFile<ELFT>::getStringTable(const Elf_Shdr &Section,
                              WarningHandler WarnHandler) const {
  if (Section.sh_type != ELF::SHT_STRTAB)
    if (Error E = WarnHandler("invalid sh_type for string table section " +
                              getSecIndexForError(*this, Section) +
                              ": expected SHT_STRTAB, but got " +
                              object::getELFSectionTypeName(
                                  getHeader().e_machine, Section.sh_type)))
      return std::move(E);

  auto V = getSectionContentsAsArray<char>(Section);
  if (!V)
    return V.takeError();
  ArrayRef<char> Data = *V;
  if (Data.empty())
    return createError("SHT_STRTAB string table section " +
                       getSecIndexForError(*this, Section) + " is empty");
  if (Data.back() != '\0')
    return createError("SHT_STRTAB string table section " +
                       getSecIndexForError(*this, Section) +
                       " is non-null terminated");
  return StringRef(Data.begin(), Data.size());
}

template <class ELFT>
Expected<ArrayRef<typename ELFT::Word>>
ELFFile<ELFT>::getSHNDXTable(const Elf_Shdr &Section) const {
  auto SectionsOrErr = sections();
  if (!SectionsOrErr)
    return SectionsOrErr.takeError();
  return getSHNDXTable(Section, *SectionsOrErr);
}

template <class ELFT>
Expected<ArrayRef<typename ELFT::Word>>
ELFFile<ELFT>::getSHNDXTable(const Elf_Shdr &Section,
                             Elf_Shdr_Range Sections) const {
  assert(Section.sh_type == ELF::SHT_SYMTAB_SHNDX);
  auto VOrErr = getSectionContentsAsArray<Elf_Word>(Section);
  if (!VOrErr)
    return VOrErr.takeError();
  ArrayRef<Elf_Word> V = *VOrErr;
  auto SymTableOrErr = object::getSection<ELFT>(Sections, Section.sh_link);
  if (!SymTableOrErr)
    return SymTableOrErr.takeError();
  const Elf_Shdr &SymTable = **SymTableOrErr;
  if (SymTable.sh_type != ELF::SHT_SYMTAB &&
      SymTable.sh_type != ELF::SHT_DYNSYM)
    return createError(
        "SHT_SYMTAB_SHNDX section is linked with " +
        object::getELFSectionTypeName(getHeader().e_machine, SymTable.sh_type) +
        " section (expected SHT_SYMTAB/SHT_DYNSYM)");

  uint64_t Syms = SymTable.sh_size / sizeof(Elf_Sym);
  if (V.size() != Syms)
    return createError("SHT_SYMTAB_SHNDX has " + Twine(V.size()) +
                       " entries, but the symbol table associated has " +
                       Twine(Syms));

  return V;
}

template <class ELFT>
Expected<StringRef>
ELFFile<ELFT>::getStringTableForSymtab(const Elf_Shdr &Sec) const {
  auto SectionsOrErr = sections();
  if (!SectionsOrErr)
    return SectionsOrErr.takeError();
  return getStringTableForSymtab(Sec, *SectionsOrErr);
}

template <class ELFT>
Expected<StringRef>
ELFFile<ELFT>::getStringTableForSymtab(const Elf_Shdr &Sec,
                                       Elf_Shdr_Range Sections) const {

  if (Sec.sh_type != ELF::SHT_SYMTAB && Sec.sh_type != ELF::SHT_DYNSYM)
    return createError(
        "invalid sh_type for symbol table, expected SHT_SYMTAB or SHT_DYNSYM");
  Expected<const Elf_Shdr *> SectionOrErr =
      object::getSection<ELFT>(Sections, Sec.sh_link);
  if (!SectionOrErr)
    return SectionOrErr.takeError();
  return getStringTable(**SectionOrErr);
}

template <class ELFT>
Expected<StringRef>
ELFFile<ELFT>::getLinkAsStrtab(const typename ELFT::Shdr &Sec) const {
  Expected<const typename ELFT::Shdr *> StrTabSecOrErr =
      getSection(Sec.sh_link);
  if (!StrTabSecOrErr)
    return createError("invalid section linked to " + describe(*this, Sec) +
                       ": " + toString(StrTabSecOrErr.takeError()));

  Expected<StringRef> StrTabOrErr = getStringTable(**StrTabSecOrErr);
  if (!StrTabOrErr)
    return createError("invalid string table linked to " +
                       describe(*this, Sec) + ": " +
                       toString(StrTabOrErr.takeError()));
  return *StrTabOrErr;
}

template <class ELFT>
Expected<StringRef>
ELFFile<ELFT>::getSectionName(const Elf_Shdr &Section,
                              WarningHandler WarnHandler) const {
  auto SectionsOrErr = sections();
  if (!SectionsOrErr)
    return SectionsOrErr.takeError();
  auto Table = getSectionStringTable(*SectionsOrErr, WarnHandler);
  if (!Table)
    return Table.takeError();
  return getSectionName(Section, *Table);
}

template <class ELFT>
Expected<StringRef> ELFFile<ELFT>::getSectionName(const Elf_Shdr &Section,
                                                  StringRef DotShstrtab) const {
  uint32_t Offset = Section.sh_name;
  if (Offset == 0)
    return StringRef();
  if (Offset >= DotShstrtab.size())
    return createError("a section " + getSecIndexForError(*this, Section) +
                       " has an invalid sh_name (0x" +
                       Twine::utohexstr(Offset) +
                       ") offset which goes past the end of the "
                       "section name string table");
  return StringRef(DotShstrtab.data() + Offset);
}

/// This function returns the hash value for a symbol in the .dynsym section
/// Name of the API remains consistent as specified in the libelf
/// REF : http://www.sco.com/developers/gabi/latest/ch5.dynamic.html#hash
inline uint32_t hashSysV(StringRef SymbolName) {
  uint32_t H = 0;
  for (uint8_t C : SymbolName) {
    H = (H << 4) + C;
    H ^= (H >> 24) & 0xf0;
  }
  return H & 0x0fffffff;
}

/// This function returns the hash value for a symbol in the .dynsym section
/// for the GNU hash table. The implementation is defined in the GNU hash ABI.
/// REF : https://sourceware.org/git/?p=binutils-gdb.git;a=blob;f=bfd/elf.c#l222
inline uint32_t hashGnu(StringRef Name) {
  uint32_t H = 5381;
  for (uint8_t C : Name)
    H = (H << 5) + H + C;
  return H;
}

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_ELF_H
