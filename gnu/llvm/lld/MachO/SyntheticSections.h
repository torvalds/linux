//===- SyntheticSections.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_SYNTHETIC_SECTIONS_H
#define LLD_MACHO_SYNTHETIC_SECTIONS_H

#include "Config.h"
#include "ExportTrie.h"
#include "InputSection.h"
#include "OutputSection.h"
#include "OutputSegment.h"
#include "Target.h"
#include "Writer.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

#include <unordered_map>

namespace llvm {
class DWARFUnit;
} // namespace llvm

namespace lld::macho {

class Defined;
class DylibSymbol;
class LoadCommand;
class ObjFile;
class UnwindInfoSection;

class SyntheticSection : public OutputSection {
public:
  SyntheticSection(const char *segname, const char *name);
  virtual ~SyntheticSection() = default;

  static bool classof(const OutputSection *sec) {
    return sec->kind() == SyntheticKind;
  }

  StringRef segname;
  // This fake InputSection makes it easier for us to write code that applies
  // generically to both user inputs and synthetics.
  InputSection *isec;
};

// All sections in __LINKEDIT should inherit from this.
class LinkEditSection : public SyntheticSection {
public:
  LinkEditSection(const char *segname, const char *name)
      : SyntheticSection(segname, name) {
    align = target->wordSize;
  }

  // Implementations of this method can assume that the regular (non-__LINKEDIT)
  // sections already have their addresses assigned.
  virtual void finalizeContents() {}

  // Sections in __LINKEDIT are special: their offsets are recorded in the
  // load commands like LC_DYLD_INFO_ONLY and LC_SYMTAB, instead of in section
  // headers.
  bool isHidden() const final { return true; }

  virtual uint64_t getRawSize() const = 0;

  // codesign (or more specifically libstuff) checks that each section in
  // __LINKEDIT ends where the next one starts -- no gaps are permitted. We
  // therefore align every section's start and end points to WordSize.
  //
  // NOTE: This assumes that the extra bytes required for alignment can be
  // zero-valued bytes.
  uint64_t getSize() const final { return llvm::alignTo(getRawSize(), align); }
};

// The header of the Mach-O file, which must have a file offset of zero.
class MachHeaderSection final : public SyntheticSection {
public:
  MachHeaderSection();
  bool isHidden() const override { return true; }
  uint64_t getSize() const override;
  void writeTo(uint8_t *buf) const override;

  void addLoadCommand(LoadCommand *);

protected:
  std::vector<LoadCommand *> loadCommands;
  uint32_t sizeOfCmds = 0;
};

// A hidden section that exists solely for the purpose of creating the
// __PAGEZERO segment, which is used to catch null pointer dereferences.
class PageZeroSection final : public SyntheticSection {
public:
  PageZeroSection();
  bool isHidden() const override { return true; }
  bool isNeeded() const override { return target->pageZeroSize != 0; }
  uint64_t getSize() const override { return target->pageZeroSize; }
  uint64_t getFileSize() const override { return 0; }
  void writeTo(uint8_t *buf) const override {}
};

// This is the base class for the GOT and TLVPointer sections, which are nearly
// functionally identical -- they will both be populated by dyld with addresses
// to non-lazily-loaded dylib symbols. The main difference is that the
// TLVPointerSection stores references to thread-local variables.
class NonLazyPointerSectionBase : public SyntheticSection {
public:
  NonLazyPointerSectionBase(const char *segname, const char *name);
  const llvm::SetVector<const Symbol *> &getEntries() const { return entries; }
  bool isNeeded() const override { return !entries.empty(); }
  uint64_t getSize() const override {
    return entries.size() * target->wordSize;
  }
  void writeTo(uint8_t *buf) const override;
  void addEntry(Symbol *sym);
  uint64_t getVA(uint32_t gotIndex) const {
    return addr + gotIndex * target->wordSize;
  }

private:
  llvm::SetVector<const Symbol *> entries;
};

class GotSection final : public NonLazyPointerSectionBase {
public:
  GotSection();
};

class TlvPointerSection final : public NonLazyPointerSectionBase {
public:
  TlvPointerSection();
};

struct Location {
  const InputSection *isec;
  uint64_t offset;

