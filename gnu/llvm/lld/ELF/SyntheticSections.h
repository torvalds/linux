//===- SyntheticSection.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Synthetic sections represent chunks of linker-created data. If you
// need to create a chunk of data that to be included in some section
// in the result, you probably want to create that as a synthetic section.
//
// Synthetic sections are designed as input sections as opposed to
// output sections because we want to allow them to be manipulated
// using linker scripts just like other input sections from regular
// files.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_SYNTHETIC_SECTIONS_H
#define LLD_ELF_SYNTHETIC_SECTIONS_H

#include "Config.h"
#include "DWARF.h"
#include "InputSection.h"
#include "Symbols.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Threading.h"

namespace lld::elf {
class Defined;
struct PhdrEntry;
class SymbolTableBaseSection;

struct CieRecord {
  EhSectionPiece *cie = nullptr;
  SmallVector<EhSectionPiece *, 0> fdes;
};

// Section for .eh_frame.
class EhFrameSection final : public SyntheticSection {
public:
  EhFrameSection();
  void writeTo(uint8_t *buf) override;
  void finalizeContents() override;
  bool isNeeded() const override { return !sections.empty(); }
  size_t getSize() const override { return size; }

  static bool classof(const SectionBase *d) {
    return SyntheticSection::classof(d) && d->name == ".eh_frame";
  }

  SmallVector<EhInputSection *, 0> sections;
  size_t numFdes = 0;

  struct FdeData {
    uint32_t pcRel;
    uint32_t fdeVARel;
  };

  SmallVector<FdeData, 0> getFdeData() const;
  ArrayRef<CieRecord *> getCieRecords() const { return cieRecords; }
  template <class ELFT>
  void iterateFDEWithLSDA(llvm::function_ref<void(InputSection &)> fn);

private:
  // This is used only when parsing EhInputSection. We keep it here to avoid
  // allocating one for each EhInputSection.
  llvm::DenseMap<size_t, CieRecord *> offsetToCie;

  uint64_t size = 0;

  template <class ELFT, class RelTy>
  void addRecords(EhInputSection *s, llvm::ArrayRef<RelTy> rels);
  template <class ELFT> void addSectionAux(EhInputSection *s);
  template <class ELFT, class RelTy>
  void iterateFDEWithLSDAAux(EhInputSection &sec, ArrayRef<RelTy> rels,
                             llvm::DenseSet<size_t> &ciesWithLSDA,
                             llvm::function_ref<void(InputSection &)> fn);

  template <class ELFT, class RelTy>
  CieRecord *addCie(EhSectionPiece &piece, ArrayRef<RelTy> rels);

  template <class ELFT, class RelTy>
  Defined *isFdeLive(EhSectionPiece &piece, ArrayRef<RelTy> rels);

  uint64_t getFdePc(uint8_t *buf, size_t off, uint8_t enc) const;

  SmallVector<CieRecord *, 0> cieRecords;

  // CIE records are uniquified by their contents and personality functions.
  llvm::DenseMap<std::pair<ArrayRef<uint8_t>, Symbol *>, CieRecord *> cieMap;
};

class GotSection final : public SyntheticSection {
public:
  GotSection();
  size_t getSize() const override { return size; }
  void finalizeContents() override;
  bool isNeeded() const override;
  void writeTo(uint8_t *buf) override;

  void addConstant(const Relocation &r);
  void addEntry(const Symbol &sym);
  bool addTlsDescEntry(const Symbol &sym);
  bool addDynTlsEntry(const Symbol &sym);
  bool addTlsIndex();
  uint32_t getTlsDescOffset(const Symbol &sym) const;
  uint64_t getTlsDescAddr(const Symbol &sym) const;
  uint64_t getGlobalDynAddr(const Symbol &b) const;
  uint64_t getGlobalDynOffset(const Symbol &b) const;

  uint64_t getTlsIndexVA() { return this->getVA() + tlsIndexOff; }
  uint32_t getTlsIndexOff() const { return tlsIndexOff; }

  // Flag to force GOT to be in output if we have relocations
  // that relies on its address.
  std::atomic<bool> hasGotOffRel = false;

protected:
  size_t numEntries = 0;
  uint32_t tlsIndexOff = -1;
  uint64_t size = 0;
};

// .note.GNU-stack section.
class GnuStackSection : public SyntheticSection {
public:
  GnuStackSection()
      : SyntheticSection(0, llvm::ELF::SHT_PROGBITS, 1, ".note.GNU-stack") {}
  void writeTo(uint8_t *buf) override {}
  size_t getSize() const override { return 0; }
};

class GnuPropertySection final : public SyntheticSection {
public:
  GnuPropertySection();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
};

// .note.gnu.build-id section.
class BuildIdSection : public SyntheticSection {
  // First 16 bytes are a header.
  static const unsigned headerSize = 16;

public:
  const size_t hashSize;
  BuildIdSection();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return headerSize + hashSize; }
  void writeBuildId(llvm::ArrayRef<uint8_t> buf);

private:
  uint8_t *hashBuf;
};

// BssSection is used to reserve space for copy relocations and common symbols.
// We create three instances of this class for .bss, .bss.rel.ro and "COMMON",
// that are used for writable symbols, read-only symbols and common symbols,
// respectively.
class BssSection final : public SyntheticSection {
public:
  BssSection(StringRef name, uint64_t size, uint32_t addralign);
  void writeTo(uint8_t *) override {}
  bool isNeeded() const override { return size != 0; }
  size_t getSize() const override { return size; }

  static bool classof(const SectionBase *s) { return s->bss; }
  uint64_t size;
};

class MipsGotSection final : public SyntheticSection {
public:
  MipsGotSection();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }
  bool updateAllocSize() override;
  void finalizeContents() override;
  bool isNeeded() const override;

  // Join separate GOTs built for each input file to generate
  // primary and optional multiple secondary GOTs.
  void build();

  void addEntry(InputFile &file, Symbol &sym, int64_t addend, RelExpr expr);
  void addDynTlsEntry(InputFile &file, Symbol &sym);
  void addTlsIndex(InputFile &file);

  uint64_t getPageEntryOffset(const InputFile *f, const Symbol &s,
                              int64_t addend) const;
  uint64_t getSymEntryOffset(const InputFile *f, const Symbol &s,
                             int64_t addend) const;
  uint64_t getGlobalDynOffset(const InputFile *f, const Symbol &s) const;
  uint64_t getTlsIndexOffset(const InputFile *f) const;

  // Returns the symbol which corresponds to the first entry of the global part
  // of GOT on MIPS platform. It is required to fill up MIPS-specific dynamic
  // table properties.
  // Returns nullptr if the global part is empty.
  const Symbol *getFirstGlobalEntry() const;

  // Returns the number of entries in the local part of GOT including
  // the number of reserved entries.
  unsigned getLocalEntriesNum() const;

  // Return _gp value for primary GOT (nullptr) or particular input file.
  uint64_t getGp(const InputFile *f = nullptr) const;

