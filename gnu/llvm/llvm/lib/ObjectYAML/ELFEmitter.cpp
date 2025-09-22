//===- yaml2elf - Convert YAML to a ELF object file -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The ELF component of yaml2obj.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/ObjectYAML/DWARFEmitter.h"
#include "llvm/ObjectYAML/DWARFYAML.h"
#include "llvm/ObjectYAML/ELFYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <variant>

using namespace llvm;

// This class is used to build up a contiguous binary blob while keeping
// track of an offset in the output (which notionally begins at
// `InitialOffset`).
// The blob might be limited to an arbitrary size. All attempts to write data
// are ignored and the error condition is remembered once the limit is reached.
// Such an approach allows us to simplify the code by delaying error reporting
// and doing it at a convenient time.
namespace {
class ContiguousBlobAccumulator {
  const uint64_t InitialOffset;
  const uint64_t MaxSize;

  SmallVector<char, 128> Buf;
  raw_svector_ostream OS;
  Error ReachedLimitErr = Error::success();

  bool checkLimit(uint64_t Size) {
    if (!ReachedLimitErr && getOffset() + Size <= MaxSize)
      return true;
    if (!ReachedLimitErr)
      ReachedLimitErr = createStringError(errc::invalid_argument,
                                          "reached the output size limit");
    return false;
  }

public:
  ContiguousBlobAccumulator(uint64_t BaseOffset, uint64_t SizeLimit)
      : InitialOffset(BaseOffset), MaxSize(SizeLimit), OS(Buf) {}

  uint64_t tell() const { return OS.tell(); }
  uint64_t getOffset() const { return InitialOffset + OS.tell(); }
  void writeBlobToStream(raw_ostream &Out) const { Out << OS.str(); }

  Error takeLimitError() {
    // Request to write 0 bytes to check we did not reach the limit.
    checkLimit(0);
    return std::move(ReachedLimitErr);
  }

  /// \returns The new offset.
  uint64_t padToAlignment(unsigned Align) {
    uint64_t CurrentOffset = getOffset();
    if (ReachedLimitErr)
      return CurrentOffset;

    uint64_t AlignedOffset = alignTo(CurrentOffset, Align == 0 ? 1 : Align);
    uint64_t PaddingSize = AlignedOffset - CurrentOffset;
    if (!checkLimit(PaddingSize))
      return CurrentOffset;

    writeZeros(PaddingSize);
    return AlignedOffset;
  }

  raw_ostream *getRawOS(uint64_t Size) {
    if (checkLimit(Size))
      return &OS;
    return nullptr;
  }

  void writeAsBinary(const yaml::BinaryRef &Bin, uint64_t N = UINT64_MAX) {
    if (!checkLimit(Bin.binary_size()))
      return;
    Bin.writeAsBinary(OS, N);
  }

  void writeZeros(uint64_t Num) {
    if (checkLimit(Num))
      OS.write_zeros(Num);
  }

  void write(const char *Ptr, size_t Size) {
    if (checkLimit(Size))
      OS.write(Ptr, Size);
  }

  void write(unsigned char C) {
    if (checkLimit(1))
      OS.write(C);
  }

  unsigned writeULEB128(uint64_t Val) {
    if (!checkLimit(sizeof(uint64_t)))
      return 0;
    return encodeULEB128(Val, OS);
  }

  unsigned writeSLEB128(int64_t Val) {
    if (!checkLimit(10))
      return 0;
    return encodeSLEB128(Val, OS);
  }

  template <typename T> void write(T Val, llvm::endianness E) {
    if (checkLimit(sizeof(T)))
      support::endian::write<T>(OS, Val, E);
  }

  void updateDataAt(uint64_t Pos, void *Data, size_t Size) {
    assert(Pos >= InitialOffset && Pos + Size <= getOffset());
    memcpy(&Buf[Pos - InitialOffset], Data, Size);
  }
};

// Used to keep track of section and symbol names, so that in the YAML file
// sections and symbols can be referenced by name instead of by index.
class NameToIdxMap {
  StringMap<unsigned> Map;

public:
  /// \Returns false if name is already present in the map.
  bool addName(StringRef Name, unsigned Ndx) {
    return Map.insert({Name, Ndx}).second;
  }
  /// \Returns false if name is not present in the map.
  bool lookup(StringRef Name, unsigned &Idx) const {
    auto I = Map.find(Name);
    if (I == Map.end())
      return false;
    Idx = I->getValue();
    return true;
  }
  /// Asserts if name is not present in the map.
  unsigned get(StringRef Name) const {
    unsigned Idx;
    if (lookup(Name, Idx))
      return Idx;
    assert(false && "Expected section not found in index");
    return 0;
  }
  unsigned size() const { return Map.size(); }
};

namespace {
struct Fragment {
  uint64_t Offset;
  uint64_t Size;
  uint32_t Type;
  uint64_t AddrAlign;
};
} // namespace

/// "Single point of truth" for the ELF file construction.
/// TODO: This class still has a ways to go before it is truly a "single
/// point of truth".
template <class ELFT> class ELFState {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

  enum class SymtabType { Static, Dynamic };

  /// The future symbol table string section.
  StringTableBuilder DotStrtab{StringTableBuilder::ELF};

  /// The future section header string table section, if a unique string table
  /// is needed. Don't reference this variable direectly: use the
  /// ShStrtabStrings member instead.
  StringTableBuilder DotShStrtab{StringTableBuilder::ELF};

  /// The future dynamic symbol string section.
  StringTableBuilder DotDynstr{StringTableBuilder::ELF};

  /// The name of the section header string table section. If it is .strtab or
  /// .dynstr, the section header strings will be written to the same string
  /// table as the static/dynamic symbols respectively. Otherwise a dedicated
  /// section will be created with that name.
  StringRef SectionHeaderStringTableName = ".shstrtab";
  StringTableBuilder *ShStrtabStrings = &DotShStrtab;

  NameToIdxMap SN2I;
  NameToIdxMap SymN2I;
  NameToIdxMap DynSymN2I;
  ELFYAML::Object &Doc;

  StringSet<> ExcludedSectionHeaders;

  uint64_t LocationCounter = 0;
  bool HasError = false;
  yaml::ErrorHandler ErrHandler;
  void reportError(const Twine &Msg);
  void reportError(Error Err);

  std::vector<Elf_Sym> toELFSymbols(ArrayRef<ELFYAML::Symbol> Symbols,
                                    const StringTableBuilder &Strtab);
  unsigned toSectionIndex(StringRef S, StringRef LocSec, StringRef LocSym = "");
  unsigned toSymbolIndex(StringRef S, StringRef LocSec, bool IsDynamic);

  void buildSectionIndex();
  void buildSymbolIndexes();
  void initProgramHeaders(std::vector<Elf_Phdr> &PHeaders);
  bool initImplicitHeader(ContiguousBlobAccumulator &CBA, Elf_Shdr &Header,
                          StringRef SecName, ELFYAML::Section *YAMLSec);
  void initSectionHeaders(std::vector<Elf_Shdr> &SHeaders,
                          ContiguousBlobAccumulator &CBA);
  void initSymtabSectionHeader(Elf_Shdr &SHeader, SymtabType STType,
                               ContiguousBlobAccumulator &CBA,
                               ELFYAML::Section *YAMLSec);
  void initStrtabSectionHeader(Elf_Shdr &SHeader, StringRef Name,
                               StringTableBuilder &STB,
                               ContiguousBlobAccumulator &CBA,
                               ELFYAML::Section *YAMLSec);
  void initDWARFSectionHeader(Elf_Shdr &SHeader, StringRef Name,
                              ContiguousBlobAccumulator &CBA,
                              ELFYAML::Section *YAMLSec);
  void setProgramHeaderLayout(std::vector<Elf_Phdr> &PHeaders,
                              std::vector<Elf_Shdr> &SHeaders);

  std::vector<Fragment>
  getPhdrFragments(const ELFYAML::ProgramHeader &Phdr,
                   ArrayRef<typename ELFT::Shdr> SHeaders);

  void finalizeStrings();
  void writeELFHeader(raw_ostream &OS);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::NoBitsSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::RawContentSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::RelocationSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::RelrSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::GroupSection &Group,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::SymtabShndxSection &Shndx,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::SymverSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::VerneedSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::VerdefSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::ARMIndexTableSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::MipsABIFlags &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::DynamicSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::StackSizesSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::BBAddrMapSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::HashSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::AddrsigSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::NoteSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::GnuHashSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::LinkerOptionsSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::DependentLibrariesSection &Section,
                           ContiguousBlobAccumulator &CBA);
  void writeSectionContent(Elf_Shdr &SHeader,
                           const ELFYAML::CallGraphProfileSection &Section,
                           ContiguousBlobAccumulator &CBA);

  void writeFill(ELFYAML::Fill &Fill, ContiguousBlobAccumulator &CBA);

  ELFState(ELFYAML::Object &D, yaml::ErrorHandler EH);

  void assignSectionAddress(Elf_Shdr &SHeader, ELFYAML::Section *YAMLSec);

  DenseMap<StringRef, size_t> buildSectionHeaderReorderMap();

  BumpPtrAllocator StringAlloc;
  uint64_t alignToOffset(ContiguousBlobAccumulator &CBA, uint64_t Align,
                         std::optional<llvm::yaml::Hex64> Offset);

  uint64_t getSectionNameOffset(StringRef Name);

public:
  static bool writeELF(raw_ostream &OS, ELFYAML::Object &Doc,
                       yaml::ErrorHandler EH, uint64_t MaxSize);
};
} // end anonymous namespace

template <class T> static size_t arrayDataSize(ArrayRef<T> A) {
  return A.size() * sizeof(T);
}

template <class T> static void writeArrayData(raw_ostream &OS, ArrayRef<T> A) {
  OS.write((const char *)A.data(), arrayDataSize(A));
}

template <class T> static void zero(T &Obj) { memset(&Obj, 0, sizeof(Obj)); }