  Location(const InputSection *isec, uint64_t offset)
      : isec(isec), offset(offset) {}
  uint64_t getVA() const { return isec->getVA(offset); }
};

// Stores rebase opcodes, which tell dyld where absolute addresses have been
// encoded in the binary. If the binary is not loaded at its preferred address,
// dyld has to rebase these addresses by adding an offset to them.
class RebaseSection final : public LinkEditSection {
public:
  RebaseSection();
  void finalizeContents() override;
  uint64_t getRawSize() const override { return contents.size(); }
  bool isNeeded() const override { return !locations.empty(); }
  void writeTo(uint8_t *buf) const override;

  void addEntry(const InputSection *isec, uint64_t offset) {
    if (config->isPic)
      locations.emplace_back(isec, offset);
  }

private:
  std::vector<Location> locations;
  SmallVector<char, 128> contents;
};

struct BindingEntry {
  int64_t addend;
  Location target;
  BindingEntry(int64_t addend, Location target)
      : addend(addend), target(target) {}
};

template <class Sym>
using BindingsMap = llvm::DenseMap<Sym, std::vector<BindingEntry>>;

// Stores bind opcodes for telling dyld which symbols to load non-lazily.
class BindingSection final : public LinkEditSection {
public:
  BindingSection();
  void finalizeContents() override;
  uint64_t getRawSize() const override { return contents.size(); }
  bool isNeeded() const override { return !bindingsMap.empty(); }
  void writeTo(uint8_t *buf) const override;

  void addEntry(const Symbol *dysym, const InputSection *isec, uint64_t offset,
                int64_t addend = 0) {
    bindingsMap[dysym].emplace_back(addend, Location(isec, offset));
  }

private:
  BindingsMap<const Symbol *> bindingsMap;
  SmallVector<char, 128> contents;
};

// Stores bind opcodes for telling dyld which weak symbols need coalescing.
// There are two types of entries in this section:
//
//   1) Non-weak definitions: This is a symbol definition that weak symbols in
//   other dylibs should coalesce to.
//
//   2) Weak bindings: These tell dyld that a given symbol reference should
//   coalesce to a non-weak definition if one is found. Note that unlike the
//   entries in the BindingSection, the bindings here only refer to these
//   symbols by name, but do not specify which dylib to load them from.
class WeakBindingSection final : public LinkEditSection {
public:
  WeakBindingSection();
  void finalizeContents() override;
  uint64_t getRawSize() const override { return contents.size(); }
  bool isNeeded() const override {
    return !bindingsMap.empty() || !definitions.empty();
  }

  void writeTo(uint8_t *buf) const override;

  void addEntry(const Symbol *symbol, const InputSection *isec, uint64_t offset,
                int64_t addend = 0) {
    bindingsMap[symbol].emplace_back(addend, Location(isec, offset));
  }

  bool hasEntry() const { return !bindingsMap.empty(); }

  void addNonWeakDefinition(const Defined *defined) {
    definitions.emplace_back(defined);
  }