private:
  // MIPS GOT consists of three parts: local, global and tls. Each part
  // contains different types of entries. Here is a layout of GOT:
  // - Header entries                |
  // - Page entries                  |   Local part
  // - Local entries (16-bit access) |
  // - Local entries (32-bit access) |
  // - Normal global entries         ||  Global part
  // - Reloc-only global entries     ||
  // - TLS entries                   ||| TLS part
  //
  // Header:
  //   Two entries hold predefined value 0x0 and 0x80000000.
  // Page entries:
  //   These entries created by R_MIPS_GOT_PAGE relocation and R_MIPS_GOT16
  //   relocation against local symbols. They are initialized by higher 16-bit
  //   of the corresponding symbol's value. So each 64kb of address space
  //   requires a single GOT entry.
  // Local entries (16-bit access):
  //   These entries created by GOT relocations against global non-preemptible
  //   symbols so dynamic linker is not necessary to resolve the symbol's
  //   values. "16-bit access" means that corresponding relocations address
  //   GOT using 16-bit index. Each unique Symbol-Addend pair has its own
  //   GOT entry.
  // Local entries (32-bit access):
  //   These entries are the same as above but created by relocations which
  //   address GOT using 32-bit index (R_MIPS_GOT_HI16/LO16 etc).
  // Normal global entries:
  //   These entries created by GOT relocations against preemptible global
  //   symbols. They need to be initialized by dynamic linker and they ordered
  //   exactly as the corresponding entries in the dynamic symbols table.
  // Reloc-only global entries:
  //   These entries created for symbols that are referenced by dynamic
  //   relocations R_MIPS_REL32. These entries are not accessed with gp-relative
  //   addressing, but MIPS ABI requires that these entries be present in GOT.
  // TLS entries:
  //   Entries created by TLS relocations.
  //
  // If the sum of local, global and tls entries is less than 64K only single
  // got is enough. Otherwise, multi-got is created. Series of primary and
  // multiple secondary GOTs have the following layout:
  // - Primary GOT
  //     Header
  //     Local entries
  //     Global entries
  //     Relocation only entries
  //     TLS entries
  //
  // - Secondary GOT
  //     Local entries
  //     Global entries
  //     TLS entries
  // ...
  //
  // All GOT entries required by relocations from a single input file entirely
  // belong to either primary or one of secondary GOTs. To reference GOT entries
  // each GOT has its own _gp value points to the "middle" of the GOT.
  // In the code this value loaded to the register which is used for GOT access.
  //
  // MIPS 32 function's prologue:
  //   lui     v0,0x0
  //   0: R_MIPS_HI16  _gp_disp
  //   addiu   v0,v0,0
  //   4: R_MIPS_LO16  _gp_disp
  //
  // MIPS 64:
  //   lui     at,0x0
  //   14: R_MIPS_GPREL16  main
  //
  // Dynamic linker does not know anything about secondary GOTs and cannot
  // use a regular MIPS mechanism for GOT entries initialization. So we have
  // to use an approach accepted by other architectures and create dynamic
  // relocations R_MIPS_REL32 to initialize global entries (and local in case
  // of PIC code) in secondary GOTs. But ironically MIPS dynamic linker
  // requires GOT entries and correspondingly ordered dynamic symbol table
  // entries to deal with dynamic relocations. To handle this problem
  // relocation-only section in the primary GOT contains entries for all
  // symbols referenced in global parts of secondary GOTs. Although the sum
  // of local and normal global entries of the primary got should be less
  // than 64K, the size of the primary got (including relocation-only entries
  // can be greater than 64K, because parts of the primary got that overflow
  // the 64K limit are used only by the dynamic linker at dynamic link-time
  // and not by 16-bit gp-relative addressing at run-time.
  //
  // For complete multi-GOT description see the following link
  // https://dmz-portal.mips.com/wiki/MIPS_Multi_GOT

  // Number of "Header" entries.
  static const unsigned headerEntriesNum = 2;

  uint64_t size = 0;

  // Symbol and addend.
  using GotEntry = std::pair<Symbol *, int64_t>;

  struct FileGot {
    InputFile *file = nullptr;
    size_t startIndex = 0;

    struct PageBlock {
      size_t firstIndex;
      size_t count;
      PageBlock() : firstIndex(0), count(0) {}
    };

    // Map output sections referenced by MIPS GOT relocations
    // to the description (index/count) "page" entries allocated
    // for this section.
    llvm::SmallMapVector<const OutputSection *, PageBlock, 16> pagesMap;
    // Maps from Symbol+Addend pair or just Symbol to the GOT entry index.
    llvm::MapVector<GotEntry, size_t> local16;
    llvm::MapVector<GotEntry, size_t> local32;
    llvm::MapVector<Symbol *, size_t> global;
    llvm::MapVector<Symbol *, size_t> relocs;
    llvm::MapVector<Symbol *, size_t> tls;
    // Set of symbols referenced by dynamic TLS relocations.
    llvm::MapVector<Symbol *, size_t> dynTlsSymbols;

    // Total number of all entries.
    size_t getEntriesNum() const;
    // Number of "page" entries.
    size_t getPageEntriesNum() const;
    // Number of entries require 16-bit index to access.
    size_t getIndexedEntriesNum() const;
  };

  // Container of GOT created for each input file.
  // After building a final series of GOTs this container
  // holds primary and secondary GOT's.
  std::vector<FileGot> gots;

  // Return (and create if necessary) `FileGot`.
  FileGot &getGot(InputFile &f);

  // Try to merge two GOTs. In case of success the `Dst` contains
  // result of merging and the function returns true. In case of
  // overflow the `Dst` is unchanged and the function returns false.
  bool tryMergeGots(FileGot & dst, FileGot & src, bool isPrimary);
};

class GotPltSection final : public SyntheticSection {
public:
  GotPltSection();
  void addEntry(Symbol &sym);
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  bool isNeeded() const override;

  // Flag to force GotPlt to be in output if we have relocations
  // that relies on its address.
  std::atomic<bool> hasGotPltOffRel = false;

private:
  SmallVector<const Symbol *, 0> entries;
};

// The IgotPltSection is a Got associated with the PltSection for GNU Ifunc
// Symbols that will be relocated by Target->IRelativeRel.
// On most Targets the IgotPltSection will immediately follow the GotPltSection
// on ARM the IgotPltSection will immediately follow the GotSection.
class IgotPltSection final : public SyntheticSection {
public:
  IgotPltSection();
  void addEntry(Symbol &sym);
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  bool isNeeded() const override { return !entries.empty(); }

private:
  SmallVector<const Symbol *, 0> entries;
};