template <class ELFT>
ELFState<ELFT>::ELFState(ELFYAML::Object &D, yaml::ErrorHandler EH)
    : Doc(D), ErrHandler(EH) {
  // The input may explicitly request to store the section header table strings
  // in the same string table as dynamic or static symbol names. Set the
  // ShStrtabStrings member accordingly.
  if (Doc.Header.SectionHeaderStringTable) {
    SectionHeaderStringTableName = *Doc.Header.SectionHeaderStringTable;
    if (*Doc.Header.SectionHeaderStringTable == ".strtab")
      ShStrtabStrings = &DotStrtab;
    else if (*Doc.Header.SectionHeaderStringTable == ".dynstr")
      ShStrtabStrings = &DotDynstr;
    // Otherwise, the unique table will be used.
  }

  std::vector<ELFYAML::Section *> Sections = Doc.getSections();
  // Insert SHT_NULL section implicitly when it is not defined in YAML.
  if (Sections.empty() || Sections.front()->Type != ELF::SHT_NULL)
    Doc.Chunks.insert(
        Doc.Chunks.begin(),
        std::make_unique<ELFYAML::Section>(
            ELFYAML::Chunk::ChunkKind::RawContent, /*IsImplicit=*/true));

  StringSet<> DocSections;
  ELFYAML::SectionHeaderTable *SecHdrTable = nullptr;
  for (size_t I = 0; I < Doc.Chunks.size(); ++I) {
    const std::unique_ptr<ELFYAML::Chunk> &C = Doc.Chunks[I];

    // We might have an explicit section header table declaration.
    if (auto S = dyn_cast<ELFYAML::SectionHeaderTable>(C.get())) {
      if (SecHdrTable)
        reportError("multiple section header tables are not allowed");
      SecHdrTable = S;
      continue;
    }

    // We add a technical suffix for each unnamed section/fill. It does not
    // affect the output, but allows us to map them by name in the code and
    // report better error messages.
    if (C->Name.empty()) {
      std::string NewName = ELFYAML::appendUniqueSuffix(
          /*Name=*/"", "index " + Twine(I));
      C->Name = StringRef(NewName).copy(StringAlloc);
      assert(ELFYAML::dropUniqueSuffix(C->Name).empty());
    }

    if (!DocSections.insert(C->Name).second)
      reportError("repeated section/fill name: '" + C->Name +
                  "' at YAML section/fill number " + Twine(I));
  }

  SmallSetVector<StringRef, 8> ImplicitSections;
  if (Doc.DynamicSymbols) {
    if (SectionHeaderStringTableName == ".dynsym")
      reportError("cannot use '.dynsym' as the section header name table when "
                  "there are dynamic symbols");
    ImplicitSections.insert(".dynsym");
    ImplicitSections.insert(".dynstr");
  }
  if (Doc.Symbols) {
    if (SectionHeaderStringTableName == ".symtab")
      reportError("cannot use '.symtab' as the section header name table when "
                  "there are symbols");
    ImplicitSections.insert(".symtab");
  }
  if (Doc.DWARF)
    for (StringRef DebugSecName : Doc.DWARF->getNonEmptySectionNames()) {
      std::string SecName = ("." + DebugSecName).str();
      // TODO: For .debug_str it should be possible to share the string table,
      // in the same manner as the symbol string tables.
      if (SectionHeaderStringTableName == SecName)
        reportError("cannot use '" + SecName +
                    "' as the section header name table when it is needed for "
                    "DWARF output");
      ImplicitSections.insert(StringRef(SecName).copy(StringAlloc));
    }
  // TODO: Only create the .strtab here if any symbols have been requested.
  ImplicitSections.insert(".strtab");
  if (!SecHdrTable || !SecHdrTable->NoHeaders.value_or(false))
    ImplicitSections.insert(SectionHeaderStringTableName);

  // Insert placeholders for implicit sections that are not
  // defined explicitly in YAML.
  for (StringRef SecName : ImplicitSections) {
    if (DocSections.count(SecName))
      continue;

    std::unique_ptr<ELFYAML::Section> Sec = std::make_unique<ELFYAML::Section>(
        ELFYAML::Chunk::ChunkKind::RawContent, true /*IsImplicit*/);
    Sec->Name = SecName;

    if (SecName == SectionHeaderStringTableName)
      Sec->Type = ELF::SHT_STRTAB;
    else if (SecName == ".dynsym")
      Sec->Type = ELF::SHT_DYNSYM;
    else if (SecName == ".symtab")
      Sec->Type = ELF::SHT_SYMTAB;
    else
      Sec->Type = ELF::SHT_STRTAB;

    // When the section header table is explicitly defined at the end of the
    // sections list, it is reasonable to assume that the user wants to reorder
    // section headers, but still wants to place the section header table after
    // all sections, like it normally happens. In this case we want to insert
    // other implicit sections right before the section header table.
    if (Doc.Chunks.back().get() == SecHdrTable)
      Doc.Chunks.insert(Doc.Chunks.end() - 1, std::move(Sec));
    else
      Doc.Chunks.push_back(std::move(Sec));
  }

  // Insert the section header table implicitly at the end, when it is not
  // explicitly defined.
  if (!SecHdrTable)
    Doc.Chunks.push_back(
        std::make_unique<ELFYAML::SectionHeaderTable>(/*IsImplicit=*/true));
}

template <class ELFT>
void ELFState<ELFT>::writeELFHeader(raw_ostream &OS) {
  using namespace llvm::ELF;

  Elf_Ehdr Header;
  zero(Header);
  Header.e_ident[EI_MAG0] = 0x7f;
  Header.e_ident[EI_MAG1] = 'E';
  Header.e_ident[EI_MAG2] = 'L';
  Header.e_ident[EI_MAG3] = 'F';
  Header.e_ident[EI_CLASS] = ELFT::Is64Bits ? ELFCLASS64 : ELFCLASS32;
  Header.e_ident[EI_DATA] = Doc.Header.Data;
  Header.e_ident[EI_VERSION] = EV_CURRENT;
  Header.e_ident[EI_OSABI] = Doc.Header.OSABI;
  Header.e_ident[EI_ABIVERSION] = Doc.Header.ABIVersion;
  Header.e_type = Doc.Header.Type;

  if (Doc.Header.Machine)
    Header.e_machine = *Doc.Header.Machine;
  else
    Header.e_machine = EM_NONE;

  Header.e_version = EV_CURRENT;
  Header.e_entry = Doc.Header.Entry;
  Header.e_flags = Doc.Header.Flags;
  Header.e_ehsize = sizeof(Elf_Ehdr);

  if (Doc.Header.EPhOff)
    Header.e_phoff = *Doc.Header.EPhOff;
  else if (!Doc.ProgramHeaders.empty())
    Header.e_phoff = sizeof(Header);
  else
    Header.e_phoff = 0;

  if (Doc.Header.EPhEntSize)
    Header.e_phentsize = *Doc.Header.EPhEntSize;
  else if (!Doc.ProgramHeaders.empty())
    Header.e_phentsize = sizeof(Elf_Phdr);
  else
    Header.e_phentsize = 0;

  if (Doc.Header.EPhNum)
    Header.e_phnum = *Doc.Header.EPhNum;
  else if (!Doc.ProgramHeaders.empty())
    Header.e_phnum = Doc.ProgramHeaders.size();
  else
    Header.e_phnum = 0;

  Header.e_shentsize = Doc.Header.EShEntSize ? (uint16_t)*Doc.Header.EShEntSize
                                             : sizeof(Elf_Shdr);

  const ELFYAML::SectionHeaderTable &SectionHeaders =
      Doc.getSectionHeaderTable();

  if (Doc.Header.EShOff)
    Header.e_shoff = *Doc.Header.EShOff;
  else if (SectionHeaders.Offset)
    Header.e_shoff = *SectionHeaders.Offset;
  else
    Header.e_shoff = 0;

  if (Doc.Header.EShNum)
    Header.e_shnum = *Doc.Header.EShNum;
  else
    Header.e_shnum = SectionHeaders.getNumHeaders(Doc.getSections().size());

  if (Doc.Header.EShStrNdx)
    Header.e_shstrndx = *Doc.Header.EShStrNdx;
  else if (SectionHeaders.Offset &&
           !ExcludedSectionHeaders.count(SectionHeaderStringTableName))
    Header.e_shstrndx = SN2I.get(SectionHeaderStringTableName);
  else
    Header.e_shstrndx = 0;

  OS.write((const char *)&Header, sizeof(Header));
}

template <class ELFT>
void ELFState<ELFT>::initProgramHeaders(std::vector<Elf_Phdr> &PHeaders) {
  DenseMap<StringRef, ELFYAML::Fill *> NameToFill;
  DenseMap<StringRef, size_t> NameToIndex;
  for (size_t I = 0, E = Doc.Chunks.size(); I != E; ++I) {
    if (auto S = dyn_cast<ELFYAML::Fill>(Doc.Chunks[I].get()))
      NameToFill[S->Name] = S;
    NameToIndex[Doc.Chunks[I]->Name] = I + 1;
  }

  std::vector<ELFYAML::Section *> Sections = Doc.getSections();
  for (size_t I = 0, E = Doc.ProgramHeaders.size(); I != E; ++I) {
    ELFYAML::ProgramHeader &YamlPhdr = Doc.ProgramHeaders[I];
    Elf_Phdr Phdr;
    zero(Phdr);
    Phdr.p_type = YamlPhdr.Type;
    Phdr.p_flags = YamlPhdr.Flags;
    Phdr.p_vaddr = YamlPhdr.VAddr;
    Phdr.p_paddr = YamlPhdr.PAddr;
    PHeaders.push_back(Phdr);

    if (!YamlPhdr.FirstSec && !YamlPhdr.LastSec)
      continue;

    // Get the index of the section, or 0 in the case when the section doesn't exist.
    size_t First = NameToIndex[*YamlPhdr.FirstSec];
    if (!First)
      reportError("unknown section or fill referenced: '" + *YamlPhdr.FirstSec +
                  "' by the 'FirstSec' key of the program header with index " +
                  Twine(I));
    size_t Last = NameToIndex[*YamlPhdr.LastSec];
    if (!Last)
      reportError("unknown section or fill referenced: '" + *YamlPhdr.LastSec +
                  "' by the 'LastSec' key of the program header with index " +
                  Twine(I));
    if (!First || !Last)
      continue;

    if (First > Last)
      reportError("program header with index " + Twine(I) +
                  ": the section index of " + *YamlPhdr.FirstSec +
                  " is greater than the index of " + *YamlPhdr.LastSec);

    for (size_t I = First; I <= Last; ++I)
      YamlPhdr.Chunks.push_back(Doc.Chunks[I - 1].get());
  }
}