  bool hasNonWeakDefinition() const { return !definitions.empty(); }

private:
  BindingsMap<const Symbol *> bindingsMap;
  std::vector<const Defined *> definitions;
  SmallVector<char, 128> contents;
};

// The following sections implement lazy symbol binding -- very similar to the
// PLT mechanism in ELF.
//
// ELF's .plt section is broken up into two sections in Mach-O: StubsSection
// and StubHelperSection. Calls to functions in dylibs will end up calling into
// StubsSection, which contains indirect jumps to addresses stored in the
// LazyPointerSection (the counterpart to ELF's .plt.got).
//
// We will first describe how non-weak symbols are handled.
//
// At program start, the LazyPointerSection contains addresses that point into
// one of the entry points in the middle of the StubHelperSection. The code in
// StubHelperSection will push on the stack an offset into the
// LazyBindingSection. The push is followed by a jump to the beginning of the
// StubHelperSection (similar to PLT0), which then calls into dyld_stub_binder.
// dyld_stub_binder is a non-lazily-bound symbol, so this call looks it up in
// the GOT.
//
// The stub binder will look up the bind opcodes in the LazyBindingSection at
// the given offset. The bind opcodes will tell the binder to update the
// address in the LazyPointerSection to point to the symbol, so that subsequent
// calls don't have to redo the symbol resolution. The binder will then jump to
// the resolved symbol.
//
// With weak symbols, the situation is slightly different. Since there is no
// "weak lazy" lookup, function calls to weak symbols are always non-lazily
// bound. We emit both regular non-lazy bindings as well as weak bindings, in
// order that the weak bindings may overwrite the non-lazy bindings if an
// appropriate symbol is found at runtime. However, the bound addresses will
// still be written (non-lazily) into the LazyPointerSection.
//
// Symbols are always bound eagerly when chained fixups are used. In that case,
// StubsSection contains indirect jumps to addresses stored in the GotSection.
// The GOT directly contains the fixup entries, which will be replaced by the
// address of the target symbols on load. LazyPointerSection and
// StubHelperSection are not used.

class StubsSection final : public SyntheticSection {
public:
  StubsSection();
  uint64_t getSize() const override;
  bool isNeeded() const override { return !entries.empty(); }
  void finalize() override;
  void writeTo(uint8_t *buf) const override;
  const llvm::SetVector<Symbol *> &getEntries() const { return entries; }
  // Creates a stub for the symbol and the corresponding entry in the
  // LazyPointerSection.
  void addEntry(Symbol *);
  uint64_t getVA(uint32_t stubsIndex) const {
    assert(isFinal || target->usesThunks());
    // ConcatOutputSection::finalize() can seek the address of a
    // stub before its address is assigned. Before __stubs is
    // finalized, return a contrived out-of-range address.
    return isFinal ? addr + stubsIndex * target->stubSize
                   : TargetInfo::outOfRangeVA;
  }

  bool isFinal = false; // is address assigned?

private:
  llvm::SetVector<Symbol *> entries;
};

class StubHelperSection final : public SyntheticSection {
public:
  StubHelperSection();
  uint64_t getSize() const override;
  bool isNeeded() const override;
  void writeTo(uint8_t *buf) const override;

  void setUp();

  DylibSymbol *stubBinder = nullptr;
  Defined *dyldPrivate = nullptr;
};

class ObjCSelRefsHelper {
public:
  static void initialize();
  static void cleanup();

  static ConcatInputSection *getSelRef(StringRef methname);
  static ConcatInputSection *makeSelRef(StringRef methname);

private:
  static llvm::DenseMap<llvm::CachedHashStringRef, ConcatInputSection *>
      methnameToSelref;
};

// Objective-C stubs are hoisted objc_msgSend calls per selector called in the
// program. Apple Clang produces undefined symbols to each stub, such as
// '_objc_msgSend$foo', which are then synthesized by the linker. The stubs
// load the particular selector 'foo' from __objc_selrefs, setting it to the
// first argument of the objc_msgSend call, and then jumps to objc_msgSend. The
// actual stub contents are mirrored from ld64.
class ObjCStubsSection final : public SyntheticSection {
public:
  ObjCStubsSection();
  void addEntry(Symbol *sym);
  uint64_t getSize() const override;
  bool isNeeded() const override { return !symbols.empty(); }
  void finalize() override { isec->isFinal = true; }
  void writeTo(uint8_t *buf) const override;
  void setUp();