class StringTableSection final : public SyntheticSection {
public:
  StringTableSection(StringRef name, bool dynamic);
  unsigned addString(StringRef s, bool hashIt = true);
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }
  bool isDynamic() const { return dynamic; }

private:
  const bool dynamic;

  uint64_t size = 0;

  llvm::DenseMap<llvm::CachedHashStringRef, unsigned> stringMap;
  SmallVector<StringRef, 0> strings;
};

class DynamicReloc {
public:
  enum Kind {
    /// The resulting dynamic relocation does not reference a symbol (#sym must
    /// be nullptr) and uses #addend as the result of computeAddend().
    AddendOnly,
    /// The resulting dynamic relocation will not reference a symbol: #sym is
    /// only used to compute the addend with InputSection::getRelocTargetVA().
    /// Useful for various relative and TLS relocations (e.g. R_X86_64_TPOFF64).
    AddendOnlyWithTargetVA,
    /// The resulting dynamic relocation references symbol #sym from the dynamic
    /// symbol table and uses #addend as the value of computeAddend().
    AgainstSymbol,
    /// The resulting dynamic relocation references symbol #sym from the dynamic
    /// symbol table and uses InputSection::getRelocTargetVA() + #addend for the
    /// final addend. It can be used for relocations that write the symbol VA as
    // the addend (e.g. R_MIPS_TLS_TPREL64) but still reference the symbol.
    AgainstSymbolWithTargetVA,
    /// This is used by the MIPS multi-GOT implementation. It relocates
    /// addresses of 64kb pages that lie inside the output section.
    MipsMultiGotPage,
  };
  /// This constructor records a relocation against a symbol.
  DynamicReloc(RelType type, const InputSectionBase *inputSec,
               uint64_t offsetInSec, Kind kind, Symbol &sym, int64_t addend,
               RelExpr expr)
      : sym(&sym), inputSec(inputSec), offsetInSec(offsetInSec), type(type),
        addend(addend), kind(kind), expr(expr) {}
  /// This constructor records a relative relocation with no symbol.
  DynamicReloc(RelType type, const InputSectionBase *inputSec,
               uint64_t offsetInSec, int64_t addend = 0)
      : sym(nullptr), inputSec(inputSec), offsetInSec(offsetInSec), type(type),
        addend(addend), kind(AddendOnly), expr(R_ADDEND) {}
  /// This constructor records dynamic relocation settings used by the MIPS
  /// multi-GOT implementation.
  DynamicReloc(RelType type, const InputSectionBase *inputSec,
               uint64_t offsetInSec, const OutputSection *outputSec,
               int64_t addend)
      : sym(nullptr), outputSec(outputSec), inputSec(inputSec),
        offsetInSec(offsetInSec), type(type), addend(addend),
        kind(MipsMultiGotPage), expr(R_ADDEND) {}

  uint64_t getOffset() const;
  uint32_t getSymIndex(SymbolTableBaseSection *symTab) const;
  bool needsDynSymIndex() const {
    return kind == AgainstSymbol || kind == AgainstSymbolWithTargetVA;
  }

  /// Computes the addend of the dynamic relocation. Note that this is not the
  /// same as the #addend member variable as it may also include the symbol
  /// address/the address of the corresponding GOT entry/etc.
  int64_t computeAddend() const;

  void computeRaw(SymbolTableBaseSection *symtab);

  Symbol *sym;
  const OutputSection *outputSec = nullptr;
  const InputSectionBase *inputSec;
  uint64_t offsetInSec;
  uint64_t r_offset;
  RelType type;
  uint32_t r_sym;
  // Initially input addend, then the output addend after
  // RelocationSection<ELFT>::writeTo.
  int64_t addend;

private:
  Kind kind;
  // The kind of expression used to calculate the added (required e.g. for
  // relative GOT relocations).
  RelExpr expr;
};

template <class ELFT> class DynamicSection final : public SyntheticSection {
  LLVM_ELF_IMPORT_TYPES_ELFT(ELFT)

public:
  DynamicSection();
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }

private:
  std::vector<std::pair<int32_t, uint64_t>> computeContents();
  uint64_t size = 0;
};

class RelocationBaseSection : public SyntheticSection {
public:
  RelocationBaseSection(StringRef name, uint32_t type, int32_t dynamicTag,
                        int32_t sizeDynamicTag, bool combreloc,
                        unsigned concurrency);
  /// Add a dynamic relocation without writing an addend to the output section.
  /// This overload can be used if the addends are written directly instead of
  /// using relocations on the input section (e.g. MipsGotSection::writeTo()).
  template <bool shard = false> void addReloc(const DynamicReloc &reloc) {
    relocs.push_back(reloc);
  }
  /// Add a dynamic relocation against \p sym with an optional addend.
  void addSymbolReloc(RelType dynType, InputSectionBase &isec,
                      uint64_t offsetInSec, Symbol &sym, int64_t addend = 0,
                      std::optional<RelType> addendRelType = {});
  /// Add a relative dynamic relocation that uses the target address of \p sym
  /// (i.e. InputSection::getRelocTargetVA()) + \p addend as the addend.
  /// This function should only be called for non-preemptible symbols or
  /// RelExpr values that refer to an address inside the output file (e.g. the
  /// address of the GOT entry for a potentially preemptible symbol).
  template <bool shard = false>
  void addRelativeReloc(RelType dynType, InputSectionBase &isec,
                        uint64_t offsetInSec, Symbol &sym, int64_t addend,
                        RelType addendRelType, RelExpr expr) {
    assert(expr != R_ADDEND && "expected non-addend relocation expression");
    addReloc<shard>(DynamicReloc::AddendOnlyWithTargetVA, dynType, isec,
                    offsetInSec, sym, addend, expr, addendRelType);
  }
  /// Add a dynamic relocation using the target address of \p sym as the addend
  /// if \p sym is non-preemptible. Otherwise add a relocation against \p sym.
  void addAddendOnlyRelocIfNonPreemptible(RelType dynType, GotSection &sec,
                                          uint64_t offsetInSec, Symbol &sym,
                                          RelType addendRelType);
  template <bool shard = false>
  void addReloc(DynamicReloc::Kind kind, RelType dynType, InputSectionBase &sec,
                uint64_t offsetInSec, Symbol &sym, int64_t addend, RelExpr expr,
                RelType addendRelType) {
    // Write the addends to the relocated address if required. We skip
    // it if the written value would be zero.
    if (config->writeAddends && (expr != R_ADDEND || addend != 0))
      sec.addReloc({expr, addendRelType, offsetInSec, addend, &sym});
    addReloc<shard>({dynType, &sec, offsetInSec, kind, sym, addend, expr});
  }
  bool isNeeded() const override {
    return !relocs.empty() ||
           llvm::any_of(relocsVec, [](auto &v) { return !v.empty(); });
  }
  size_t getSize() const override { return relocs.size() * this->entsize; }
  size_t getRelativeRelocCount() const { return numRelativeRelocs; }
  void mergeRels();
  void partitionRels();
  void finalizeContents() override;
  static bool classof(const SectionBase *d) {
    return SyntheticSection::classof(d) &&
           (d->type == llvm::ELF::SHT_RELA || d->type == llvm::ELF::SHT_REL ||
            d->type == llvm::ELF::SHT_RELR ||
            (d->type == llvm::ELF::SHT_AARCH64_AUTH_RELR &&
             config->emachine == llvm::ELF::EM_AARCH64));
  }
  int32_t dynamicTag, sizeDynamicTag;
  SmallVector<DynamicReloc, 0> relocs;

protected:
  void computeRels();
  // Used when parallel relocation scanning adds relocations. The elements
  // will be moved into relocs by mergeRel().
  SmallVector<SmallVector<DynamicReloc, 0>, 0> relocsVec;
  size_t numRelativeRelocs = 0; // used by -z combreloc
  bool combreloc;
};

