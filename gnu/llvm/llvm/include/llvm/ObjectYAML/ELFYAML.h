//===- ELFYAML.h - ELF YAMLIO implementation --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation
/// of ELF.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_ELFYAML_H
#define LLVM_OBJECTYAML_ELFYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Object/ELFTypes.h"
#include "llvm/ObjectYAML/DWARFYAML.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/YAMLTraits.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace llvm {
namespace ELFYAML {

StringRef dropUniqueSuffix(StringRef S);
std::string appendUniqueSuffix(StringRef Name, const Twine& Msg);

// These types are invariant across 32/64-bit ELF, so for simplicity just
// directly give them their exact sizes. We don't need to worry about
// endianness because these are just the types in the YAMLIO structures,
// and are appropriately converted to the necessary endianness when
// reading/generating binary object files.
// The naming of these types is intended to be ELF_PREFIX, where PREFIX is
// the common prefix of the respective constants. E.g. ELF_EM corresponds
// to the `e_machine` constants, like `EM_X86_64`.
// In the future, these would probably be better suited by C++11 enum
// class's with appropriate fixed underlying type.
LLVM_YAML_STRONG_TYPEDEF(uint16_t, ELF_ET)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, ELF_PT)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, ELF_EM)
LLVM_YAML_STRONG_TYPEDEF(uint8_t, ELF_ELFCLASS)
LLVM_YAML_STRONG_TYPEDEF(uint8_t, ELF_ELFDATA)
LLVM_YAML_STRONG_TYPEDEF(uint8_t, ELF_ELFOSABI)
// Just use 64, since it can hold 32-bit values too.
LLVM_YAML_STRONG_TYPEDEF(uint64_t, ELF_EF)
// Just use 64, since it can hold 32-bit values too.
LLVM_YAML_STRONG_TYPEDEF(uint64_t, ELF_DYNTAG)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, ELF_PF)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, ELF_SHT)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, ELF_REL)
LLVM_YAML_STRONG_TYPEDEF(uint8_t, ELF_RSS)
// Just use 64, since it can hold 32-bit values too.
LLVM_YAML_STRONG_TYPEDEF(uint64_t, ELF_SHF)
LLVM_YAML_STRONG_TYPEDEF(uint16_t, ELF_SHN)
LLVM_YAML_STRONG_TYPEDEF(uint8_t, ELF_STB)
LLVM_YAML_STRONG_TYPEDEF(uint8_t, ELF_STT)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, ELF_NT)

LLVM_YAML_STRONG_TYPEDEF(uint8_t, MIPS_AFL_REG)
LLVM_YAML_STRONG_TYPEDEF(uint8_t, MIPS_ABI_FP)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, MIPS_AFL_EXT)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, MIPS_AFL_ASE)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, MIPS_AFL_FLAGS1)
LLVM_YAML_STRONG_TYPEDEF(uint32_t, MIPS_ISA)

LLVM_YAML_STRONG_TYPEDEF(StringRef, YAMLFlowString)
LLVM_YAML_STRONG_TYPEDEF(int64_t, YAMLIntUInt)

template <class ELFT>
unsigned getDefaultShEntSize(unsigned EMachine, ELF_SHT SecType,
                             StringRef SecName) {
  if (EMachine == ELF::EM_MIPS && SecType == ELF::SHT_MIPS_ABIFLAGS)
    return sizeof(object::Elf_Mips_ABIFlags<ELFT>);

  switch (SecType) {
  case ELF::SHT_SYMTAB:
  case ELF::SHT_DYNSYM:
    return sizeof(typename ELFT::Sym);
  case ELF::SHT_GROUP:
    return sizeof(typename ELFT::Word);
  case ELF::SHT_REL:
    return sizeof(typename ELFT::Rel);
  case ELF::SHT_RELA:
    return sizeof(typename ELFT::Rela);
  case ELF::SHT_RELR:
    return sizeof(typename ELFT::Relr);
  case ELF::SHT_DYNAMIC:
    return sizeof(typename ELFT::Dyn);
  case ELF::SHT_HASH:
    return sizeof(typename ELFT::Word);
  case ELF::SHT_SYMTAB_SHNDX:
    return sizeof(typename ELFT::Word);
  case ELF::SHT_GNU_versym:
    return sizeof(typename ELFT::Half);
  case ELF::SHT_LLVM_CALL_GRAPH_PROFILE:
    return sizeof(object::Elf_CGProfile_Impl<ELFT>);
  default:
    if (SecName == ".debug_str")
      return 1;
    return 0;
  }
}