  static constexpr llvm::StringLiteral symbolPrefix = "_objc_msgSend$";
  static bool isObjCStubSymbol(Symbol *sym);
  static StringRef getMethname(Symbol *sym);

private:
  std::vector<Defined *> symbols;
  Symbol *objcMsgSend = nullptr;
};

// Note that this section may also be targeted by non-lazy bindings. In
// particular, this happens when branch relocations target weak symbols.
class LazyPointerSection final : public SyntheticSection {
public:
  LazyPointerSection();
  uint64_t getSize() const override;
  bool isNeeded() const override;
  void writeTo(uint8_t *buf) const override;
  uint64_t getVA(uint32_t index) const {
    return addr + (index << target->p2WordSize);
  }
};

class LazyBindingSection final : public LinkEditSection {
public:
  LazyBindingSection();
  void finalizeContents() override;
  uint64_t getRawSize() const override { return contents.size(); }
  bool isNeeded() const override { return !entries.empty(); }
  void writeTo(uint8_t *buf) const override;
  // Note that every entry here will by referenced by a corresponding entry in
  // the StubHelperSection.
  void addEntry(Symbol *dysym);
  const llvm::SetVector<Symbol *> &getEntries() const { return entries; }

private:
  uint32_t encode(const Symbol &);

  llvm::SetVector<Symbol *> entries;
  SmallVector<char, 128> contents;
  llvm::raw_svector_ostream os{contents};
};

// Stores a trie that describes the set of exported symbols.
class ExportSection final : public LinkEditSection {
public:
  ExportSection();
  void finalizeContents() override;
  uint64_t getRawSize() const override { return size; }
  bool isNeeded() const override { return size; }
  void writeTo(uint8_t *buf) const override;

  bool hasWeakSymbol = false;

private:
  TrieBuilder trieBuilder;
  size_t size = 0;
};

// Stores 'data in code' entries that describe the locations of data regions
// inside code sections. This is used by llvm-objdump to distinguish jump tables
// and stop them from being disassembled as instructions.
class DataInCodeSection final : public LinkEditSection {
public:
  DataInCodeSection();
  void finalizeContents() override;
  uint64_t getRawSize() const override {
    return sizeof(llvm::MachO::data_in_code_entry) * entries.size();
  }
  void writeTo(uint8_t *buf) const override;

private:
  std::vector<llvm::MachO::data_in_code_entry> entries;
};

// Stores ULEB128 delta encoded addresses of functions.
class FunctionStartsSection final : public LinkEditSection {
public:
  FunctionStartsSection();
  void finalizeContents() override;
  uint64_t getRawSize() const override { return contents.size(); }
  void writeTo(uint8_t *buf) const override;

private:
  SmallVector<char, 128> contents;
};

// Stores the strings referenced by the symbol table.
class StringTableSection final : public LinkEditSection {
public:
  StringTableSection();
  // Returns the start offset of the added string.
  uint32_t addString(StringRef);
  uint64_t getRawSize() const override { return size; }
  void writeTo(uint8_t *buf) const override;

  static constexpr size_t emptyStringIndex = 1;

private:
  // ld64 emits string tables which start with a space and a zero byte. We
  // match its behavior here since some tools depend on it.
  // Consequently, the empty string will be at index 1, not zero.
  std::vector<StringRef> strings{" "};
  size_t size = 2;
};

struct SymtabEntry {
  Symbol *sym;
  size_t strx;
};

struct StabsEntry {
  uint8_t type = 0;
  uint32_t strx = StringTableSection::emptyStringIndex;
  uint8_t sect = 0;
  uint16_t desc = 0;
  uint64_t value = 0;

  StabsEntry() = default;
  explicit StabsEntry(uint8_t type) : type(type) {}
};

// Symbols of the same type must be laid out contiguously: we choose to emit
// all local symbols first, then external symbols, and finally undefined
// symbols. For each symbol type, the LC_DYSYMTAB load command will record the
// range (start index and total number) of those symbols in the symbol table.
class SymtabSection : public LinkEditSection {
public:
  void finalizeContents() override;
  uint32_t getNumSymbols() const;
  uint32_t getNumLocalSymbols() const {
    return stabs.size() + localSymbols.size();
  }
  uint32_t getNumExternalSymbols() const { return externalSymbols.size(); }
  uint32_t getNumUndefinedSymbols() const { return undefinedSymbols.size(); }

private:
  void emitBeginSourceStab(StringRef);
  void emitEndSourceStab();
  void emitObjectFileStab(ObjFile *);
  void emitEndFunStab(Defined *);
  void emitStabs();

protected:
  SymtabSection(StringTableSection &);