template <>
inline void RelocationBaseSection::addReloc<true>(const DynamicReloc &reloc) {
  relocsVec[llvm::parallel::getThreadIndex()].push_back(reloc);
}

template <class ELFT>
class RelocationSection final : public RelocationBaseSection {
  using Elf_Rel = typename ELFT::Rel;
  using Elf_Rela = typename ELFT::Rela;

public:
  RelocationSection(StringRef name, bool combreloc, unsigned concurrency);
  void writeTo(uint8_t *buf) override;
};

template <class ELFT>
class AndroidPackedRelocationSection final : public RelocationBaseSection {
  using Elf_Rel = typename ELFT::Rel;
  using Elf_Rela = typename ELFT::Rela;

public:
  AndroidPackedRelocationSection(StringRef name, unsigned concurrency);

  bool updateAllocSize() override;
  size_t getSize() const override { return relocData.size(); }
  void writeTo(uint8_t *buf) override {
    memcpy(buf, relocData.data(), relocData.size());
  }

private:
  SmallVector<char, 0> relocData;
};

struct RelativeReloc {
  uint64_t getOffset() const {
    return inputSec->getVA(inputSec->relocs()[relocIdx].offset);
  }

  const InputSectionBase *inputSec;
  size_t relocIdx;
};

class RelrBaseSection : public SyntheticSection {
public:
  RelrBaseSection(unsigned concurrency, bool isAArch64Auth = false);
  void mergeRels();
  bool isNeeded() const override {
    return !relocs.empty() ||
           llvm::any_of(relocsVec, [](auto &v) { return !v.empty(); });
  }
  SmallVector<RelativeReloc, 0> relocs;
  SmallVector<SmallVector<RelativeReloc, 0>, 0> relocsVec;
};

// RelrSection is used to encode offsets for relative relocations.
// Proposal for adding SHT_RELR sections to generic-abi is here:
//   https://groups.google.com/forum/#!topic/generic-abi/bX460iggiKg
// For more details, see the comment in RelrSection::updateAllocSize().
template <class ELFT> class RelrSection final : public RelrBaseSection {
  using Elf_Relr = typename ELFT::Relr;

public:
  RelrSection(unsigned concurrency, bool isAArch64Auth = false);

  bool updateAllocSize() override;
  size_t getSize() const override { return relrRelocs.size() * this->entsize; }
  void writeTo(uint8_t *buf) override {
    memcpy(buf, relrRelocs.data(), getSize());
  }

private:
  SmallVector<Elf_Relr, 0> relrRelocs;
};

struct SymbolTableEntry {
  Symbol *sym;
  size_t strTabOffset;
};

class SymbolTableBaseSection : public SyntheticSection {
public:
  SymbolTableBaseSection(StringTableSection &strTabSec);
  void finalizeContents() override;
  size_t getSize() const override { return getNumSymbols() * entsize; }
  void addSymbol(Symbol *sym);
  unsigned getNumSymbols() const { return symbols.size() + 1; }
  size_t getSymbolIndex(const Symbol &sym);
  ArrayRef<SymbolTableEntry> getSymbols() const { return symbols; }

protected:
  void sortSymTabSymbols();

  // A vector of symbols and their string table offsets.
  SmallVector<SymbolTableEntry, 0> symbols;

  StringTableSection &strTabSec;

  llvm::once_flag onceFlag;
  llvm::DenseMap<Symbol *, size_t> symbolIndexMap;
  llvm::DenseMap<OutputSection *, size_t> sectionIndexMap;
};

template <class ELFT>
class SymbolTableSection final : public SymbolTableBaseSection {
  using Elf_Sym = typename ELFT::Sym;

public:
  SymbolTableSection(StringTableSection &strTabSec);
  void writeTo(uint8_t *buf) override;
};

class SymtabShndxSection final : public SyntheticSection {
public:
  SymtabShndxSection();

  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
  bool isNeeded() const override;
  void finalizeContents() override;
};

// Outputs GNU Hash section. For detailed explanation see:
// https://blogs.oracle.com/ali/entry/gnu_hash_elf_sections
class GnuHashTableSection final : public SyntheticSection {
public:
  GnuHashTableSection();
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }

  // Adds symbols to the hash table.
  // Sorts the input to satisfy GNU hash section requirements.
  void addSymbols(llvm::SmallVectorImpl<SymbolTableEntry> &symbols);

private:
  // See the comment in writeBloomFilter.
  enum { Shift2 = 26 };

  struct Entry {
    Symbol *sym;
    size_t strTabOffset;
    uint32_t hash;
    uint32_t bucketIdx;
  };

  SmallVector<Entry, 0> symbols;
  size_t maskWords;
  size_t nBuckets = 0;
  size_t size = 0;
};

class HashTableSection final : public SyntheticSection {
public:
  HashTableSection();
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }

private:
  size_t size = 0;
};

// Used for PLT entries. It usually has a PLT header for lazy binding. Each PLT
// entry is associated with a JUMP_SLOT relocation, which may be resolved lazily
// at runtime.
//
// On PowerPC, this section contains lazy symbol resolvers. A branch instruction
// jumps to a PLT call stub, which will then jump to the target (BIND_NOW) or a
// lazy symbol resolver.
//
// On x86 when IBT is enabled, this section (.plt.sec) contains PLT call stubs.
// A call instruction jumps to a .plt.sec entry, which will then jump to the
// target (BIND_NOW) or a .plt entry.
class PltSection : public SyntheticSection {
public:
  PltSection();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
  bool isNeeded() const override;
  void addSymbols();
  void addEntry(Symbol &sym);
  size_t getNumEntries() const { return entries.size(); }

  size_t headerSize;

  SmallVector<const Symbol *, 0> entries;
};