// For now, hardcode 64 bits everywhere that 32 or 64 would be needed
// since 64-bit can hold 32-bit values too.
struct FileHeader {
  ELF_ELFCLASS Class;
  ELF_ELFDATA Data;
  ELF_ELFOSABI OSABI;
  llvm::yaml::Hex8 ABIVersion;
  ELF_ET Type;
  std::optional<ELF_EM> Machine;
  ELF_EF Flags;
  llvm::yaml::Hex64 Entry;
  std::optional<StringRef> SectionHeaderStringTable;

  std::optional<llvm::yaml::Hex64> EPhOff;
  std::optional<llvm::yaml::Hex16> EPhEntSize;
  std::optional<llvm::yaml::Hex16> EPhNum;
  std::optional<llvm::yaml::Hex16> EShEntSize;
  std::optional<llvm::yaml::Hex64> EShOff;
  std::optional<llvm::yaml::Hex16> EShNum;
  std::optional<llvm::yaml::Hex16> EShStrNdx;
};

struct SectionHeader {
  StringRef Name;
};

struct Symbol {
  StringRef Name;
  ELF_STT Type;
  std::optional<StringRef> Section;
  std::optional<ELF_SHN> Index;
  ELF_STB Binding;
  std::optional<llvm::yaml::Hex64> Value;
  std::optional<llvm::yaml::Hex64> Size;
  std::optional<uint8_t> Other;

  std::optional<uint32_t> StName;
};

struct SectionOrType {
  StringRef sectionNameOrType;
};

struct DynamicEntry {
  ELF_DYNTAG Tag;
  llvm::yaml::Hex64 Val;
};

struct BBAddrMapEntry {
  struct BBEntry {
    uint32_t ID;
    llvm::yaml::Hex64 AddressOffset;
    llvm::yaml::Hex64 Size;
    llvm::yaml::Hex64 Metadata;
  };
  uint8_t Version;
  llvm::yaml::Hex8 Feature;

  struct BBRangeEntry {
    llvm::yaml::Hex64 BaseAddress;
    std::optional<uint64_t> NumBlocks;
    std::optional<std::vector<BBEntry>> BBEntries;
  };

  std::optional<uint64_t> NumBBRanges;
  std::optional<std::vector<BBRangeEntry>> BBRanges;

  llvm::yaml::Hex64 getFunctionAddress() const {
    if (!BBRanges || BBRanges->empty())
      return 0;
    return BBRanges->front().BaseAddress;
  }
};

struct PGOAnalysisMapEntry {
  struct PGOBBEntry {
    struct SuccessorEntry {
      uint32_t ID;
      llvm::yaml::Hex32 BrProb;
    };
    std::optional<uint64_t> BBFreq;
    std::optional<std::vector<SuccessorEntry>> Successors;
  };
  std::optional<uint64_t> FuncEntryCount;
  std::optional<std::vector<PGOBBEntry>> PGOBBEntries;
};

struct StackSizeEntry {
  llvm::yaml::Hex64 Address;
  llvm::yaml::Hex64 Size;
};

struct NoteEntry {
  StringRef Name;
  yaml::BinaryRef Desc;
  ELF_NT Type;
};