  StringTableSection &stringTableSection;
  // STABS symbols are always local symbols, but we represent them with special
  // entries because they may use fields like n_sect and n_desc differently.
  std::vector<StabsEntry> stabs;
  std::vector<SymtabEntry> localSymbols;
  std::vector<SymtabEntry> externalSymbols;
  std::vector<SymtabEntry> undefinedSymbols;
};

template <class LP> SymtabSection *makeSymtabSection(StringTableSection &);

// The indirect symbol table is a list of 32-bit integers that serve as indices
// into the (actual) symbol table. The indirect symbol table is a
// concatenation of several sub-arrays of indices, each sub-array belonging to
// a separate section. The starting offset of each sub-array is stored in the
// reserved1 header field of the respective section.
//
// These sub-arrays provide symbol information for sections that store
// contiguous sequences of symbol references. These references can be pointers
// (e.g. those in the GOT and TLVP sections) or assembly sequences (e.g.
// function stubs).
class IndirectSymtabSection final : public LinkEditSection {
public:
  IndirectSymtabSection();
  void finalizeContents() override;
  uint32_t getNumSymbols() const;
  uint64_t getRawSize() const override {
    return getNumSymbols() * sizeof(uint32_t);
  }
  bool isNeeded() const override;
  void writeTo(uint8_t *buf) const override;
};

// The code signature comes at the very end of the linked output file.
class CodeSignatureSection final : public LinkEditSection {
public:
  // NOTE: These values are duplicated in llvm-objcopy's MachO/Object.h file
  // and any changes here, should be repeated there.
  static constexpr uint8_t blockSizeShift = 12;
  static constexpr size_t blockSize = (1 << blockSizeShift); // 4 KiB
  static constexpr size_t hashSize = 256 / 8;
  static constexpr size_t blobHeadersSize = llvm::alignTo<8>(
      sizeof(llvm::MachO::CS_SuperBlob) + sizeof(llvm::MachO::CS_BlobIndex));
  static constexpr uint32_t fixedHeadersSize =
      blobHeadersSize + sizeof(llvm::MachO::CS_CodeDirectory);

  uint32_t fileNamePad = 0;
  uint32_t allHeadersSize = 0;
  StringRef fileName;

  CodeSignatureSection();
  uint64_t getRawSize() const override;
  bool isNeeded() const override { return true; }
  void writeTo(uint8_t *buf) const override;
  uint32_t getBlockCount() const;
  void writeHashes(uint8_t *buf) const;
};

class CStringSection : public SyntheticSection {
public:
  CStringSection(const char *name);
  void addInput(CStringInputSection *);
  uint64_t getSize() const override { return size; }
  virtual void finalizeContents();
  bool isNeeded() const override { return !inputs.empty(); }
  void writeTo(uint8_t *buf) const override;

  std::vector<CStringInputSection *> inputs;

private:
  uint64_t size;
};

class DeduplicatedCStringSection final : public CStringSection {
public:
  DeduplicatedCStringSection(const char *name) : CStringSection(name){};
  uint64_t getSize() const override { return size; }
  void finalizeContents() override;
  void writeTo(uint8_t *buf) const override;

  struct StringOffset {
    uint8_t trailingZeros;
    uint64_t outSecOff = UINT64_MAX;

    explicit StringOffset(uint8_t zeros) : trailingZeros(zeros) {}
  };