// Used for non-preemptible ifuncs. It does not have a header. Each entry is
// associated with an IRELATIVE relocation, which will be resolved eagerly at
// runtime. PltSection can only contain entries associated with JUMP_SLOT
// relocations, so IPLT entries are in a separate section.
class IpltSection final : public SyntheticSection {
  SmallVector<const Symbol *, 0> entries;

public:
  IpltSection();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
  bool isNeeded() const override { return !entries.empty(); }
  void addSymbols();
  void addEntry(Symbol &sym);
};

class PPC32GlinkSection : public PltSection {
public:
  PPC32GlinkSection();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;

  SmallVector<const Symbol *, 0> canonical_plts;
  static constexpr size_t footerSize = 64;
};

// This is x86-only.
class IBTPltSection : public SyntheticSection {
public:
  IBTPltSection();
  void writeTo(uint8_t *Buf) override;
  bool isNeeded() const override;
  size_t getSize() const override;
};

// Used to align the end of the PT_GNU_RELRO segment and the associated PT_LOAD
// segment to a common-page-size boundary. This padding section ensures that all
// pages in the PT_LOAD segment is covered by at least one section.
class RelroPaddingSection final : public SyntheticSection {
public:
  RelroPaddingSection();
  size_t getSize() const override { return 0; }
  void writeTo(uint8_t *buf) override {}
};

// Used by the merged DWARF32 .debug_names (a per-module index). If we
// move to DWARF64, most of this data will need to be re-sized.
class DebugNamesBaseSection : public SyntheticSection {
public:
  struct Abbrev : llvm::FoldingSetNode {
    uint32_t code;
    uint32_t tag;
    SmallVector<llvm::DWARFDebugNames::AttributeEncoding, 2> attributes;

    void Profile(llvm::FoldingSetNodeID &id) const;
  };

  struct AttrValue {
    uint32_t attrValue;
    uint8_t attrSize;
  };

  struct IndexEntry {
    uint32_t abbrevCode;
    uint32_t poolOffset;
    union {
      uint64_t parentOffset = 0;
      IndexEntry *parentEntry;
    };
    SmallVector<AttrValue, 3> attrValues;
  };

  struct NameEntry {
    const char *name;
    uint32_t hashValue;
    uint32_t stringOffset;
    uint32_t entryOffset;
    // Used to relocate `stringOffset` in the merged section.
    uint32_t chunkIdx;
    SmallVector<IndexEntry *, 0> indexEntries;

    llvm::iterator_range<
        llvm::pointee_iterator<typename SmallVector<IndexEntry *, 0>::iterator>>
    entries() {
      return llvm::make_pointee_range(indexEntries);
    }
  };

  // The contents of one input .debug_names section. An InputChunk
  // typically contains one NameData, but might contain more, especially
  // in LTO builds.
  struct NameData {
    llvm::DWARFDebugNames::Header hdr;
    llvm::DenseMap<uint32_t, uint32_t> abbrevCodeMap;
    SmallVector<NameEntry, 0> nameEntries;
  };

  // InputChunk and OutputChunk hold per-file contributions to the merged index.
  // InputChunk instances will be discarded after `init` completes.
  struct InputChunk {
    uint32_t baseCuIdx;
    LLDDWARFSection section;
    SmallVector<NameData, 0> nameData;
    std::optional<llvm::DWARFDebugNames> llvmDebugNames;
  };

  struct OutputChunk {
    // Pointer to the .debug_info section that contains compile units, used to
    // compute the relocated CU offsets.
    InputSection *infoSec;
    // This initially holds section offsets. After relocation, the section
    // offsets are changed to CU offsets relative the the output section.
    SmallVector<uint32_t, 0> compUnits;
  };

  DebugNamesBaseSection();
  size_t getSize() const override { return size; }
  bool isNeeded() const override { return numChunks > 0; }

protected:
  void init(llvm::function_ref<void(InputFile *, InputChunk &, OutputChunk &)>);
  static void
  parseDebugNames(InputChunk &inputChunk, OutputChunk &chunk,
                  llvm::DWARFDataExtractor &namesExtractor,
                  llvm::DataExtractor &strExtractor,
                  llvm::function_ref<SmallVector<uint32_t, 0>(
                      uint32_t numCUs, const llvm::DWARFDebugNames::Header &hdr,
                      const llvm::DWARFDebugNames::DWARFDebugNamesOffsets &)>
                      readOffsets);
  void computeHdrAndAbbrevTable(MutableArrayRef<InputChunk> inputChunks);
  std::pair<uint32_t, uint32_t>
  computeEntryPool(MutableArrayRef<InputChunk> inputChunks);

  // Input .debug_names sections for relocating string offsets in the name table
  // in `finalizeContents`.
  SmallVector<InputSection *, 0> inputSections;

  llvm::DWARFDebugNames::Header hdr;
  size_t numChunks;
  std::unique_ptr<OutputChunk[]> chunks;
  llvm::SpecificBumpPtrAllocator<Abbrev> abbrevAlloc;
  SmallVector<Abbrev *, 0> abbrevTable;
  SmallVector<char, 0> abbrevTableBuf;

  ArrayRef<OutputChunk> getChunks() const {
    return ArrayRef(chunks.get(), numChunks);
  }

  // Sharded name entries that will be used to compute bucket_count and the
  // count name table.
  static constexpr size_t numShards = 32;
  SmallVector<NameEntry, 0> nameVecs[numShards];
};

// Complement DebugNamesBaseSection for ELFT-aware code: reading offsets,
// relocating string offsets, and writeTo.
template <class ELFT>
class DebugNamesSection final : public DebugNamesBaseSection {
public:
  DebugNamesSection();
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;

  template <class RelTy>
  void getNameRelocs(const InputFile &file,
                     llvm::DenseMap<uint32_t, uint32_t> &relocs,
                     Relocs<RelTy> rels);

private:
  static void readOffsets(InputChunk &inputChunk, OutputChunk &chunk,
                          llvm::DWARFDataExtractor &namesExtractor,
                          llvm::DataExtractor &strExtractor);
};

class GdbIndexSection final : public SyntheticSection {
public:
  struct AddressEntry {
    InputSection *section;
    uint64_t lowAddress;
    uint64_t highAddress;
    uint32_t cuIndex;
  };

  struct CuEntry {
    uint64_t cuOffset;
    uint64_t cuLength;
  };

  struct NameAttrEntry {
    llvm::CachedHashStringRef name;
    uint32_t cuIndexAndAttrs;
  };

  struct GdbChunk {
    InputSection *sec;
    SmallVector<AddressEntry, 0> addressAreas;
    SmallVector<CuEntry, 0> compilationUnits;
  };

  struct GdbSymbol {
    llvm::CachedHashStringRef name;
    SmallVector<uint32_t, 0> cuVector;
    uint32_t nameOff;
    uint32_t cuVectorOff;
  };