template <class ELFT>
unsigned ELFState<ELFT>::toSectionIndex(StringRef S, StringRef LocSec,
                                        StringRef LocSym) {
  assert(LocSec.empty() || LocSym.empty());

  unsigned Index;
  if (!SN2I.lookup(S, Index) && !to_integer(S, Index)) {
    if (!LocSym.empty())
      reportError("unknown section referenced: '" + S + "' by YAML symbol '" +
                  LocSym + "'");
    else
      reportError("unknown section referenced: '" + S + "' by YAML section '" +
                  LocSec + "'");
    return 0;
  }

  const ELFYAML::SectionHeaderTable &SectionHeaders =
      Doc.getSectionHeaderTable();
  if (SectionHeaders.IsImplicit ||
      (SectionHeaders.NoHeaders && !*SectionHeaders.NoHeaders) ||
      SectionHeaders.isDefault())
    return Index;

  assert(!SectionHeaders.NoHeaders.value_or(false) || !SectionHeaders.Sections);
  size_t FirstExcluded =
      SectionHeaders.Sections ? SectionHeaders.Sections->size() : 0;
  if (Index > FirstExcluded) {
    if (LocSym.empty())
      reportError("unable to link '" + LocSec + "' to excluded section '" + S +
                  "'");
    else
      reportError("excluded section referenced: '" + S + "'  by symbol '" +
                  LocSym + "'");
  }
  return Index;
}

template <class ELFT>
unsigned ELFState<ELFT>::toSymbolIndex(StringRef S, StringRef LocSec,
                                       bool IsDynamic) {
  const NameToIdxMap &SymMap = IsDynamic ? DynSymN2I : SymN2I;
  unsigned Index;
  // Here we try to look up S in the symbol table. If it is not there,
  // treat its value as a symbol index.
  if (!SymMap.lookup(S, Index) && !to_integer(S, Index)) {
    reportError("unknown symbol referenced: '" + S + "' by YAML section '" +
                LocSec + "'");
    return 0;
  }
  return Index;
}

template <class ELFT>
static void overrideFields(ELFYAML::Section *From, typename ELFT::Shdr &To) {
  if (!From)
    return;
  if (From->ShAddrAlign)
    To.sh_addralign = *From->ShAddrAlign;
  if (From->ShFlags)
    To.sh_flags = *From->ShFlags;
  if (From->ShName)
    To.sh_name = *From->ShName;
  if (From->ShOffset)
    To.sh_offset = *From->ShOffset;
  if (From->ShSize)
    To.sh_size = *From->ShSize;
  if (From->ShType)
    To.sh_type = *From->ShType;
}

template <class ELFT>
bool ELFState<ELFT>::initImplicitHeader(ContiguousBlobAccumulator &CBA,
                                        Elf_Shdr &Header, StringRef SecName,
                                        ELFYAML::Section *YAMLSec) {
  // Check if the header was already initialized.
  if (Header.sh_offset)
    return false;

  if (SecName == ".strtab")
    initStrtabSectionHeader(Header, SecName, DotStrtab, CBA, YAMLSec);
  else if (SecName == ".dynstr")
    initStrtabSectionHeader(Header, SecName, DotDynstr, CBA, YAMLSec);
  else if (SecName == SectionHeaderStringTableName)
    initStrtabSectionHeader(Header, SecName, *ShStrtabStrings, CBA, YAMLSec);
  else if (SecName == ".symtab")
    initSymtabSectionHeader(Header, SymtabType::Static, CBA, YAMLSec);
  else if (SecName == ".dynsym")
    initSymtabSectionHeader(Header, SymtabType::Dynamic, CBA, YAMLSec);
  else if (SecName.starts_with(".debug_")) {
    // If a ".debug_*" section's type is a preserved one, e.g., SHT_DYNAMIC, we
    // will not treat it as a debug section.
    if (YAMLSec && !isa<ELFYAML::RawContentSection>(YAMLSec))
      return false;
    initDWARFSectionHeader(Header, SecName, CBA, YAMLSec);
  } else
    return false;

  LocationCounter += Header.sh_size;

  // Override section fields if requested.
  overrideFields<ELFT>(YAMLSec, Header);
  return true;
}

constexpr char SuffixStart = '(';
constexpr char SuffixEnd = ')';

std::string llvm::ELFYAML::appendUniqueSuffix(StringRef Name,
                                              const Twine &Msg) {
  // Do not add a space when a Name is empty.
  std::string Ret = Name.empty() ? "" : Name.str() + ' ';
  return Ret + (Twine(SuffixStart) + Msg + Twine(SuffixEnd)).str();
}

StringRef llvm::ELFYAML::dropUniqueSuffix(StringRef S) {
  if (S.empty() || S.back() != SuffixEnd)
    return S;

  // A special case for empty names. See appendUniqueSuffix() above.
  size_t SuffixPos = S.rfind(SuffixStart);
  if (SuffixPos == 0)
    return "";

  if (SuffixPos == StringRef::npos || S[SuffixPos - 1] != ' ')
    return S;
  return S.substr(0, SuffixPos - 1);
}

template <class ELFT>
uint64_t ELFState<ELFT>::getSectionNameOffset(StringRef Name) {
  // If a section is excluded from section headers, we do not save its name in
  // the string table.
  if (ExcludedSectionHeaders.count(Name))
    return 0;
  return ShStrtabStrings->getOffset(Name);
}

static uint64_t writeContent(ContiguousBlobAccumulator &CBA,
                             const std::optional<yaml::BinaryRef> &Content,
                             const std::optional<llvm::yaml::Hex64> &Size) {
  size_t ContentSize = 0;
  if (Content) {
    CBA.writeAsBinary(*Content);
    ContentSize = Content->binary_size();
  }

  if (!Size)
    return ContentSize;

  CBA.writeZeros(*Size - ContentSize);
  return *Size;
}

static StringRef getDefaultLinkSec(unsigned SecType) {
  switch (SecType) {
  case ELF::SHT_REL:
  case ELF::SHT_RELA:
  case ELF::SHT_GROUP:
  case ELF::SHT_LLVM_CALL_GRAPH_PROFILE:
  case ELF::SHT_LLVM_ADDRSIG:
    return ".symtab";
  case ELF::SHT_GNU_versym:
  case ELF::SHT_HASH:
  case ELF::SHT_GNU_HASH:
    return ".dynsym";
  case ELF::SHT_DYNSYM:
  case ELF::SHT_GNU_verdef:
  case ELF::SHT_GNU_verneed:
    return ".dynstr";
  case ELF::SHT_SYMTAB:
    return ".strtab";
  default:
    return "";
  }
}