  StringOffset getStringOffset(StringRef str) const;

private:
  llvm::DenseMap<llvm::CachedHashStringRef, StringOffset> stringOffsetMap;
  size_t size = 0;
};

/*
 * This section contains deduplicated literal values. The 16-byte values are
 * laid out first, followed by the 8- and then the 4-byte ones.
 */
class WordLiteralSection final : public SyntheticSection {
public:
  using UInt128 = std::pair<uint64_t, uint64_t>;
  // I don't think the standard guarantees the size of a pair, so let's make
  // sure it's exact -- that way we can construct it via `mmap`.
  static_assert(sizeof(UInt128) == 16);

  WordLiteralSection();
  void addInput(WordLiteralInputSection *);
  void finalizeContents();
  void writeTo(uint8_t *buf) const override;

  uint64_t getSize() const override {
    return literal16Map.size() * 16 + literal8Map.size() * 8 +
           literal4Map.size() * 4;
  }

  bool isNeeded() const override {
    return !literal16Map.empty() || !literal4Map.empty() ||
           !literal8Map.empty();
  }

  uint64_t getLiteral16Offset(uintptr_t buf) const {
    return literal16Map.at(*reinterpret_cast<const UInt128 *>(buf)) * 16;
  }

  uint64_t getLiteral8Offset(uintptr_t buf) const {
    return literal16Map.size() * 16 +
           literal8Map.at(*reinterpret_cast<const uint64_t *>(buf)) * 8;
  }

  uint64_t getLiteral4Offset(uintptr_t buf) const {
    return literal16Map.size() * 16 + literal8Map.size() * 8 +
           literal4Map.at(*reinterpret_cast<const uint32_t *>(buf)) * 4;
  }

private:
  std::vector<WordLiteralInputSection *> inputs;

  template <class T> struct Hasher {
    llvm::hash_code operator()(T v) const { return llvm::hash_value(v); }
  };
  // We're using unordered_map instead of DenseMap here because we need to
  // support all possible integer values -- there are no suitable tombstone
  // values for DenseMap.
  std::unordered_map<UInt128, uint64_t, Hasher<UInt128>> literal16Map;
  std::unordered_map<uint64_t, uint64_t> literal8Map;
  std::unordered_map<uint32_t, uint64_t> literal4Map;
};

class ObjCImageInfoSection final : public SyntheticSection {
public:
  ObjCImageInfoSection();
  bool isNeeded() const override { return !files.empty(); }
  uint64_t getSize() const override { return 8; }
  void addFile(const InputFile *file) {
    assert(!file->objCImageInfo.empty());
    files.push_back(file);
  }
  void finalizeContents();
  void writeTo(uint8_t *buf) const override;

private:
  struct ImageInfo {
    uint8_t swiftVersion = 0;
    bool hasCategoryClassProperties = false;
  } info;
  static ImageInfo parseImageInfo(const InputFile *);
  std::vector<const InputFile *> files; // files with image info
};

// This section stores 32-bit __TEXT segment offsets of initializer functions.
//
// The compiler stores pointers to initializers in __mod_init_func. These need
// to be fixed up at load time, which takes time and dirties memory. By
// synthesizing InitOffsetsSection from them, this data can live in the
// read-only __TEXT segment instead. This section is used by default when
// chained fixups are enabled.
//
// There is no similar counterpart to __mod_term_func, as that section is
// deprecated, and static destructors are instead handled by registering them
// via __cxa_atexit from an autogenerated initializer function (see D121736).
class InitOffsetsSection final : public SyntheticSection {
public:
  InitOffsetsSection();
  bool isNeeded() const override { return !sections.empty(); }
  uint64_t getSize() const override;
  void writeTo(uint8_t *buf) const override;
  void setUp();

  void addInput(ConcatInputSection *isec) { sections.push_back(isec); }
  const std::vector<ConcatInputSection *> &inputs() const { return sections; }

private:
  std::vector<ConcatInputSection *> sections;
};

// This SyntheticSection is for the __objc_methlist section, which contains
// relative method lists if the -objc_relative_method_lists option is enabled.
class ObjCMethListSection final : public SyntheticSection {
public:
  ObjCMethListSection();

  static bool isMethodList(const ConcatInputSection *isec);
  void addInput(ConcatInputSection *isec) { inputs.push_back(isec); }
  std::vector<ConcatInputSection *> getInputs() { return inputs; }