struct Chunk {
  enum class ChunkKind {
    Dynamic,
    Group,
    RawContent,
    Relocation,
    Relr,
    NoBits,
    Note,
    Hash,
    GnuHash,
    Verdef,
    Verneed,
    StackSizes,
    SymtabShndxSection,
    Symver,
    ARMIndexTable,
    MipsABIFlags,
    Addrsig,
    LinkerOptions,
    DependentLibraries,
    CallGraphProfile,
    BBAddrMap,

    // Special chunks.
    SpecialChunksStart,
    Fill = SpecialChunksStart,
    SectionHeaderTable,
  };

  ChunkKind Kind;
  StringRef Name;
  std::optional<llvm::yaml::Hex64> Offset;

  // Usually chunks are not created implicitly, but rather loaded from YAML.
  // This flag is used to signal whether this is the case or not.
  bool IsImplicit;

  Chunk(ChunkKind K, bool Implicit) : Kind(K), IsImplicit(Implicit) {}
  virtual ~Chunk();
};

struct Section : public Chunk {
  ELF_SHT Type;
  std::optional<ELF_SHF> Flags;
  std::optional<llvm::yaml::Hex64> Address;
  std::optional<StringRef> Link;
  llvm::yaml::Hex64 AddressAlign;
  std::optional<llvm::yaml::Hex64> EntSize;

  std::optional<yaml::BinaryRef> Content;
  std::optional<llvm::yaml::Hex64> Size;

  // Holds the original section index.
  unsigned OriginalSecNdx;

  Section(ChunkKind Kind, bool IsImplicit = false) : Chunk(Kind, IsImplicit) {}

  static bool classof(const Chunk *S) {
    return S->Kind < ChunkKind::SpecialChunksStart;
  }

  // Some derived sections might have their own special entries. This method
  // returns a vector of <entry name, is used> pairs. It is used for section
  // validation.
  virtual std::vector<std::pair<StringRef, bool>> getEntries() const {
    return {};
  };

  // The following members are used to override section fields which is
  // useful for creating invalid objects.

  // This can be used to override the sh_addralign field.
  std::optional<llvm::yaml::Hex64> ShAddrAlign;

  // This can be used to override the offset stored in the sh_name field.
  // It does not affect the name stored in the string table.
  std::optional<llvm::yaml::Hex64> ShName;

  // This can be used to override the sh_offset field. It does not place the
  // section data at the offset specified.
  std::optional<llvm::yaml::Hex64> ShOffset;

  // This can be used to override the sh_size field. It does not affect the
  // content written.
  std::optional<llvm::yaml::Hex64> ShSize;

  // This can be used to override the sh_flags field.
  std::optional<llvm::yaml::Hex64> ShFlags;

  // This can be used to override the sh_type field. It is useful when we
  // want to use specific YAML keys for a section of a particular type to
  // describe the content, but still want to have a different final type
  // for the section.
  std::optional<ELF_SHT> ShType;
};

// Fill is a block of data which is placed outside of sections. It is
// not present in the sections header table, but it might affect the output file
// size and program headers produced.
struct Fill : Chunk {
  std::optional<yaml::BinaryRef> Pattern;
  llvm::yaml::Hex64 Size;

  Fill() : Chunk(ChunkKind::Fill, /*Implicit=*/false) {}

  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Fill; }
};

struct SectionHeaderTable : Chunk {
  SectionHeaderTable(bool IsImplicit)
      : Chunk(ChunkKind::SectionHeaderTable, IsImplicit) {}

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::SectionHeaderTable;
  }

  std::optional<std::vector<SectionHeader>> Sections;
  std::optional<std::vector<SectionHeader>> Excluded;
  std::optional<bool> NoHeaders;

  size_t getNumHeaders(size_t SectionsNum) const {
    if (IsImplicit || isDefault())
      return SectionsNum;
    if (NoHeaders)
      return (*NoHeaders) ? 0 : SectionsNum;
    return (Sections ? Sections->size() : 0) + /*Null section*/ 1;
  }

  bool isDefault() const { return !Sections && !Excluded && !NoHeaders; }

  static constexpr StringRef TypeStr = "SectionHeaderTable";
};