template <class ELFT>
void ELFState<ELFT>::initSectionHeaders(std::vector<Elf_Shdr> &SHeaders,
                                        ContiguousBlobAccumulator &CBA) {
  // Ensure SHN_UNDEF entry is present. An all-zero section header is a
  // valid SHN_UNDEF entry since SHT_NULL == 0.
  SHeaders.resize(Doc.getSections().size());

  for (const std::unique_ptr<ELFYAML::Chunk> &D : Doc.Chunks) {
    if (ELFYAML::Fill *S = dyn_cast<ELFYAML::Fill>(D.get())) {
      S->Offset = alignToOffset(CBA, /*Align=*/1, S->Offset);
      writeFill(*S, CBA);
      LocationCounter += S->Size;
      continue;
    }

    if (ELFYAML::SectionHeaderTable *S =
            dyn_cast<ELFYAML::SectionHeaderTable>(D.get())) {
      if (S->NoHeaders.value_or(false))
        continue;

      if (!S->Offset)
        S->Offset = alignToOffset(CBA, sizeof(typename ELFT::uint),
                                  /*Offset=*/std::nullopt);
      else
        S->Offset = alignToOffset(CBA, /*Align=*/1, S->Offset);

      uint64_t Size = S->getNumHeaders(SHeaders.size()) * sizeof(Elf_Shdr);
      // The full section header information might be not available here, so
      // fill the space with zeroes as a placeholder.
      CBA.writeZeros(Size);
      LocationCounter += Size;
      continue;
    }

    ELFYAML::Section *Sec = cast<ELFYAML::Section>(D.get());
    bool IsFirstUndefSection = Sec == Doc.getSections().front();
    if (IsFirstUndefSection && Sec->IsImplicit)
      continue;

    Elf_Shdr &SHeader = SHeaders[SN2I.get(Sec->Name)];
    if (Sec->Link) {
      SHeader.sh_link = toSectionIndex(*Sec->Link, Sec->Name);
    } else {
      StringRef LinkSec = getDefaultLinkSec(Sec->Type);
      unsigned Link = 0;
      if (!LinkSec.empty() && !ExcludedSectionHeaders.count(LinkSec) &&
          SN2I.lookup(LinkSec, Link))
        SHeader.sh_link = Link;
    }

    if (Sec->EntSize)
      SHeader.sh_entsize = *Sec->EntSize;
    else
      SHeader.sh_entsize = ELFYAML::getDefaultShEntSize<ELFT>(
          Doc.Header.Machine.value_or(ELF::EM_NONE), Sec->Type, Sec->Name);

    // We have a few sections like string or symbol tables that are usually
    // added implicitly to the end. However, if they are explicitly specified
    // in the YAML, we need to write them here. This ensures the file offset
    // remains correct.
    if (initImplicitHeader(CBA, SHeader, Sec->Name,
                           Sec->IsImplicit ? nullptr : Sec))
      continue;

    assert(Sec && "It can't be null unless it is an implicit section. But all "
                  "implicit sections should already have been handled above.");

    SHeader.sh_name =
        getSectionNameOffset(ELFYAML::dropUniqueSuffix(Sec->Name));
    SHeader.sh_type = Sec->Type;
    if (Sec->Flags)
      SHeader.sh_flags = *Sec->Flags;
    SHeader.sh_addralign = Sec->AddressAlign;

    // Set the offset for all sections, except the SHN_UNDEF section with index
    // 0 when not explicitly requested.
    if (!IsFirstUndefSection || Sec->Offset)
      SHeader.sh_offset = alignToOffset(CBA, SHeader.sh_addralign, Sec->Offset);

    assignSectionAddress(SHeader, Sec);

    if (IsFirstUndefSection) {
      if (auto RawSec = dyn_cast<ELFYAML::RawContentSection>(Sec)) {
        // We do not write any content for special SHN_UNDEF section.
        if (RawSec->Size)
          SHeader.sh_size = *RawSec->Size;
        if (RawSec->Info)
          SHeader.sh_info = *RawSec->Info;
      }

      LocationCounter += SHeader.sh_size;
      overrideFields<ELFT>(Sec, SHeader);
      continue;
    }

    if (!isa<ELFYAML::NoBitsSection>(Sec) && (Sec->Content || Sec->Size))
      SHeader.sh_size = writeContent(CBA, Sec->Content, Sec->Size);

    if (auto S = dyn_cast<ELFYAML::RawContentSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::SymtabShndxSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::RelocationSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::RelrSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::GroupSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::ARMIndexTableSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::MipsABIFlags>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::NoBitsSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::DynamicSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::SymverSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::VerneedSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::VerdefSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::StackSizesSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::HashSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::AddrsigSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::LinkerOptionsSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::NoteSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::GnuHashSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::DependentLibrariesSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::CallGraphProfileSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else if (auto S = dyn_cast<ELFYAML::BBAddrMapSection>(Sec)) {
      writeSectionContent(SHeader, *S, CBA);
    } else {
      llvm_unreachable("Unknown section type");
    }

    LocationCounter += SHeader.sh_size;

    // Override section fields if requested.
    overrideFields<ELFT>(Sec, SHeader);
  }
}

template <class ELFT>
void ELFState<ELFT>::assignSectionAddress(Elf_Shdr &SHeader,
                                          ELFYAML::Section *YAMLSec) {
  if (YAMLSec && YAMLSec->Address) {
    SHeader.sh_addr = *YAMLSec->Address;
    LocationCounter = *YAMLSec->Address;
    return;
  }

  // sh_addr represents the address in the memory image of a process. Sections
  // in a relocatable object file or non-allocatable sections do not need
  // sh_addr assignment.
  if (Doc.Header.Type.value == ELF::ET_REL ||
      !(SHeader.sh_flags & ELF::SHF_ALLOC))
    return;

  LocationCounter =
      alignTo(LocationCounter, SHeader.sh_addralign ? SHeader.sh_addralign : 1);
  SHeader.sh_addr = LocationCounter;
}

static size_t findFirstNonGlobal(ArrayRef<ELFYAML::Symbol> Symbols) {
  for (size_t I = 0; I < Symbols.size(); ++I)
    if (Symbols[I].Binding.value != ELF::STB_LOCAL)
      return I;
  return Symbols.size();
}

template <class ELFT>
std::vector<typename ELFT::Sym>
ELFState<ELFT>::toELFSymbols(ArrayRef<ELFYAML::Symbol> Symbols,
                             const StringTableBuilder &Strtab) {
  std::vector<Elf_Sym> Ret;
  Ret.resize(Symbols.size() + 1);

  size_t I = 0;
  for (const ELFYAML::Symbol &Sym : Symbols) {
    Elf_Sym &Symbol = Ret[++I];

    // If NameIndex, which contains the name offset, is explicitly specified, we
    // use it. This is useful for preparing broken objects. Otherwise, we add
    // the specified Name to the string table builder to get its offset.
    if (Sym.StName)
      Symbol.st_name = *Sym.StName;
    else if (!Sym.Name.empty())
      Symbol.st_name = Strtab.getOffset(ELFYAML::dropUniqueSuffix(Sym.Name));

    Symbol.setBindingAndType(Sym.Binding, Sym.Type);
    if (Sym.Section)
      Symbol.st_shndx = toSectionIndex(*Sym.Section, "", Sym.Name);
    else if (Sym.Index)
      Symbol.st_shndx = *Sym.Index;

    Symbol.st_value = Sym.Value.value_or(yaml::Hex64(0));
    Symbol.st_other = Sym.Other ? *Sym.Other : 0;
    Symbol.st_size = Sym.Size.value_or(yaml::Hex64(0));
  }

  return Ret;
}

template <class ELFT>
void ELFState<ELFT>::initSymtabSectionHeader(Elf_Shdr &SHeader,
                                             SymtabType STType,
                                             ContiguousBlobAccumulator &CBA,
                                             ELFYAML::Section *YAMLSec) {

  bool IsStatic = STType == SymtabType::Static;
  ArrayRef<ELFYAML::Symbol> Symbols;
  if (IsStatic && Doc.Symbols)
    Symbols = *Doc.Symbols;
  else if (!IsStatic && Doc.DynamicSymbols)
    Symbols = *Doc.DynamicSymbols;

  ELFYAML::RawContentSection *RawSec =
      dyn_cast_or_null<ELFYAML::RawContentSection>(YAMLSec);
  if (RawSec && (RawSec->Content || RawSec->Size)) {
    bool HasSymbolsDescription =
        (IsStatic && Doc.Symbols) || (!IsStatic && Doc.DynamicSymbols);
    if (HasSymbolsDescription) {
      StringRef Property = (IsStatic ? "`Symbols`" : "`DynamicSymbols`");
      if (RawSec->Content)
        reportError("cannot specify both `Content` and " + Property +
                    " for symbol table section '" + RawSec->Name + "'");
      if (RawSec->Size)
        reportError("cannot specify both `Size` and " + Property +
                    " for symbol table section '" + RawSec->Name + "'");
      return;
    }
  }

  SHeader.sh_name = getSectionNameOffset(IsStatic ? ".symtab" : ".dynsym");

  if (YAMLSec)
    SHeader.sh_type = YAMLSec->Type;
  else
    SHeader.sh_type = IsStatic ? ELF::SHT_SYMTAB : ELF::SHT_DYNSYM;

  if (YAMLSec && YAMLSec->Flags)
    SHeader.sh_flags = *YAMLSec->Flags;
  else if (!IsStatic)
    SHeader.sh_flags = ELF::SHF_ALLOC;

  // If the symbol table section is explicitly described in the YAML
  // then we should set the fields requested.
  SHeader.sh_info = (RawSec && RawSec->Info) ? (unsigned)(*RawSec->Info)
                                             : findFirstNonGlobal(Symbols) + 1;
  SHeader.sh_addralign = YAMLSec ? (uint64_t)YAMLSec->AddressAlign : 8;

  assignSectionAddress(SHeader, YAMLSec);

  SHeader.sh_offset = alignToOffset(CBA, SHeader.sh_addralign,
                                    RawSec ? RawSec->Offset : std::nullopt);

  if (RawSec && (RawSec->Content || RawSec->Size)) {
    assert(Symbols.empty());
    SHeader.sh_size = writeContent(CBA, RawSec->Content, RawSec->Size);
    return;
  }

  std::vector<Elf_Sym> Syms =
      toELFSymbols(Symbols, IsStatic ? DotStrtab : DotDynstr);
  SHeader.sh_size = Syms.size() * sizeof(Elf_Sym);
  CBA.write((const char *)Syms.data(), SHeader.sh_size);
}

template <class ELFT>
void ELFState<ELFT>::initStrtabSectionHeader(Elf_Shdr &SHeader, StringRef Name,
                                             StringTableBuilder &STB,
                                             ContiguousBlobAccumulator &CBA,
                                             ELFYAML::Section *YAMLSec) {
  SHeader.sh_name = getSectionNameOffset(ELFYAML::dropUniqueSuffix(Name));
  SHeader.sh_type = YAMLSec ? YAMLSec->Type : ELF::SHT_STRTAB;
  SHeader.sh_addralign = YAMLSec ? (uint64_t)YAMLSec->AddressAlign : 1;

  ELFYAML::RawContentSection *RawSec =
      dyn_cast_or_null<ELFYAML::RawContentSection>(YAMLSec);

  SHeader.sh_offset = alignToOffset(CBA, SHeader.sh_addralign,
                                    YAMLSec ? YAMLSec->Offset : std::nullopt);

  if (RawSec && (RawSec->Content || RawSec->Size)) {
    SHeader.sh_size = writeContent(CBA, RawSec->Content, RawSec->Size);
  } else {
    if (raw_ostream *OS = CBA.getRawOS(STB.getSize()))
      STB.write(*OS);
    SHeader.sh_size = STB.getSize();
  }

  if (RawSec && RawSec->Info)
    SHeader.sh_info = *RawSec->Info;

  if (YAMLSec && YAMLSec->Flags)
    SHeader.sh_flags = *YAMLSec->Flags;
  else if (Name == ".dynstr")
    SHeader.sh_flags = ELF::SHF_ALLOC;

  assignSectionAddress(SHeader, YAMLSec);
}

static bool shouldEmitDWARF(DWARFYAML::Data &DWARF, StringRef Name) {
  SetVector<StringRef> DebugSecNames = DWARF.getNonEmptySectionNames();
  return Name.consume_front(".") && DebugSecNames.count(Name);
}

template <class ELFT>
Expected<uint64_t> emitDWARF(typename ELFT::Shdr &SHeader, StringRef Name,
                             const DWARFYAML::Data &DWARF,
                             ContiguousBlobAccumulator &CBA) {
  // We are unable to predict the size of debug data, so we request to write 0
  // bytes. This should always return us an output stream unless CBA is already
  // in an error state.
  raw_ostream *OS = CBA.getRawOS(0);
  if (!OS)
    return 0;

  uint64_t BeginOffset = CBA.tell();

  auto EmitFunc = DWARFYAML::getDWARFEmitterByName(Name.substr(1));
  if (Error Err = EmitFunc(*OS, DWARF))
    return std::move(Err);

  return CBA.tell() - BeginOffset;
}

template <class ELFT>
void ELFState<ELFT>::initDWARFSectionHeader(Elf_Shdr &SHeader, StringRef Name,
                                            ContiguousBlobAccumulator &CBA,
                                            ELFYAML::Section *YAMLSec) {
  SHeader.sh_name = getSectionNameOffset(ELFYAML::dropUniqueSuffix(Name));
  SHeader.sh_type = YAMLSec ? YAMLSec->Type : ELF::SHT_PROGBITS;
  SHeader.sh_addralign = YAMLSec ? (uint64_t)YAMLSec->AddressAlign : 1;
  SHeader.sh_offset = alignToOffset(CBA, SHeader.sh_addralign,
                                    YAMLSec ? YAMLSec->Offset : std::nullopt);

  ELFYAML::RawContentSection *RawSec =
      dyn_cast_or_null<ELFYAML::RawContentSection>(YAMLSec);
  if (Doc.DWARF && shouldEmitDWARF(*Doc.DWARF, Name)) {
    if (RawSec && (RawSec->Content || RawSec->Size))
      reportError("cannot specify section '" + Name +
                  "' contents in the 'DWARF' entry and the 'Content' "
                  "or 'Size' in the 'Sections' entry at the same time");
    else {
      if (Expected<uint64_t> ShSizeOrErr =
              emitDWARF<ELFT>(SHeader, Name, *Doc.DWARF, CBA))
        SHeader.sh_size = *ShSizeOrErr;
      else
        reportError(ShSizeOrErr.takeError());
    }
  } else if (RawSec)
    SHeader.sh_size = writeContent(CBA, RawSec->Content, RawSec->Size);
  else
    llvm_unreachable("debug sections can only be initialized via the 'DWARF' "
                     "entry or a RawContentSection");

  if (RawSec && RawSec->Info)
    SHeader.sh_info = *RawSec->Info;

  if (YAMLSec && YAMLSec->Flags)
    SHeader.sh_flags = *YAMLSec->Flags;
  else if (Name == ".debug_str")
    SHeader.sh_flags = ELF::SHF_MERGE | ELF::SHF_STRINGS;

  assignSectionAddress(SHeader, YAMLSec);
}

template <class ELFT> void ELFState<ELFT>::reportError(const Twine &Msg) {
  ErrHandler(Msg);
  HasError = true;
}

template <class ELFT> void ELFState<ELFT>::reportError(Error Err) {
  handleAllErrors(std::move(Err), [&](const ErrorInfoBase &Err) {
    reportError(Err.message());
  });
}

template <class ELFT>
std::vector<Fragment>
ELFState<ELFT>::getPhdrFragments(const ELFYAML::ProgramHeader &Phdr,
                                 ArrayRef<Elf_Shdr> SHeaders) {
  std::vector<Fragment> Ret;
  for (const ELFYAML::Chunk *C : Phdr.Chunks) {
    if (const ELFYAML::Fill *F = dyn_cast<ELFYAML::Fill>(C)) {
      Ret.push_back({*F->Offset, F->Size, llvm::ELF::SHT_PROGBITS,
                     /*ShAddrAlign=*/1});
      continue;
    }

    const ELFYAML::Section *S = cast<ELFYAML::Section>(C);
    const Elf_Shdr &H = SHeaders[SN2I.get(S->Name)];
    Ret.push_back({H.sh_offset, H.sh_size, H.sh_type, H.sh_addralign});
  }
  return Ret;
}

template <class ELFT>
void ELFState<ELFT>::setProgramHeaderLayout(std::vector<Elf_Phdr> &PHeaders,
                                            std::vector<Elf_Shdr> &SHeaders) {
  uint32_t PhdrIdx = 0;
  for (auto &YamlPhdr : Doc.ProgramHeaders) {
    Elf_Phdr &PHeader = PHeaders[PhdrIdx++];
    std::vector<Fragment> Fragments = getPhdrFragments(YamlPhdr, SHeaders);
    if (!llvm::is_sorted(Fragments, [](const Fragment &A, const Fragment &B) {
          return A.Offset < B.Offset;
        }))
      reportError("sections in the program header with index " +
                  Twine(PhdrIdx) + " are not sorted by their file offset");

    if (YamlPhdr.Offset) {
      if (!Fragments.empty() && *YamlPhdr.Offset > Fragments.front().Offset)
        reportError("'Offset' for segment with index " + Twine(PhdrIdx) +
                    " must be less than or equal to the minimum file offset of "
                    "all included sections (0x" +
                    Twine::utohexstr(Fragments.front().Offset) + ")");
      PHeader.p_offset = *YamlPhdr.Offset;
    } else if (!Fragments.empty()) {
      PHeader.p_offset = Fragments.front().Offset;
    }

    // Set the file size if not set explicitly.
    if (YamlPhdr.FileSize) {
      PHeader.p_filesz = *YamlPhdr.FileSize;
    } else if (!Fragments.empty()) {
      uint64_t FileSize = Fragments.back().Offset - PHeader.p_offset;
      // SHT_NOBITS sections occupy no physical space in a file, we should not
      // take their sizes into account when calculating the file size of a
      // segment.
      if (Fragments.back().Type != llvm::ELF::SHT_NOBITS)
        FileSize += Fragments.back().Size;
      PHeader.p_filesz = FileSize;
    }

    // Find the maximum offset of the end of a section in order to set p_memsz.
    uint64_t MemOffset = PHeader.p_offset;
    for (const Fragment &F : Fragments)
      MemOffset = std::max(MemOffset, F.Offset + F.Size);
    // Set the memory size if not set explicitly.
    PHeader.p_memsz = YamlPhdr.MemSize ? uint64_t(*YamlPhdr.MemSize)
                                       : MemOffset - PHeader.p_offset;

    if (YamlPhdr.Align) {
      PHeader.p_align = *YamlPhdr.Align;
    } else {
      // Set the alignment of the segment to be the maximum alignment of the
      // sections so that by default the segment has a valid and sensible
      // alignment.
      PHeader.p_align = 1;
      for (const Fragment &F : Fragments)
        PHeader.p_align = std::max((uint64_t)PHeader.p_align, F.AddrAlign);
    }
  }
}

bool llvm::ELFYAML::shouldAllocateFileSpace(
    ArrayRef<ELFYAML::ProgramHeader> Phdrs, const ELFYAML::NoBitsSection &S) {
  for (const ELFYAML::ProgramHeader &PH : Phdrs) {
    auto It = llvm::find_if(
        PH.Chunks, [&](ELFYAML::Chunk *C) { return C->Name == S.Name; });
    if (std::any_of(It, PH.Chunks.end(), [](ELFYAML::Chunk *C) {
          return (isa<ELFYAML::Fill>(C) ||
                  cast<ELFYAML::Section>(C)->Type != ELF::SHT_NOBITS);
        }))
      return true;
  }
  return false;
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::NoBitsSection &S,
                                         ContiguousBlobAccumulator &CBA) {
  if (!S.Size)
    return;

  SHeader.sh_size = *S.Size;

  // When a nobits section is followed by a non-nobits section or fill
  // in the same segment, we allocate the file space for it. This behavior
  // matches linkers.
  if (shouldAllocateFileSpace(Doc.ProgramHeaders, S))
    CBA.writeZeros(*S.Size);
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(
    Elf_Shdr &SHeader, const ELFYAML::RawContentSection &Section,
    ContiguousBlobAccumulator &CBA) {
  if (Section.Info)
    SHeader.sh_info = *Section.Info;
}

static bool isMips64EL(const ELFYAML::Object &Obj) {
  return Obj.getMachine() == llvm::ELF::EM_MIPS &&
         Obj.Header.Class == ELFYAML::ELF_ELFCLASS(ELF::ELFCLASS64) &&
         Obj.Header.Data == ELFYAML::ELF_ELFDATA(ELF::ELFDATA2LSB);
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(
    Elf_Shdr &SHeader, const ELFYAML::RelocationSection &Section,
    ContiguousBlobAccumulator &CBA) {
  assert((Section.Type == llvm::ELF::SHT_REL ||
          Section.Type == llvm::ELF::SHT_RELA ||
          Section.Type == llvm::ELF::SHT_CREL) &&
         "Section type is not SHT_REL nor SHT_RELA");

  if (!Section.RelocatableSec.empty())
    SHeader.sh_info = toSectionIndex(Section.RelocatableSec, Section.Name);

  if (!Section.Relocations)
    return;

  const bool IsCrel = Section.Type == llvm::ELF::SHT_CREL;
  const bool IsRela = Section.Type == llvm::ELF::SHT_RELA;
  typename ELFT::uint OffsetMask = 8, Offset = 0, Addend = 0;
  uint32_t SymIdx = 0, Type = 0;
  uint64_t CurrentOffset = CBA.getOffset();
  if (IsCrel)
    for (const ELFYAML::Relocation &Rel : *Section.Relocations)
      OffsetMask |= Rel.Offset;
  const int Shift = llvm::countr_zero(OffsetMask);
  if (IsCrel)
    CBA.writeULEB128(Section.Relocations->size() * 8 + ELF::CREL_HDR_ADDEND +
                     Shift);
  for (const ELFYAML::Relocation &Rel : *Section.Relocations) {
    const bool IsDynamic = Section.Link && (*Section.Link == ".dynsym");
    uint32_t CurSymIdx =
        Rel.Symbol ? toSymbolIndex(*Rel.Symbol, Section.Name, IsDynamic) : 0;
    if (IsCrel) {
      // The delta offset and flags member may be larger than uint64_t. Special
      // case the first byte (3 flag bits and 4 offset bits). Other ULEB128
      // bytes encode the remaining delta offset bits.
      auto DeltaOffset =
          (static_cast<typename ELFT::uint>(Rel.Offset) - Offset) >> Shift;
      Offset = Rel.Offset;
      uint8_t B =
          DeltaOffset * 8 + (SymIdx != CurSymIdx) + (Type != Rel.Type ? 2 : 0) +
          (Addend != static_cast<typename ELFT::uint>(Rel.Addend) ? 4 : 0);
      if (DeltaOffset < 0x10) {
        CBA.write(B);
      } else {
        CBA.write(B | 0x80);
        CBA.writeULEB128(DeltaOffset >> 4);
      }
      // Delta symidx/type/addend members (SLEB128).
      if (B & 1) {
        CBA.writeSLEB128(
            std::make_signed_t<typename ELFT::uint>(CurSymIdx - SymIdx));
        SymIdx = CurSymIdx;
      }
      if (B & 2) {
        CBA.writeSLEB128(static_cast<int32_t>(Rel.Type - Type));
        Type = Rel.Type;
      }
      if (B & 4) {
        CBA.writeSLEB128(
            std::make_signed_t<typename ELFT::uint>(Rel.Addend - Addend));
        Addend = Rel.Addend;
      }
    } else if (IsRela) {
      Elf_Rela REntry;
      zero(REntry);
      REntry.r_offset = Rel.Offset;
      REntry.r_addend = Rel.Addend;
      REntry.setSymbolAndType(CurSymIdx, Rel.Type, isMips64EL(Doc));
      CBA.write((const char *)&REntry, sizeof(REntry));
    } else {
      Elf_Rel REntry;
      zero(REntry);
      REntry.r_offset = Rel.Offset;
      REntry.setSymbolAndType(CurSymIdx, Rel.Type, isMips64EL(Doc));
      CBA.write((const char *)&REntry, sizeof(REntry));
    }
  }

  SHeader.sh_size = CBA.getOffset() - CurrentOffset;
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::RelrSection &Section,
                                         ContiguousBlobAccumulator &CBA) {
  if (!Section.Entries)
    return;

  for (llvm::yaml::Hex64 E : *Section.Entries) {
    if (!ELFT::Is64Bits && E > UINT32_MAX)
      reportError(Section.Name + ": the value is too large for 32-bits: 0x" +
                  Twine::utohexstr(E));
    CBA.write<uintX_t>(E, ELFT::Endianness);
  }

  SHeader.sh_size = sizeof(uintX_t) * Section.Entries->size();
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(
    Elf_Shdr &SHeader, const ELFYAML::SymtabShndxSection &Shndx,
    ContiguousBlobAccumulator &CBA) {
  if (Shndx.Content || Shndx.Size) {
    SHeader.sh_size = writeContent(CBA, Shndx.Content, Shndx.Size);
    return;
  }

  if (!Shndx.Entries)
    return;

  for (uint32_t E : *Shndx.Entries)
    CBA.write<uint32_t>(E, ELFT::Endianness);
  SHeader.sh_size = Shndx.Entries->size() * SHeader.sh_entsize;
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::GroupSection &Section,
                                         ContiguousBlobAccumulator &CBA) {
  assert(Section.Type == llvm::ELF::SHT_GROUP &&
         "Section type is not SHT_GROUP");

  if (Section.Signature)
    SHeader.sh_info =
        toSymbolIndex(*Section.Signature, Section.Name, /*IsDynamic=*/false);

  if (!Section.Members)
    return;

  for (const ELFYAML::SectionOrType &Member : *Section.Members) {
    unsigned int SectionIndex = 0;
    if (Member.sectionNameOrType == "GRP_COMDAT")
      SectionIndex = llvm::ELF::GRP_COMDAT;
    else
      SectionIndex = toSectionIndex(Member.sectionNameOrType, Section.Name);
    CBA.write<uint32_t>(SectionIndex, ELFT::Endianness);
  }
  SHeader.sh_size = SHeader.sh_entsize * Section.Members->size();
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::SymverSection &Section,
                                         ContiguousBlobAccumulator &CBA) {
  if (!Section.Entries)
    return;

  for (uint16_t Version : *Section.Entries)
    CBA.write<uint16_t>(Version, ELFT::Endianness);
  SHeader.sh_size = Section.Entries->size() * SHeader.sh_entsize;
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(
    Elf_Shdr &SHeader, const ELFYAML::StackSizesSection &Section,
    ContiguousBlobAccumulator &CBA) {
  if (!Section.Entries)
    return;

  for (const ELFYAML::StackSizeEntry &E : *Section.Entries) {
    CBA.write<uintX_t>(E.Address, ELFT::Endianness);
    SHeader.sh_size += sizeof(uintX_t) + CBA.writeULEB128(E.Size);
  }
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(
    Elf_Shdr &SHeader, const ELFYAML::BBAddrMapSection &Section,
    ContiguousBlobAccumulator &CBA) {
  if (!Section.Entries) {
    if (Section.PGOAnalyses)
      WithColor::warning()
          << "PGOAnalyses should not exist in SHT_LLVM_BB_ADDR_MAP when "
             "Entries does not exist";
    return;
  }

  const std::vector<ELFYAML::PGOAnalysisMapEntry> *PGOAnalyses = nullptr;
  if (Section.PGOAnalyses) {
    if (Section.Entries->size() != Section.PGOAnalyses->size())
      WithColor::warning() << "PGOAnalyses must be the same length as Entries "
                              "in SHT_LLVM_BB_ADDR_MAP";
    else
      PGOAnalyses = &Section.PGOAnalyses.value();
  }

  for (const auto &[Idx, E] : llvm::enumerate(*Section.Entries)) {
    // Write version and feature values.
    if (Section.Type == llvm::ELF::SHT_LLVM_BB_ADDR_MAP) {
      if (E.Version > 2)
        WithColor::warning() << "unsupported SHT_LLVM_BB_ADDR_MAP version: "
                             << static_cast<int>(E.Version)
                             << "; encoding using the most recent version";
      CBA.write(E.Version);
      CBA.write(E.Feature);
      SHeader.sh_size += 2;
    }
    auto FeatureOrErr = llvm::object::BBAddrMap::Features::decode(E.Feature);
    bool MultiBBRangeFeatureEnabled = false;
    if (!FeatureOrErr)
      WithColor::warning() << toString(FeatureOrErr.takeError());
    else
      MultiBBRangeFeatureEnabled = FeatureOrErr->MultiBBRange;
    bool MultiBBRange =
        MultiBBRangeFeatureEnabled ||
        (E.NumBBRanges.has_value() && E.NumBBRanges.value() != 1) ||
        (E.BBRanges && E.BBRanges->size() != 1);
    if (MultiBBRange && !MultiBBRangeFeatureEnabled)
      WithColor::warning() << "feature value(" << E.Feature
                           << ") does not support multiple BB ranges.";
    if (MultiBBRange) {
      // Write the number of basic block ranges, which is overridden by the
      // 'NumBBRanges' field when specified.
      uint64_t NumBBRanges =
          E.NumBBRanges.value_or(E.BBRanges ? E.BBRanges->size() : 0);
      SHeader.sh_size += CBA.writeULEB128(NumBBRanges);
    }
    if (!E.BBRanges)
      continue;
    uint64_t TotalNumBlocks = 0;
    for (const ELFYAML::BBAddrMapEntry::BBRangeEntry &BBR : *E.BBRanges) {
      // Write the base address of the range.
      CBA.write<uintX_t>(BBR.BaseAddress, ELFT::Endianness);
      // Write number of BBEntries (number of basic blocks in this basic block
      // range). This is overridden by the 'NumBlocks' YAML field when
      // specified.
      uint64_t NumBlocks =
          BBR.NumBlocks.value_or(BBR.BBEntries ? BBR.BBEntries->size() : 0);
      SHeader.sh_size += sizeof(uintX_t) + CBA.writeULEB128(NumBlocks);
      // Write all BBEntries in this BBRange.
      if (!BBR.BBEntries)
        continue;
      for (const ELFYAML::BBAddrMapEntry::BBEntry &BBE : *BBR.BBEntries) {
        ++TotalNumBlocks;
        if (Section.Type == llvm::ELF::SHT_LLVM_BB_ADDR_MAP && E.Version > 1)
          SHeader.sh_size += CBA.writeULEB128(BBE.ID);
        SHeader.sh_size += CBA.writeULEB128(BBE.AddressOffset);
        SHeader.sh_size += CBA.writeULEB128(BBE.Size);
        SHeader.sh_size += CBA.writeULEB128(BBE.Metadata);
      }
    }
    if (!PGOAnalyses)
      continue;
    const ELFYAML::PGOAnalysisMapEntry &PGOEntry = PGOAnalyses->at(Idx);

    if (PGOEntry.FuncEntryCount)
      SHeader.sh_size += CBA.writeULEB128(*PGOEntry.FuncEntryCount);

    if (!PGOEntry.PGOBBEntries)
      continue;

    const auto &PGOBBEntries = PGOEntry.PGOBBEntries.value();
    if (TotalNumBlocks != PGOBBEntries.size()) {
      WithColor::warning() << "PBOBBEntries must be the same length as "
                              "BBEntries in SHT_LLVM_BB_ADDR_MAP.\n"
                           << "Mismatch on function with address: "
                           << E.getFunctionAddress();
      continue;
    }

    for (const auto &PGOBBE : PGOBBEntries) {
      if (PGOBBE.BBFreq)
        SHeader.sh_size += CBA.writeULEB128(*PGOBBE.BBFreq);
      if (PGOBBE.Successors) {
        SHeader.sh_size += CBA.writeULEB128(PGOBBE.Successors->size());
        for (const auto &[ID, BrProb] : *PGOBBE.Successors) {
          SHeader.sh_size += CBA.writeULEB128(ID);
          SHeader.sh_size += CBA.writeULEB128(BrProb);
        }
      }
    }
  }
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(
    Elf_Shdr &SHeader, const ELFYAML::LinkerOptionsSection &Section,
    ContiguousBlobAccumulator &CBA) {
  if (!Section.Options)
    return;

  for (const ELFYAML::LinkerOption &LO : *Section.Options) {
    CBA.write(LO.Key.data(), LO.Key.size());
    CBA.write('\0');
    CBA.write(LO.Value.data(), LO.Value.size());
    CBA.write('\0');
    SHeader.sh_size += (LO.Key.size() + LO.Value.size() + 2);
  }
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(
    Elf_Shdr &SHeader, const ELFYAML::DependentLibrariesSection &Section,
    ContiguousBlobAccumulator &CBA) {
  if (!Section.Libs)
    return;

  for (StringRef Lib : *Section.Libs) {
    CBA.write(Lib.data(), Lib.size());
    CBA.write('\0');
    SHeader.sh_size += Lib.size() + 1;
  }
}

template <class ELFT>
uint64_t
ELFState<ELFT>::alignToOffset(ContiguousBlobAccumulator &CBA, uint64_t Align,
                              std::optional<llvm::yaml::Hex64> Offset) {
  uint64_t CurrentOffset = CBA.getOffset();
  uint64_t AlignedOffset;

  if (Offset) {
    if ((uint64_t)*Offset < CurrentOffset) {
      reportError("the 'Offset' value (0x" +
                  Twine::utohexstr((uint64_t)*Offset) + ") goes backward");
      return CurrentOffset;
    }

    // We ignore an alignment when an explicit offset has been requested.
    AlignedOffset = *Offset;
  } else {
    AlignedOffset = alignTo(CurrentOffset, std::max(Align, (uint64_t)1));
  }

  CBA.writeZeros(AlignedOffset - CurrentOffset);
  return AlignedOffset;
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(
    Elf_Shdr &SHeader, const ELFYAML::CallGraphProfileSection &Section,
    ContiguousBlobAccumulator &CBA) {
  if (!Section.Entries)
    return;

  for (const ELFYAML::CallGraphEntryWeight &E : *Section.Entries) {
    CBA.write<uint64_t>(E.Weight, ELFT::Endianness);
    SHeader.sh_size += sizeof(object::Elf_CGProfile_Impl<ELFT>);
  }
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::HashSection &Section,
                                         ContiguousBlobAccumulator &CBA) {
  if (!Section.Bucket)
    return;

  CBA.write<uint32_t>(
      Section.NBucket.value_or(llvm::yaml::Hex64(Section.Bucket->size())),
      ELFT::Endianness);
  CBA.write<uint32_t>(
      Section.NChain.value_or(llvm::yaml::Hex64(Section.Chain->size())),
      ELFT::Endianness);

  for (uint32_t Val : *Section.Bucket)
    CBA.write<uint32_t>(Val, ELFT::Endianness);
  for (uint32_t Val : *Section.Chain)
    CBA.write<uint32_t>(Val, ELFT::Endianness);

  SHeader.sh_size = (2 + Section.Bucket->size() + Section.Chain->size()) * 4;
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::VerdefSection &Section,
                                         ContiguousBlobAccumulator &CBA) {

  if (Section.Info)
    SHeader.sh_info = *Section.Info;
  else if (Section.Entries)
    SHeader.sh_info = Section.Entries->size();

  if (!Section.Entries)
    return;

  uint64_t AuxCnt = 0;
  for (size_t I = 0; I < Section.Entries->size(); ++I) {
    const ELFYAML::VerdefEntry &E = (*Section.Entries)[I];

    Elf_Verdef VerDef;
    VerDef.vd_version = E.Version.value_or(1);
    VerDef.vd_flags = E.Flags.value_or(0);
    VerDef.vd_ndx = E.VersionNdx.value_or(0);
    VerDef.vd_hash = E.Hash.value_or(0);
    VerDef.vd_aux = sizeof(Elf_Verdef);
    VerDef.vd_cnt = E.VerNames.size();
    if (I == Section.Entries->size() - 1)
      VerDef.vd_next = 0;
    else
      VerDef.vd_next =
          sizeof(Elf_Verdef) + E.VerNames.size() * sizeof(Elf_Verdaux);
    CBA.write((const char *)&VerDef, sizeof(Elf_Verdef));

    for (size_t J = 0; J < E.VerNames.size(); ++J, ++AuxCnt) {
      Elf_Verdaux VernAux;
      VernAux.vda_name = DotDynstr.getOffset(E.VerNames[J]);
      if (J == E.VerNames.size() - 1)
        VernAux.vda_next = 0;
      else
        VernAux.vda_next = sizeof(Elf_Verdaux);
      CBA.write((const char *)&VernAux, sizeof(Elf_Verdaux));
    }
  }

  SHeader.sh_size = Section.Entries->size() * sizeof(Elf_Verdef) +
                    AuxCnt * sizeof(Elf_Verdaux);
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::VerneedSection &Section,
                                         ContiguousBlobAccumulator &CBA) {
  if (Section.Info)
    SHeader.sh_info = *Section.Info;
  else if (Section.VerneedV)
    SHeader.sh_info = Section.VerneedV->size();

  if (!Section.VerneedV)
    return;

  uint64_t AuxCnt = 0;
  for (size_t I = 0; I < Section.VerneedV->size(); ++I) {
    const ELFYAML::VerneedEntry &VE = (*Section.VerneedV)[I];

    Elf_Verneed VerNeed;
    VerNeed.vn_version = VE.Version;
    VerNeed.vn_file = DotDynstr.getOffset(VE.File);
    if (I == Section.VerneedV->size() - 1)
      VerNeed.vn_next = 0;
    else
      VerNeed.vn_next =
          sizeof(Elf_Verneed) + VE.AuxV.size() * sizeof(Elf_Vernaux);
    VerNeed.vn_cnt = VE.AuxV.size();
    VerNeed.vn_aux = sizeof(Elf_Verneed);
    CBA.write((const char *)&VerNeed, sizeof(Elf_Verneed));

    for (size_t J = 0; J < VE.AuxV.size(); ++J, ++AuxCnt) {
      const ELFYAML::VernauxEntry &VAuxE = VE.AuxV[J];

      Elf_Vernaux VernAux;
      VernAux.vna_hash = VAuxE.Hash;
      VernAux.vna_flags = VAuxE.Flags;
      VernAux.vna_other = VAuxE.Other;
      VernAux.vna_name = DotDynstr.getOffset(VAuxE.Name);
      if (J == VE.AuxV.size() - 1)
        VernAux.vna_next = 0;
      else
        VernAux.vna_next = sizeof(Elf_Vernaux);
      CBA.write((const char *)&VernAux, sizeof(Elf_Vernaux));
    }
  }

  SHeader.sh_size = Section.VerneedV->size() * sizeof(Elf_Verneed) +
                    AuxCnt * sizeof(Elf_Vernaux);
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(
    Elf_Shdr &SHeader, const ELFYAML::ARMIndexTableSection &Section,
    ContiguousBlobAccumulator &CBA) {
  if (!Section.Entries)
    return;

  for (const ELFYAML::ARMIndexTableEntry &E : *Section.Entries) {
    CBA.write<uint32_t>(E.Offset, ELFT::Endianness);
    CBA.write<uint32_t>(E.Value, ELFT::Endianness);
  }
  SHeader.sh_size = Section.Entries->size() * 8;
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::MipsABIFlags &Section,
                                         ContiguousBlobAccumulator &CBA) {
  assert(Section.Type == llvm::ELF::SHT_MIPS_ABIFLAGS &&
         "Section type is not SHT_MIPS_ABIFLAGS");

  object::Elf_Mips_ABIFlags<ELFT> Flags;
  zero(Flags);
  SHeader.sh_size = SHeader.sh_entsize;

  Flags.version = Section.Version;
  Flags.isa_level = Section.ISALevel;
  Flags.isa_rev = Section.ISARevision;
  Flags.gpr_size = Section.GPRSize;
  Flags.cpr1_size = Section.CPR1Size;
  Flags.cpr2_size = Section.CPR2Size;
  Flags.fp_abi = Section.FpABI;
  Flags.isa_ext = Section.ISAExtension;
  Flags.ases = Section.ASEs;
  Flags.flags1 = Section.Flags1;
  Flags.flags2 = Section.Flags2;
  CBA.write((const char *)&Flags, sizeof(Flags));
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::DynamicSection &Section,
                                         ContiguousBlobAccumulator &CBA) {
  assert(Section.Type == llvm::ELF::SHT_DYNAMIC &&
         "Section type is not SHT_DYNAMIC");

  if (!Section.Entries)
    return;

  for (const ELFYAML::DynamicEntry &DE : *Section.Entries) {
    CBA.write<uintX_t>(DE.Tag, ELFT::Endianness);
    CBA.write<uintX_t>(DE.Val, ELFT::Endianness);
  }
  SHeader.sh_size = 2 * sizeof(uintX_t) * Section.Entries->size();
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::AddrsigSection &Section,
                                         ContiguousBlobAccumulator &CBA) {
  if (!Section.Symbols)
    return;

  for (StringRef Sym : *Section.Symbols)
    SHeader.sh_size +=
        CBA.writeULEB128(toSymbolIndex(Sym, Section.Name, /*IsDynamic=*/false));
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::NoteSection &Section,
                                         ContiguousBlobAccumulator &CBA) {
  if (!Section.Notes)
    return;

  uint64_t Offset = CBA.tell();
  for (const ELFYAML::NoteEntry &NE : *Section.Notes) {
    // Write name size.
    if (NE.Name.empty())
      CBA.write<uint32_t>(0, ELFT::Endianness);
    else
      CBA.write<uint32_t>(NE.Name.size() + 1, ELFT::Endianness);

    // Write description size.
    if (NE.Desc.binary_size() == 0)
      CBA.write<uint32_t>(0, ELFT::Endianness);
    else
      CBA.write<uint32_t>(NE.Desc.binary_size(), ELFT::Endianness);

    // Write type.
    CBA.write<uint32_t>(NE.Type, ELFT::Endianness);

    // Write name, null terminator and padding.
    if (!NE.Name.empty()) {
      CBA.write(NE.Name.data(), NE.Name.size());
      CBA.write('\0');
      CBA.padToAlignment(4);
    }

    // Write description and padding.
    if (NE.Desc.binary_size() != 0) {
      CBA.writeAsBinary(NE.Desc);
      CBA.padToAlignment(4);
    }
  }

  SHeader.sh_size = CBA.tell() - Offset;
}

template <class ELFT>
void ELFState<ELFT>::writeSectionContent(Elf_Shdr &SHeader,
                                         const ELFYAML::GnuHashSection &Section,
                                         ContiguousBlobAccumulator &CBA) {
  if (!Section.HashBuckets)
    return;

  if (!Section.Header)
    return;

  // We write the header first, starting with the hash buckets count. Normally
  // it is the number of entries in HashBuckets, but the "NBuckets" property can
  // be used to override this field, which is useful for producing broken
  // objects.
  if (Section.Header->NBuckets)
    CBA.write<uint32_t>(*Section.Header->NBuckets, ELFT::Endianness);
  else
    CBA.write<uint32_t>(Section.HashBuckets->size(), ELFT::Endianness);

  // Write the index of the first symbol in the dynamic symbol table accessible
  // via the hash table.
  CBA.write<uint32_t>(Section.Header->SymNdx, ELFT::Endianness);

  // Write the number of words in the Bloom filter. As above, the "MaskWords"
  // property can be used to set this field to any value.
  if (Section.Header->MaskWords)
    CBA.write<uint32_t>(*Section.Header->MaskWords, ELFT::Endianness);
  else
    CBA.write<uint32_t>(Section.BloomFilter->size(), ELFT::Endianness);

  // Write the shift constant used by the Bloom filter.
  CBA.write<uint32_t>(Section.Header->Shift2, ELFT::Endianness);

  // We've finished writing the header. Now write the Bloom filter.
  for (llvm::yaml::Hex64 Val : *Section.BloomFilter)
    CBA.write<uintX_t>(Val, ELFT::Endianness);

  // Write an array of hash buckets.
  for (llvm::yaml::Hex32 Val : *Section.HashBuckets)
    CBA.write<uint32_t>(Val, ELFT::Endianness);

  // Write an array of hash values.
  for (llvm::yaml::Hex32 Val : *Section.HashValues)
    CBA.write<uint32_t>(Val, ELFT::Endianness);

  SHeader.sh_size = 16 /*Header size*/ +
                    Section.BloomFilter->size() * sizeof(typename ELFT::uint) +
                    Section.HashBuckets->size() * 4 +
                    Section.HashValues->size() * 4;
}

template <class ELFT>
void ELFState<ELFT>::writeFill(ELFYAML::Fill &Fill,
                               ContiguousBlobAccumulator &CBA) {
  size_t PatternSize = Fill.Pattern ? Fill.Pattern->binary_size() : 0;
  if (!PatternSize) {
    CBA.writeZeros(Fill.Size);
    return;
  }

  // Fill the content with the specified pattern.
  uint64_t Written = 0;
  for (; Written + PatternSize <= Fill.Size; Written += PatternSize)
    CBA.writeAsBinary(*Fill.Pattern);
  CBA.writeAsBinary(*Fill.Pattern, Fill.Size - Written);
}

template <class ELFT>
DenseMap<StringRef, size_t> ELFState<ELFT>::buildSectionHeaderReorderMap() {
  const ELFYAML::SectionHeaderTable &SectionHeaders =
      Doc.getSectionHeaderTable();
  if (SectionHeaders.IsImplicit || SectionHeaders.NoHeaders ||
      SectionHeaders.isDefault())
    return DenseMap<StringRef, size_t>();

  DenseMap<StringRef, size_t> Ret;
  size_t SecNdx = 0;
  StringSet<> Seen;

  auto AddSection = [&](const ELFYAML::SectionHeader &Hdr) {
    if (!Ret.try_emplace(Hdr.Name, ++SecNdx).second)
      reportError("repeated section name: '" + Hdr.Name +
                  "' in the section header description");
    Seen.insert(Hdr.Name);
  };

  if (SectionHeaders.Sections)
    for (const ELFYAML::SectionHeader &Hdr : *SectionHeaders.Sections)
      AddSection(Hdr);

  if (SectionHeaders.Excluded)
    for (const ELFYAML::SectionHeader &Hdr : *SectionHeaders.Excluded)
      AddSection(Hdr);

  for (const ELFYAML::Section *S : Doc.getSections()) {
    // Ignore special first SHT_NULL section.
    if (S == Doc.getSections().front())
      continue;
    if (!Seen.count(S->Name))
      reportError("section '" + S->Name +
                  "' should be present in the 'Sections' or 'Excluded' lists");
    Seen.erase(S->Name);
  }

  for (const auto &It : Seen)
    reportError("section header contains undefined section '" + It.getKey() +
                "'");
  return Ret;
}

template <class ELFT> void ELFState<ELFT>::buildSectionIndex() {
  // A YAML description can have an explicit section header declaration that
  // allows to change the order of section headers.
  DenseMap<StringRef, size_t> ReorderMap = buildSectionHeaderReorderMap();

  if (HasError)
    return;

  // Build excluded section headers map.
  std::vector<ELFYAML::Section *> Sections = Doc.getSections();
  const ELFYAML::SectionHeaderTable &SectionHeaders =
      Doc.getSectionHeaderTable();
  if (SectionHeaders.Excluded)
    for (const ELFYAML::SectionHeader &Hdr : *SectionHeaders.Excluded)
      if (!ExcludedSectionHeaders.insert(Hdr.Name).second)
        llvm_unreachable("buildSectionIndex() failed");

  if (SectionHeaders.NoHeaders.value_or(false))
    for (const ELFYAML::Section *S : Sections)
      if (!ExcludedSectionHeaders.insert(S->Name).second)
        llvm_unreachable("buildSectionIndex() failed");

  size_t SecNdx = -1;
  for (const ELFYAML::Section *S : Sections) {
    ++SecNdx;

    size_t Index = ReorderMap.empty() ? SecNdx : ReorderMap.lookup(S->Name);
    if (!SN2I.addName(S->Name, Index))
      llvm_unreachable("buildSectionIndex() failed");

    if (!ExcludedSectionHeaders.count(S->Name))
      ShStrtabStrings->add(ELFYAML::dropUniqueSuffix(S->Name));
  }
}

template <class ELFT> void ELFState<ELFT>::buildSymbolIndexes() {
  auto Build = [this](ArrayRef<ELFYAML::Symbol> V, NameToIdxMap &Map) {
    for (size_t I = 0, S = V.size(); I < S; ++I) {
      const ELFYAML::Symbol &Sym = V[I];
      if (!Sym.Name.empty() && !Map.addName(Sym.Name, I + 1))
        reportError("repeated symbol name: '" + Sym.Name + "'");
    }
  };

  if (Doc.Symbols)
    Build(*Doc.Symbols, SymN2I);
  if (Doc.DynamicSymbols)
    Build(*Doc.DynamicSymbols, DynSymN2I);
}

template <class ELFT> void ELFState<ELFT>::finalizeStrings() {
  // Add the regular symbol names to .strtab section.
  if (Doc.Symbols)
    for (const ELFYAML::Symbol &Sym : *Doc.Symbols)
      DotStrtab.add(ELFYAML::dropUniqueSuffix(Sym.Name));
  DotStrtab.finalize();

  // Add the dynamic symbol names to .dynstr section.
  if (Doc.DynamicSymbols)
    for (const ELFYAML::Symbol &Sym : *Doc.DynamicSymbols)
      DotDynstr.add(ELFYAML::dropUniqueSuffix(Sym.Name));

  // SHT_GNU_verdef and SHT_GNU_verneed sections might also
  // add strings to .dynstr section.
  for (const ELFYAML::Chunk *Sec : Doc.getSections()) {
    if (auto VerNeed = dyn_cast<ELFYAML::VerneedSection>(Sec)) {
      if (VerNeed->VerneedV) {
        for (const ELFYAML::VerneedEntry &VE : *VerNeed->VerneedV) {
          DotDynstr.add(VE.File);
          for (const ELFYAML::VernauxEntry &Aux : VE.AuxV)
            DotDynstr.add(Aux.Name);
        }
      }
    } else if (auto VerDef = dyn_cast<ELFYAML::VerdefSection>(Sec)) {
      if (VerDef->Entries)
        for (const ELFYAML::VerdefEntry &E : *VerDef->Entries)
          for (StringRef Name : E.VerNames)
            DotDynstr.add(Name);
    }
  }

  DotDynstr.finalize();

  // Don't finalize the section header string table a second time if it has
  // already been finalized due to being one of the symbol string tables.
  if (ShStrtabStrings != &DotStrtab && ShStrtabStrings != &DotDynstr)
    ShStrtabStrings->finalize();
}

template <class ELFT>
bool ELFState<ELFT>::writeELF(raw_ostream &OS, ELFYAML::Object &Doc,
                              yaml::ErrorHandler EH, uint64_t MaxSize) {
  ELFState<ELFT> State(Doc, EH);
  if (State.HasError)
    return false;

  // Build the section index, which adds sections to the section header string
  // table first, so that we can finalize the section header string table.
  State.buildSectionIndex();
  State.buildSymbolIndexes();

  // Finalize section header string table and the .strtab and .dynstr sections.
  // We do this early because we want to finalize the string table builders
  // before writing the content of the sections that might want to use them.
  State.finalizeStrings();

  if (State.HasError)
    return false;

  std::vector<Elf_Phdr> PHeaders;
  State.initProgramHeaders(PHeaders);

  // XXX: This offset is tightly coupled with the order that we write
  // things to `OS`.
  const size_t SectionContentBeginOffset =
      sizeof(Elf_Ehdr) + sizeof(Elf_Phdr) * Doc.ProgramHeaders.size();
  // It is quite easy to accidentally create output with yaml2obj that is larger
  // than intended, for example, due to an issue in the YAML description.
  // We limit the maximum allowed output size, but also provide a command line
  // option to change this limitation.
  ContiguousBlobAccumulator CBA(SectionContentBeginOffset, MaxSize);

  std::vector<Elf_Shdr> SHeaders;
  State.initSectionHeaders(SHeaders, CBA);

  // Now we can decide segment offsets.
  State.setProgramHeaderLayout(PHeaders, SHeaders);

  bool ReachedLimit = CBA.getOffset() > MaxSize;
  if (Error E = CBA.takeLimitError()) {
    // We report a custom error message instead below.
    consumeError(std::move(E));
    ReachedLimit = true;
  }

  if (ReachedLimit)
    State.reportError(
        "the desired output size is greater than permitted. Use the "
        "--max-size option to change the limit");

  if (State.HasError)
    return false;

  State.writeELFHeader(OS);
  writeArrayData(OS, ArrayRef(PHeaders));

  const ELFYAML::SectionHeaderTable &SHT = Doc.getSectionHeaderTable();
  if (!SHT.NoHeaders.value_or(false))
    CBA.updateDataAt(*SHT.Offset, SHeaders.data(),
                     SHT.getNumHeaders(SHeaders.size()) * sizeof(Elf_Shdr));

  CBA.writeBlobToStream(OS);
  return true;
}

namespace llvm {
namespace yaml {

bool yaml2elf(llvm::ELFYAML::Object &Doc, raw_ostream &Out, ErrorHandler EH,
              uint64_t MaxSize) {
  bool IsLE = Doc.Header.Data == ELFYAML::ELF_ELFDATA(ELF::ELFDATA2LSB);
  bool Is64Bit = Doc.Header.Class == ELFYAML::ELF_ELFCLASS(ELF::ELFCLASS64);
  if (Is64Bit) {
    if (IsLE)
      return ELFState<object::ELF64LE>::writeELF(Out, Doc, EH, MaxSize);
    return ELFState<object::ELF64BE>::writeELF(Out, Doc, EH, MaxSize);
  }
  if (IsLE)
    return ELFState<object::ELF32LE>::writeELF(Out, Doc, EH, MaxSize);
  return ELFState<object::ELF32BE>::writeELF(Out, Doc, EH, MaxSize);
}

} // namespace yaml
} // namespace llvm