  void setUp();
  void finalize() override;
  bool isNeeded() const override { return !inputs.empty(); }
  uint64_t getSize() const override { return sectionSize; }
  void writeTo(uint8_t *bufStart) const override;

private:
  void readMethodListHeader(const uint8_t *buf, uint32_t &structSizeAndFlags,
                            uint32_t &structCount) const;
  void writeMethodListHeader(uint8_t *buf, uint32_t structSizeAndFlags,
                             uint32_t structCount) const;
  uint32_t computeRelativeMethodListSize(uint32_t absoluteMethodListSize) const;
  void writeRelativeOffsetForIsec(const ConcatInputSection *isec, uint8_t *buf,
                                  uint32_t &inSecOff, uint32_t &outSecOff,
                                  bool useSelRef) const;
  uint32_t writeRelativeMethodList(const ConcatInputSection *isec,
                                   uint8_t *buf) const;

  static constexpr uint32_t methodListHeaderSize =
      /*structSizeAndFlags*/ sizeof(uint32_t) +
      /*structCount*/ sizeof(uint32_t);
  // Relative method lists are supported only for 3-pointer method lists
  static constexpr uint32_t pointersPerStruct = 3;
  // The runtime identifies relative method lists via this magic value
  static constexpr uint32_t relMethodHeaderFlag = 0x80000000;
  // In the method list header, the first 2 bytes are the size of struct
  static constexpr uint32_t structSizeMask = 0x0000FFFF;
  // In the method list header, the last 2 bytes are the flags for the struct
  static constexpr uint32_t structFlagsMask = 0xFFFF0000;
  // Relative method lists have 4 byte alignment as all data in the InputSection
  // is 4 byte
  static constexpr uint32_t relativeOffsetSize = sizeof(uint32_t);

  // The output size of the __objc_methlist section, computed during finalize()
  uint32_t sectionSize = 0;
  std::vector<ConcatInputSection *> inputs;
};

// Chained fixups are a replacement for classic dyld opcodes. In this format,
// most of the metadata necessary for binding symbols and rebasing addresses is
// stored directly in the memory location that will have the fixup applied.
//
// The fixups form singly linked lists; each one covering a single page in
// memory. The __LINKEDIT,__chainfixups section stores the page offset of the
// first fixup of each page; the rest can be found by walking the chain using
// the offset that is embedded in each entry.
//
// This setup allows pages to be relocated lazily at page-in time and without
// being dirtied. The kernel can discard and load them again as needed. This
// technique, called page-in linking, was introduced in macOS 13.
//
// The benefits of this format are:
//  - smaller __LINKEDIT segment, as most of the fixup information is stored in
//    the data segment
//  - faster startup, since not all relocations need to be done upfront
//  - slightly lower memory usage, as fewer pages are dirtied
//
// Userspace x86_64 and arm64 binaries have two types of fixup entries:
//   - Rebase entries contain an absolute address, to which the object's load
//     address will be added to get the final value. This is used for loading
//     the address of a symbol defined in the same binary.
//   - Binding entries are mostly used for symbols imported from other dylibs,
//     but for weakly bound and interposable symbols as well. They are looked up
//     by a (symbol name, library) pair stored in __chainfixups. This import
//     entry also encodes whether the import is weak (i.e. if the symbol is
//     missing, it should be set to null instead of producing a load error).
//     The fixup encodes an ordinal associated with the import, and an optional
//     addend.
//
// The entries are tightly packed 64-bit bitfields. One of the bits specifies
// which kind of fixup to interpret them as.
//
// LLD generates the fixup data in 5 stages:
//   1. While scanning relocations, we make a note of each location that needs
//      a fixup by calling addRebase() or addBinding(). During this, we assign
//      a unique ordinal for each (symbol name, library, addend) import tuple.
//   2. After addresses have been assigned to all sections, and thus the memory
//      layout of the linked image is final; finalizeContents() is called. Here,
//      the page offsets of the chain start entries are calculated.
//   3. ChainedFixupsSection::writeTo() writes the page start offsets and the
//      imports table to the output file.
//   4. Each section's fixup entries are encoded and written to disk in
//      ConcatInputSection::writeTo(), but without writing the offsets that form
//      the chain.
//   5. Finally, each page's (which might correspond to multiple sections)
//      fixups are linked together in Writer::buildFixupChains().
class ChainedFixupsSection final : public LinkEditSection {
public:
  ChainedFixupsSection();
  void finalizeContents() override;
  uint64_t getRawSize() const override { return size; }
  bool isNeeded() const override;
  void writeTo(uint8_t *buf) const override;