  GdbIndexSection();
  template <typename ELFT> static std::unique_ptr<GdbIndexSection> create();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override { return size; }
  bool isNeeded() const override;

private:
  struct GdbIndexHeader {
    llvm::support::ulittle32_t version;
    llvm::support::ulittle32_t cuListOff;
    llvm::support::ulittle32_t cuTypesOff;
    llvm::support::ulittle32_t addressAreaOff;
    llvm::support::ulittle32_t symtabOff;
    llvm::support::ulittle32_t constantPoolOff;
  };

  size_t computeSymtabSize() const;

  // Each chunk contains information gathered from debug sections of a
  // single object file.
  SmallVector<GdbChunk, 0> chunks;

  // A symbol table for this .gdb_index section.
  SmallVector<GdbSymbol, 0> symbols;

  size_t size;
};

// --eh-frame-hdr option tells linker to construct a header for all the
// .eh_frame sections. This header is placed to a section named .eh_frame_hdr
// and also to a PT_GNU_EH_FRAME segment.
// At runtime the unwinder then can find all the PT_GNU_EH_FRAME segments by
// calling dl_iterate_phdr.
// This section contains a lookup table for quick binary search of FDEs.
// Detailed info about internals can be found in Ian Lance Taylor's blog:
// http://www.airs.com/blog/archives/460 (".eh_frame")
// http://www.airs.com/blog/archives/462 (".eh_frame_hdr")
class EhFrameHeader final : public SyntheticSection {
public:
  EhFrameHeader();
  void write();
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
  bool isNeeded() const override;
};

// For more information about .gnu.version and .gnu.version_r see:
// https://www.akkadia.org/drepper/symbol-versioning

// The .gnu.version_d section which has a section type of SHT_GNU_verdef shall
// contain symbol version definitions. The number of entries in this section
// shall be contained in the DT_VERDEFNUM entry of the .dynamic section.
// The section shall contain an array of Elf_Verdef structures, optionally
// followed by an array of Elf_Verdaux structures.
class VersionDefinitionSection final : public SyntheticSection {
public:
  VersionDefinitionSection();
  void finalizeContents() override;
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;

private:
  enum { EntrySize = 28 };
  void writeOne(uint8_t *buf, uint32_t index, StringRef name, size_t nameOff);
  StringRef getFileDefName();

  unsigned fileDefNameOff;
  SmallVector<unsigned, 0> verDefNameOffs;
};

// The .gnu.version section specifies the required version of each symbol in the
// dynamic symbol table. It contains one Elf_Versym for each dynamic symbol
// table entry. An Elf_Versym is just a 16-bit integer that refers to a version
// identifier defined in the either .gnu.version_r or .gnu.version_d section.
// The values 0 and 1 are reserved. All other values are used for versions in
// the own object or in any of the dependencies.
class VersionTableSection final : public SyntheticSection {
public:
  VersionTableSection();
  void finalizeContents() override;
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  bool isNeeded() const override;
};

// The .gnu.version_r section defines the version identifiers used by
// .gnu.version. It contains a linked list of Elf_Verneed data structures. Each
// Elf_Verneed specifies the version requirements for a single DSO, and contains
// a reference to a linked list of Elf_Vernaux data structures which define the
// mapping from version identifiers to version names.
template <class ELFT>
class VersionNeedSection final : public SyntheticSection {
  using Elf_Verneed = typename ELFT::Verneed;
  using Elf_Vernaux = typename ELFT::Vernaux;

  struct Vernaux {
    uint64_t hash;
    uint32_t verneedIndex;
    uint64_t nameStrTab;
  };

  struct Verneed {
    uint64_t nameStrTab;
    std::vector<Vernaux> vernauxs;
  };

  SmallVector<Verneed, 0> verneeds;

public:
  VersionNeedSection();
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
  bool isNeeded() const override;
};

// MergeSyntheticSection is a class that allows us to put mergeable sections
// with different attributes in a single output sections. To do that
// we put them into MergeSyntheticSection synthetic input sections which are
// attached to regular output sections.
class MergeSyntheticSection : public SyntheticSection {
public:
  void addSection(MergeInputSection *ms);
  SmallVector<MergeInputSection *, 0> sections;

protected:
  MergeSyntheticSection(StringRef name, uint32_t type, uint64_t flags,
                        uint32_t addralign)
      : SyntheticSection(flags, type, addralign, name) {}
};

class MergeTailSection final : public MergeSyntheticSection {
public:
  MergeTailSection(StringRef name, uint32_t type, uint64_t flags,
                   uint32_t addralign);

  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  void finalizeContents() override;

private:
  llvm::StringTableBuilder builder;
};

class MergeNoTailSection final : public MergeSyntheticSection {
public:
  MergeNoTailSection(StringRef name, uint32_t type, uint64_t flags,
                     uint32_t addralign)
      : MergeSyntheticSection(name, type, flags, addralign) {}

  size_t getSize() const override { return size; }
  void writeTo(uint8_t *buf) override;
  void finalizeContents() override;

private:
  // We use the most significant bits of a hash as a shard ID.
  // The reason why we don't want to use the least significant bits is
  // because DenseMap also uses lower bits to determine a bucket ID.
  // If we use lower bits, it significantly increases the probability of
  // hash collisions.
  size_t getShardId(uint32_t hash) {
    assert((hash >> 31) == 0);
    return hash >> (31 - llvm::countr_zero(numShards));
  }

  // Section size
  size_t size;

  // String table contents
  constexpr static size_t numShards = 32;
  SmallVector<llvm::StringTableBuilder, 0> shards;
  size_t shardOffsets[numShards];
};

// .MIPS.abiflags section.
template <class ELFT>
class MipsAbiFlagsSection final : public SyntheticSection {
  using Elf_Mips_ABIFlags = llvm::object::Elf_Mips_ABIFlags<ELFT>;

public:
  static std::unique_ptr<MipsAbiFlagsSection> create();

  MipsAbiFlagsSection(Elf_Mips_ABIFlags flags);
  size_t getSize() const override { return sizeof(Elf_Mips_ABIFlags); }
  void writeTo(uint8_t *buf) override;

private:
  Elf_Mips_ABIFlags flags;
};

// .MIPS.options section.
template <class ELFT> class MipsOptionsSection final : public SyntheticSection {
  using Elf_Mips_Options = llvm::object::Elf_Mips_Options<ELFT>;
  using Elf_Mips_RegInfo = llvm::object::Elf_Mips_RegInfo<ELFT>;

public:
  static std::unique_ptr<MipsOptionsSection<ELFT>> create();

  MipsOptionsSection(Elf_Mips_RegInfo reginfo);
  void writeTo(uint8_t *buf) override;

  size_t getSize() const override {
    return sizeof(Elf_Mips_Options) + sizeof(Elf_Mips_RegInfo);
  }

private:
  Elf_Mips_RegInfo reginfo;
};