struct BBAddrMapSection : Section {
  std::optional<std::vector<BBAddrMapEntry>> Entries;
  std::optional<std::vector<PGOAnalysisMapEntry>> PGOAnalyses;

  BBAddrMapSection() : Section(ChunkKind::BBAddrMap) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::BBAddrMap;
  }
};

struct StackSizesSection : Section {
  std::optional<std::vector<StackSizeEntry>> Entries;

  StackSizesSection() : Section(ChunkKind::StackSizes) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::StackSizes;
  }

  static bool nameMatches(StringRef Name) {
    return Name == ".stack_sizes";
  }
};

struct DynamicSection : Section {
  std::optional<std::vector<DynamicEntry>> Entries;

  DynamicSection() : Section(ChunkKind::Dynamic) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Dynamic; }
};

struct RawContentSection : Section {
  std::optional<llvm::yaml::Hex64> Info;

  RawContentSection() : Section(ChunkKind::RawContent) {}

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::RawContent;
  }

  // Is used when a content is read as an array of bytes.
  std::optional<std::vector<uint8_t>> ContentBuf;
};

struct NoBitsSection : Section {
  NoBitsSection() : Section(ChunkKind::NoBits) {}

  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::NoBits; }
};

struct NoteSection : Section {
  std::optional<std::vector<ELFYAML::NoteEntry>> Notes;

  NoteSection() : Section(ChunkKind::Note) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Notes", Notes.has_value()}};
  };

  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Note; }
};

struct HashSection : Section {
  std::optional<std::vector<uint32_t>> Bucket;
  std::optional<std::vector<uint32_t>> Chain;

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Bucket", Bucket.has_value()}, {"Chain", Chain.has_value()}};
  };

  // The following members are used to override section fields.
  // This is useful for creating invalid objects.
  std::optional<llvm::yaml::Hex64> NBucket;
  std::optional<llvm::yaml::Hex64> NChain;

  HashSection() : Section(ChunkKind::Hash) {}

  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Hash; }
};

struct GnuHashHeader {
  // The number of hash buckets.
  // Not used when dumping the object, but can be used to override
  // the real number of buckets when emiting an object from a YAML document.
  std::optional<llvm::yaml::Hex32> NBuckets;

  // Index of the first symbol in the dynamic symbol table
  // included in the hash table.
  llvm::yaml::Hex32 SymNdx;

  // The number of words in the Bloom filter.
  // Not used when dumping the object, but can be used to override the real
  // number of words in the Bloom filter when emiting an object from a YAML
  // document.
  std::optional<llvm::yaml::Hex32> MaskWords;

  // A shift constant used by the Bloom filter.
  llvm::yaml::Hex32 Shift2;
};

struct GnuHashSection : Section {
  std::optional<GnuHashHeader> Header;
  std::optional<std::vector<llvm::yaml::Hex64>> BloomFilter;
  std::optional<std::vector<llvm::yaml::Hex32>> HashBuckets;
  std::optional<std::vector<llvm::yaml::Hex32>> HashValues;

  GnuHashSection() : Section(ChunkKind::GnuHash) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Header", Header.has_value()},
            {"BloomFilter", BloomFilter.has_value()},
            {"HashBuckets", HashBuckets.has_value()},
            {"HashValues", HashValues.has_value()}};
  };

  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::GnuHash; }
};

struct VernauxEntry {
  uint32_t Hash;
  uint16_t Flags;
  uint16_t Other;
  StringRef Name;
};

struct VerneedEntry {
  uint16_t Version;
  StringRef File;
  std::vector<VernauxEntry> AuxV;
};

struct VerneedSection : Section {
  std::optional<std::vector<VerneedEntry>> VerneedV;
  std::optional<llvm::yaml::Hex64> Info;