  void addRebase(const InputSection *isec, uint64_t offset) {
    locations.emplace_back(isec, offset);
  }
  void addBinding(const Symbol *dysym, const InputSection *isec,
                  uint64_t offset, int64_t addend = 0);

  void setHasNonWeakDefinition() { hasNonWeakDef = true; }

  // Returns an (ordinal, inline addend) tuple used by dyld_chained_ptr_64_bind.
  std::pair<uint32_t, uint8_t> getBinding(const Symbol *sym,
                                          int64_t addend) const;

  const std::vector<Location> &getLocations() const { return locations; }

  bool hasWeakBinding() const { return hasWeakBind; }
  bool hasNonWeakDefinition() const { return hasNonWeakDef; }

private:
  // Location::offset initially stores the offset within an InputSection, but
  // contains output segment offsets after finalizeContents().
  std::vector<Location> locations;
  // (target symbol, addend) => import ordinal
  llvm::MapVector<std::pair<const Symbol *, int64_t>, uint32_t> bindings;

  struct SegmentInfo {
    SegmentInfo(const OutputSegment *oseg) : oseg(oseg) {}

    const OutputSegment *oseg;
    // (page index, fixup starts offset)
    llvm::SmallVector<std::pair<uint16_t, uint16_t>> pageStarts;

    size_t getSize() const;
    size_t writeTo(uint8_t *buf) const;
  };
  llvm::SmallVector<SegmentInfo, 4> fixupSegments;

  size_t symtabSize = 0;
  size_t size = 0;

  bool needsAddend = false;
  bool needsLargeAddend = false;
  bool hasWeakBind = false;
  bool hasNonWeakDef = false;
  llvm::MachO::ChainedImportFormat importFormat;
};

void writeChainedRebase(uint8_t *buf, uint64_t targetVA);
void writeChainedFixup(uint8_t *buf, const Symbol *sym, int64_t addend);

struct InStruct {
  const uint8_t *bufferStart = nullptr;
  MachHeaderSection *header = nullptr;
  CStringSection *cStringSection = nullptr;
  DeduplicatedCStringSection *objcMethnameSection = nullptr;
  WordLiteralSection *wordLiteralSection = nullptr;
  RebaseSection *rebase = nullptr;
  BindingSection *binding = nullptr;
  WeakBindingSection *weakBinding = nullptr;
  LazyBindingSection *lazyBinding = nullptr;
  ExportSection *exports = nullptr;
  GotSection *got = nullptr;
  TlvPointerSection *tlvPointers = nullptr;
  LazyPointerSection *lazyPointers = nullptr;
  StubsSection *stubs = nullptr;
  StubHelperSection *stubHelper = nullptr;
  ObjCStubsSection *objcStubs = nullptr;
  UnwindInfoSection *unwindInfo = nullptr;
  ObjCImageInfoSection *objCImageInfo = nullptr;
  ConcatInputSection *imageLoaderCache = nullptr;
  InitOffsetsSection *initOffsets = nullptr;
  ObjCMethListSection *objcMethList = nullptr;
  ChainedFixupsSection *chainedFixups = nullptr;
};

extern InStruct in;
extern std::vector<SyntheticSection *> syntheticSections;

void createSyntheticSymbols();

} // namespace lld::macho

#endif