// MIPS .reginfo section.
template <class ELFT> class MipsReginfoSection final : public SyntheticSection {
  using Elf_Mips_RegInfo = llvm::object::Elf_Mips_RegInfo<ELFT>;

public:
  static std::unique_ptr<MipsReginfoSection> create();

  MipsReginfoSection(Elf_Mips_RegInfo reginfo);
  size_t getSize() const override { return sizeof(Elf_Mips_RegInfo); }
  void writeTo(uint8_t *buf) override;

private:
  Elf_Mips_RegInfo reginfo;
};

// This is a MIPS specific section to hold a space within the data segment
// of executable file which is pointed to by the DT_MIPS_RLD_MAP entry.
// See "Dynamic section" in Chapter 5 in the following document:
// ftp://www.linux-mips.org/pub/linux/mips/doc/ABI/mipsabi.pdf
class MipsRldMapSection final : public SyntheticSection {
public:
  MipsRldMapSection();
  size_t getSize() const override { return config->wordsize; }
  void writeTo(uint8_t *buf) override {}
};

// Representation of the combined .ARM.Exidx input sections. We process these
// as a SyntheticSection like .eh_frame as we need to merge duplicate entries
// and add terminating sentinel entries.
//
// The .ARM.exidx input sections after SHF_LINK_ORDER processing is done form
// a table that the unwinder can derive (Addresses are encoded as offsets from
// table):
// | Address of function | Unwind instructions for function |
// where the unwind instructions are either a small number of unwind or the
// special EXIDX_CANTUNWIND entry representing no unwinding information.
// When an exception is thrown from an address A, the unwinder searches the
// table for the closest table entry with Address of function <= A. This means
// that for two consecutive table entries:
// | A1 | U1 |
// | A2 | U2 |
// The range of addresses described by U1 is [A1, A2)
//
// There are two cases where we need a linker generated table entry to fixup
// the address ranges in the table
// Case 1:
// - A sentinel entry added with an address higher than all
// executable sections. This was needed to work around libunwind bug pr31091.
// - After address assignment we need to find the highest addressed executable
// section and use the limit of that section so that the unwinder never
// matches it.
// Case 2:
// - InputSections without a .ARM.exidx section (usually from Assembly)
// need a table entry so that they terminate the range of the previously
// function. This is pr40277.
//
// Instead of storing pointers to the .ARM.exidx InputSections from
// InputObjects, we store pointers to the executable sections that need
// .ARM.exidx sections. We can then use the dependentSections of these to
// either find the .ARM.exidx section or know that we need to generate one.
class ARMExidxSyntheticSection : public SyntheticSection {
public:
  ARMExidxSyntheticSection();

  // Add an input section to the ARMExidxSyntheticSection. Returns whether the
  // section needs to be removed from the main input section list.
  bool addSection(InputSection *isec);

  size_t getSize() const override { return size; }
  void writeTo(uint8_t *buf) override;
  bool isNeeded() const override;
  // Sort and remove duplicate entries.
  void finalizeContents() override;
  InputSection *getLinkOrderDep() const;

  static bool classof(const SectionBase *sec) {
    return sec->kind() == InputSectionBase::Synthetic &&
           sec->type == llvm::ELF::SHT_ARM_EXIDX;
  }

  // Links to the ARMExidxSections so we can transfer the relocations once the
  // layout is known.
  SmallVector<InputSection *, 0> exidxSections;

private:
  size_t size = 0;

  // Instead of storing pointers to the .ARM.exidx InputSections from
  // InputObjects, we store pointers to the executable sections that need
  // .ARM.exidx sections. We can then use the dependentSections of these to
  // either find the .ARM.exidx section or know that we need to generate one.
  SmallVector<InputSection *, 0> executableSections;

  // Value of executableSecitons before finalizeContents(), so that it can be
  // run repeateadly during fixed point iteration.
  SmallVector<InputSection *, 0> originalExecutableSections;

  // The executable InputSection with the highest address to use for the
  // sentinel. We store separately from ExecutableSections as merging of
  // duplicate entries may mean this InputSection is removed from
  // ExecutableSections.
  InputSection *sentinel = nullptr;
};

// A container for one or more linker generated thunks. Instances of these
// thunks including ARM interworking and Mips LA25 PI to non-PI thunks.
class ThunkSection final : public SyntheticSection {
public:
  // ThunkSection in OS, with desired outSecOff of Off
  ThunkSection(OutputSection *os, uint64_t off);

  // Add a newly created Thunk to this container:
  // Thunk is given offset from start of this InputSection
  // Thunk defines a symbol in this InputSection that can be used as target
  // of a relocation
  void addThunk(Thunk *t);
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  InputSection *getTargetInputSection() const;
  bool assignOffsets();

  // When true, round up reported size of section to 4 KiB. See comment
  // in addThunkSection() for more details.
  bool roundUpSizeForErrata = false;

private:
  SmallVector<Thunk *, 0> thunks;
  size_t size = 0;
};

// Cortex-M Security Extensions. Prefix for functions that should be exported
// for the non-secure world.
const char ACLESESYM_PREFIX[] = "__acle_se_";
const int ACLESESYM_SIZE = 8;

class ArmCmseSGVeneer;

class ArmCmseSGSection final : public SyntheticSection {
public:
  ArmCmseSGSection();
  bool isNeeded() const override { return !entries.empty(); }
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  void addSGVeneer(Symbol *sym, Symbol *ext_sym);
  void addMappingSymbol();
  void finalizeContents() override;
  void exportEntries(SymbolTableBaseSection *symTab);
  uint64_t impLibMaxAddr = 0;

private:
  SmallVector<std::pair<Symbol *, Symbol *>, 0> entries;
  SmallVector<ArmCmseSGVeneer *, 0> sgVeneers;
  uint64_t newEntries = 0;
};

// Used to compute outSecOff of .got2 in each object file. This is needed to
// synthesize PLT entries for PPC32 Secure PLT ABI.
class PPC32Got2Section final : public SyntheticSection {
public:
  PPC32Got2Section();
  size_t getSize() const override { return 0; }
  bool isNeeded() const override;
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override {}
};

// This section is used to store the addresses of functions that are called
// in range-extending thunks on PowerPC64. When producing position dependent
// code the addresses are link-time constants and the table is written out to
// the binary. When producing position-dependent code the table is allocated and
// filled in by the dynamic linker.
class PPC64LongBranchTargetSection final : public SyntheticSection {
public:
  PPC64LongBranchTargetSection();
  uint64_t getEntryVA(const Symbol *sym, int64_t addend);
  std::optional<uint32_t> addEntry(const Symbol *sym, int64_t addend);
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
  bool isNeeded() const override;
  void finalizeContents() override { finalized = true; }

private:
  SmallVector<std::pair<const Symbol *, int64_t>, 0> entries;
  llvm::DenseMap<std::pair<const Symbol *, int64_t>, uint32_t> entry_index;
  bool finalized = false;
};