  VerneedSection() : Section(ChunkKind::Verneed) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Dependencies", VerneedV.has_value()}};
  };

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::Verneed;
  }
};

struct AddrsigSection : Section {
  std::optional<std::vector<YAMLFlowString>> Symbols;

  AddrsigSection() : Section(ChunkKind::Addrsig) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Symbols", Symbols.has_value()}};
  };

  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Addrsig; }
};

struct LinkerOption {
  StringRef Key;
  StringRef Value;
};

struct LinkerOptionsSection : Section {
  std::optional<std::vector<LinkerOption>> Options;

  LinkerOptionsSection() : Section(ChunkKind::LinkerOptions) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Options", Options.has_value()}};
  };

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::LinkerOptions;
  }
};

struct DependentLibrariesSection : Section {
  std::optional<std::vector<YAMLFlowString>> Libs;

  DependentLibrariesSection() : Section(ChunkKind::DependentLibraries) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Libraries", Libs.has_value()}};
  };

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::DependentLibraries;
  }
};

// Represents the call graph profile section entry.
struct CallGraphEntryWeight {
  // The weight of the edge.
  uint64_t Weight;
};

struct CallGraphProfileSection : Section {
  std::optional<std::vector<CallGraphEntryWeight>> Entries;

  CallGraphProfileSection() : Section(ChunkKind::CallGraphProfile) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::CallGraphProfile;
  }
};

struct SymverSection : Section {
  std::optional<std::vector<uint16_t>> Entries;

  SymverSection() : Section(ChunkKind::Symver) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Symver; }
};

struct VerdefEntry {
  std::optional<uint16_t> Version;
  std::optional<uint16_t> Flags;
  std::optional<uint16_t> VersionNdx;
  std::optional<uint32_t> Hash;
  std::vector<StringRef> VerNames;
};

struct VerdefSection : Section {
  std::optional<std::vector<VerdefEntry>> Entries;
  std::optional<llvm::yaml::Hex64> Info;

  VerdefSection() : Section(ChunkKind::Verdef) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Verdef; }
};

struct GroupSection : Section {
  // Members of a group contain a flag and a list of section indices
  // that are part of the group.
  std::optional<std::vector<SectionOrType>> Members;
  std::optional<StringRef> Signature; /* Info */

  GroupSection() : Section(ChunkKind::Group) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Members", Members.has_value()}};
  };

  static bool classof(const Chunk *S) { return S->Kind == ChunkKind::Group; }
};

struct Relocation {
  llvm::yaml::Hex64 Offset;
  YAMLIntUInt Addend;
  ELF_REL Type;
  std::optional<StringRef> Symbol;
};

struct RelocationSection : Section {
  std::optional<std::vector<Relocation>> Relocations;
  StringRef RelocatableSec; /* Info */

  RelocationSection() : Section(ChunkKind::Relocation) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Relocations", Relocations.has_value()}};
  };

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::Relocation;
  }
};

struct RelrSection : Section {
  std::optional<std::vector<llvm::yaml::Hex64>> Entries;

  RelrSection() : Section(ChunkKind::Relr) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::Relr;
  }
};

struct SymtabShndxSection : Section {
  std::optional<std::vector<uint32_t>> Entries;

  SymtabShndxSection() : Section(ChunkKind::SymtabShndxSection) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::SymtabShndxSection;
  }
};

struct ARMIndexTableEntry {
  llvm::yaml::Hex32 Offset;
  llvm::yaml::Hex32 Value;
};

struct ARMIndexTableSection : Section {
  std::optional<std::vector<ARMIndexTableEntry>> Entries;

  ARMIndexTableSection() : Section(ChunkKind::ARMIndexTable) {}

  std::vector<std::pair<StringRef, bool>> getEntries() const override {
    return {{"Entries", Entries.has_value()}};
  };

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::ARMIndexTable;
  }
};