template <typename ELFT>
class PartitionElfHeaderSection final : public SyntheticSection {
public:
  PartitionElfHeaderSection();
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
};

template <typename ELFT>
class PartitionProgramHeadersSection final : public SyntheticSection {
public:
  PartitionProgramHeadersSection();
  size_t getSize() const override;
  void writeTo(uint8_t *buf) override;
};

class PartitionIndexSection final : public SyntheticSection {
public:
  PartitionIndexSection();
  size_t getSize() const override;
  void finalizeContents() override;
  void writeTo(uint8_t *buf) override;
};

// See the following link for the Android-specific loader code that operates on
// this section:
// https://cs.android.com/android/platform/superproject/+/master:bionic/libc/bionic/libc_init_static.cpp;drc=9425b16978f9c5aa8f2c50c873db470819480d1d;l=192
class MemtagAndroidNote final : public SyntheticSection {
public:
  MemtagAndroidNote()
      : SyntheticSection(llvm::ELF::SHF_ALLOC, llvm::ELF::SHT_NOTE,
                         /*alignment=*/4, ".note.android.memtag") {}
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
};

class PackageMetadataNote final : public SyntheticSection {
public:
  PackageMetadataNote()
      : SyntheticSection(llvm::ELF::SHF_ALLOC, llvm::ELF::SHT_NOTE,
                         /*alignment=*/4, ".note.package") {}
  void writeTo(uint8_t *buf) override;
  size_t getSize() const override;
};

class MemtagGlobalDescriptors final : public SyntheticSection {
public:
  MemtagGlobalDescriptors()
      : SyntheticSection(llvm::ELF::SHF_ALLOC,
                         llvm::ELF::SHT_AARCH64_MEMTAG_GLOBALS_DYNAMIC,
                         /*alignment=*/4, ".memtag.globals.dynamic") {}
  void writeTo(uint8_t *buf) override;
  // The size of the section is non-computable until all addresses are
  // synthetized, because the section's contents contain a sorted
  // varint-compressed list of pointers to global variables. We only know the
  // final size after `finalizeAddressDependentContent()`.
  size_t getSize() const override;
  bool updateAllocSize() override;

  void addSymbol(const Symbol &sym) {
    symbols.push_back(&sym);
  }

  bool isNeeded() const override {
    return !symbols.empty();
  }

private:
  SmallVector<const Symbol *, 0> symbols;
};

template <class ELFT> void createSyntheticSections();
InputSection *createInterpSection();
MergeInputSection *createCommentSection();
template <class ELFT> void splitSections();
void combineEhSections();

bool hasMemtag();
bool canHaveMemtagGlobals();

template <typename ELFT> void writeEhdr(uint8_t *buf, Partition &part);
template <typename ELFT> void writePhdrs(uint8_t *buf, Partition &part);

Defined *addSyntheticLocal(StringRef name, uint8_t type, uint64_t value,
                           uint64_t size, InputSectionBase &section);

void addVerneed(Symbol *ss);

// Linker generated per-partition sections.
struct Partition {
  StringRef name;
  uint64_t nameStrTab;

  std::unique_ptr<SyntheticSection> elfHeader;
  std::unique_ptr<SyntheticSection> programHeaders;
  SmallVector<PhdrEntry *, 0> phdrs;

  std::unique_ptr<ARMExidxSyntheticSection> armExidx;
  std::unique_ptr<BuildIdSection> buildId;
  std::unique_ptr<SyntheticSection> dynamic;
  std::unique_ptr<StringTableSection> dynStrTab;
  std::unique_ptr<SymbolTableBaseSection> dynSymTab;
  std::unique_ptr<EhFrameHeader> ehFrameHdr;
  std::unique_ptr<EhFrameSection> ehFrame;
  std::unique_ptr<GnuHashTableSection> gnuHashTab;
  std::unique_ptr<HashTableSection> hashTab;
  std::unique_ptr<MemtagAndroidNote> memtagAndroidNote;
  std::unique_ptr<MemtagGlobalDescriptors> memtagGlobalDescriptors;
  std::unique_ptr<PackageMetadataNote> packageMetadataNote;
  std::unique_ptr<RelocationBaseSection> relaDyn;
  std::unique_ptr<RelrBaseSection> relrDyn;
  std::unique_ptr<RelrBaseSection> relrAuthDyn;
  std::unique_ptr<VersionDefinitionSection> verDef;
  std::unique_ptr<SyntheticSection> verNeed;
  std::unique_ptr<VersionTableSection> verSym;

  unsigned getNumber() const { return this - &partitions[0] + 1; }
};

LLVM_LIBRARY_VISIBILITY extern Partition *mainPart;

inline Partition &SectionBase::getPartition() const {
  assert(isLive());
  return partitions[partition - 1];
}

// Linker generated sections which can be used as inputs and are not specific to
// a partition.
struct InStruct {
  std::unique_ptr<InputSection> attributes;
  std::unique_ptr<SyntheticSection> riscvAttributes;
  std::unique_ptr<BssSection> bss;
  std::unique_ptr<BssSection> bssRelRo;
  std::unique_ptr<GotSection> got;
  std::unique_ptr<GotPltSection> gotPlt;
  std::unique_ptr<IgotPltSection> igotPlt;
  std::unique_ptr<RelroPaddingSection> relroPadding;
  std::unique_ptr<SyntheticSection> armCmseSGSection;
  std::unique_ptr<PPC64LongBranchTargetSection> ppc64LongBranchTarget;
  std::unique_ptr<SyntheticSection> mipsAbiFlags;
  std::unique_ptr<MipsGotSection> mipsGot;
  std::unique_ptr<SyntheticSection> mipsOptions;
  std::unique_ptr<SyntheticSection> mipsReginfo;
  std::unique_ptr<MipsRldMapSection> mipsRldMap;
  std::unique_ptr<SyntheticSection> partEnd;
  std::unique_ptr<SyntheticSection> partIndex;
  std::unique_ptr<PltSection> plt;
  std::unique_ptr<IpltSection> iplt;
  std::unique_ptr<PPC32Got2Section> ppc32Got2;
  std::unique_ptr<IBTPltSection> ibtPlt;
  std::unique_ptr<RelocationBaseSection> relaPlt;
  // Non-SHF_ALLOC sections
  std::unique_ptr<SyntheticSection> debugNames;
  std::unique_ptr<GdbIndexSection> gdbIndex;
  std::unique_ptr<StringTableSection> shStrTab;
  std::unique_ptr<StringTableSection> strTab;
  std::unique_ptr<SymbolTableBaseSection> symTab;
  std::unique_ptr<SymtabShndxSection> symTabShndx;

  void reset();
};

LLVM_LIBRARY_VISIBILITY extern InStruct in;

} // namespace lld::elf

#endif