// Represents .MIPS.abiflags section
struct MipsABIFlags : Section {
  llvm::yaml::Hex16 Version;
  MIPS_ISA ISALevel;
  llvm::yaml::Hex8 ISARevision;
  MIPS_AFL_REG GPRSize;
  MIPS_AFL_REG CPR1Size;
  MIPS_AFL_REG CPR2Size;
  MIPS_ABI_FP FpABI;
  MIPS_AFL_EXT ISAExtension;
  MIPS_AFL_ASE ASEs;
  MIPS_AFL_FLAGS1 Flags1;
  llvm::yaml::Hex32 Flags2;

  MipsABIFlags() : Section(ChunkKind::MipsABIFlags) {}

  static bool classof(const Chunk *S) {
    return S->Kind == ChunkKind::MipsABIFlags;
  }
};

struct ProgramHeader {
  ELF_PT Type;
  ELF_PF Flags;
  llvm::yaml::Hex64 VAddr;
  llvm::yaml::Hex64 PAddr;
  std::optional<llvm::yaml::Hex64> Align;
  std::optional<llvm::yaml::Hex64> FileSize;
  std::optional<llvm::yaml::Hex64> MemSize;
  std::optional<llvm::yaml::Hex64> Offset;
  std::optional<StringRef> FirstSec;
  std::optional<StringRef> LastSec;

  // This vector contains all chunks from [FirstSec, LastSec].
  std::vector<Chunk *> Chunks;
};

struct Object {
  FileHeader Header;
  std::vector<ProgramHeader> ProgramHeaders;

  // An object might contain output section descriptions as well as
  // custom data that does not belong to any section.
  std::vector<std::unique_ptr<Chunk>> Chunks;

  // Although in reality the symbols reside in a section, it is a lot
  // cleaner and nicer if we read them from the YAML as a separate
  // top-level key, which automatically ensures that invariants like there
  // being a single SHT_SYMTAB section are upheld.
  std::optional<std::vector<Symbol>> Symbols;
  std::optional<std::vector<Symbol>> DynamicSymbols;
  std::optional<DWARFYAML::Data> DWARF;

  std::vector<Section *> getSections() {
    std::vector<Section *> Ret;
    for (const std::unique_ptr<Chunk> &Sec : Chunks)
      if (auto S = dyn_cast<ELFYAML::Section>(Sec.get()))
        Ret.push_back(S);
    return Ret;
  }

  const SectionHeaderTable &getSectionHeaderTable() const {
    for (const std::unique_ptr<Chunk> &C : Chunks)
      if (auto *S = dyn_cast<ELFYAML::SectionHeaderTable>(C.get()))
        return *S;
    llvm_unreachable("the section header table chunk must always be present");
  }

  ELF_ELFOSABI getOSAbi() const;
  unsigned getMachine() const;
};

bool shouldAllocateFileSpace(ArrayRef<ProgramHeader> Phdrs,
                             const NoBitsSection &S);

} // end namespace ELFYAML
} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::StackSizeEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::BBAddrMapEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::BBAddrMapEntry::BBEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::BBAddrMapEntry::BBRangeEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::PGOAnalysisMapEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::PGOAnalysisMapEntry::PGOBBEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(
    llvm::ELFYAML::PGOAnalysisMapEntry::PGOBBEntry::SuccessorEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::DynamicEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::LinkerOption)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::CallGraphEntryWeight)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::NoteEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::ProgramHeader)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::SectionHeader)
LLVM_YAML_IS_SEQUENCE_VECTOR(std::unique_ptr<llvm::ELFYAML::Chunk>)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::Symbol)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::VerdefEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::VernauxEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::VerneedEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::Relocation)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::SectionOrType)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::ELFYAML::ARMIndexTableEntry)

namespace llvm {
namespace yaml {

template <> struct ScalarTraits<ELFYAML::YAMLIntUInt> {
  static void output(const ELFYAML::YAMLIntUInt &Val, void *Ctx,
                     raw_ostream &Out);
  static StringRef input(StringRef Scalar, void *Ctx,
                         ELFYAML::YAMLIntUInt &Val);
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_ET> {
  static void enumeration(IO &IO, ELFYAML::ELF_ET &Value);
};

template <> struct ScalarEnumerationTraits<ELFYAML::ELF_PT> {
  static void enumeration(IO &IO, ELFYAML::ELF_PT &Value);
};

template <> struct ScalarEnumerationTraits<ELFYAML::ELF_NT> {
  static void enumeration(IO &IO, ELFYAML::ELF_NT &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_EM> {
  static void enumeration(IO &IO, ELFYAML::ELF_EM &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_ELFCLASS> {
  static void enumeration(IO &IO, ELFYAML::ELF_ELFCLASS &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_ELFDATA> {
  static void enumeration(IO &IO, ELFYAML::ELF_ELFDATA &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_ELFOSABI> {
  static void enumeration(IO &IO, ELFYAML::ELF_ELFOSABI &Value);
};

template <>
struct ScalarBitSetTraits<ELFYAML::ELF_EF> {
  static void bitset(IO &IO, ELFYAML::ELF_EF &Value);
};

template <> struct ScalarBitSetTraits<ELFYAML::ELF_PF> {
  static void bitset(IO &IO, ELFYAML::ELF_PF &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_SHT> {
  static void enumeration(IO &IO, ELFYAML::ELF_SHT &Value);
};

template <>
struct ScalarBitSetTraits<ELFYAML::ELF_SHF> {
  static void bitset(IO &IO, ELFYAML::ELF_SHF &Value);
};

template <> struct ScalarEnumerationTraits<ELFYAML::ELF_SHN> {
  static void enumeration(IO &IO, ELFYAML::ELF_SHN &Value);
};

template <> struct ScalarEnumerationTraits<ELFYAML::ELF_STB> {
  static void enumeration(IO &IO, ELFYAML::ELF_STB &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_STT> {
  static void enumeration(IO &IO, ELFYAML::ELF_STT &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_REL> {
  static void enumeration(IO &IO, ELFYAML::ELF_REL &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_DYNTAG> {
  static void enumeration(IO &IO, ELFYAML::ELF_DYNTAG &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::ELF_RSS> {
  static void enumeration(IO &IO, ELFYAML::ELF_RSS &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::MIPS_AFL_REG> {
  static void enumeration(IO &IO, ELFYAML::MIPS_AFL_REG &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::MIPS_ABI_FP> {
  static void enumeration(IO &IO, ELFYAML::MIPS_ABI_FP &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::MIPS_AFL_EXT> {
  static void enumeration(IO &IO, ELFYAML::MIPS_AFL_EXT &Value);
};

template <>
struct ScalarEnumerationTraits<ELFYAML::MIPS_ISA> {
  static void enumeration(IO &IO, ELFYAML::MIPS_ISA &Value);
};

template <>
struct ScalarBitSetTraits<ELFYAML::MIPS_AFL_ASE> {
  static void bitset(IO &IO, ELFYAML::MIPS_AFL_ASE &Value);
};

template <>
struct ScalarBitSetTraits<ELFYAML::MIPS_AFL_FLAGS1> {
  static void bitset(IO &IO, ELFYAML::MIPS_AFL_FLAGS1 &Value);
};

template <>
struct MappingTraits<ELFYAML::FileHeader> {
  static void mapping(IO &IO, ELFYAML::FileHeader &FileHdr);
};

template <> struct MappingTraits<ELFYAML::SectionHeader> {
  static void mapping(IO &IO, ELFYAML::SectionHeader &SHdr);
};

template <> struct MappingTraits<ELFYAML::ProgramHeader> {
  static void mapping(IO &IO, ELFYAML::ProgramHeader &FileHdr);
  static std::string validate(IO &IO, ELFYAML::ProgramHeader &FileHdr);
};

template <>
struct MappingTraits<ELFYAML::Symbol> {
  static void mapping(IO &IO, ELFYAML::Symbol &Symbol);
  static std::string validate(IO &IO, ELFYAML::Symbol &Symbol);
};

template <> struct MappingTraits<ELFYAML::StackSizeEntry> {
  static void mapping(IO &IO, ELFYAML::StackSizeEntry &Rel);
};

template <> struct MappingTraits<ELFYAML::BBAddrMapEntry> {
  static void mapping(IO &IO, ELFYAML::BBAddrMapEntry &E);
};

template <> struct MappingTraits<ELFYAML::BBAddrMapEntry::BBRangeEntry> {
  static void mapping(IO &IO, ELFYAML::BBAddrMapEntry::BBRangeEntry &E);
};

template <> struct MappingTraits<ELFYAML::BBAddrMapEntry::BBEntry> {
  static void mapping(IO &IO, ELFYAML::BBAddrMapEntry::BBEntry &E);
};

template <> struct MappingTraits<ELFYAML::PGOAnalysisMapEntry> {
  static void mapping(IO &IO, ELFYAML::PGOAnalysisMapEntry &Rel);
};

template <> struct MappingTraits<ELFYAML::PGOAnalysisMapEntry::PGOBBEntry> {
  static void mapping(IO &IO, ELFYAML::PGOAnalysisMapEntry::PGOBBEntry &Rel);
};

template <>
struct MappingTraits<ELFYAML::PGOAnalysisMapEntry::PGOBBEntry::SuccessorEntry> {
  static void
  mapping(IO &IO,
          ELFYAML::PGOAnalysisMapEntry::PGOBBEntry::SuccessorEntry &Rel);
};

template <> struct MappingTraits<ELFYAML::GnuHashHeader> {
  static void mapping(IO &IO, ELFYAML::GnuHashHeader &Rel);
};

template <> struct MappingTraits<ELFYAML::DynamicEntry> {
  static void mapping(IO &IO, ELFYAML::DynamicEntry &Rel);
};

template <> struct MappingTraits<ELFYAML::NoteEntry> {
  static void mapping(IO &IO, ELFYAML::NoteEntry &N);
};

template <> struct MappingTraits<ELFYAML::VerdefEntry> {
  static void mapping(IO &IO, ELFYAML::VerdefEntry &E);
};

template <> struct MappingTraits<ELFYAML::VerneedEntry> {
  static void mapping(IO &IO, ELFYAML::VerneedEntry &E);
};

template <> struct MappingTraits<ELFYAML::VernauxEntry> {
  static void mapping(IO &IO, ELFYAML::VernauxEntry &E);
};

template <> struct MappingTraits<ELFYAML::LinkerOption> {
  static void mapping(IO &IO, ELFYAML::LinkerOption &Sym);
};

template <> struct MappingTraits<ELFYAML::CallGraphEntryWeight> {
  static void mapping(IO &IO, ELFYAML::CallGraphEntryWeight &E);
};

template <> struct MappingTraits<ELFYAML::Relocation> {
  static void mapping(IO &IO, ELFYAML::Relocation &Rel);
};

template <> struct MappingTraits<ELFYAML::ARMIndexTableEntry> {
  static void mapping(IO &IO, ELFYAML::ARMIndexTableEntry &E);
};

template <> struct MappingTraits<std::unique_ptr<ELFYAML::Chunk>> {
  static void mapping(IO &IO, std::unique_ptr<ELFYAML::Chunk> &C);
  static std::string validate(IO &io, std::unique_ptr<ELFYAML::Chunk> &C);
};

template <>
struct MappingTraits<ELFYAML::Object> {
  static void mapping(IO &IO, ELFYAML::Object &Object);
};

template <> struct MappingTraits<ELFYAML::SectionOrType> {
  static void mapping(IO &IO, ELFYAML::SectionOrType &sectionOrType);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_ELFYAML_H
