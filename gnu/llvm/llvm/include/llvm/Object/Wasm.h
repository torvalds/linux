//===- Wasm.h - Wasm object file implementation -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the WasmObjectFile class, which implements the ObjectFile
// interface for Wasm files.
//
// See: https://github.com/WebAssembly/design/blob/main/BinaryEncoding.md
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_WASM_H
#define LLVM_OBJECT_WASM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace llvm {
namespace object {

class WasmSymbol {
public:
  WasmSymbol(const wasm::WasmSymbolInfo &Info,
             const wasm::WasmGlobalType *GlobalType,
             const wasm::WasmTableType *TableType,
             const wasm::WasmSignature *Signature)
      : Info(Info), GlobalType(GlobalType), TableType(TableType),
        Signature(Signature) {
    assert(!Signature || Signature->Kind != wasm::WasmSignature::Placeholder);
  }

  // Symbol info as represented in the symbol's 'syminfo' entry of an object
  // file's symbol table.
  wasm::WasmSymbolInfo Info;
  const wasm::WasmGlobalType *GlobalType;
  const wasm::WasmTableType *TableType;
  const wasm::WasmSignature *Signature;

  bool isTypeFunction() const {
    return Info.Kind == wasm::WASM_SYMBOL_TYPE_FUNCTION;
  }

  bool isTypeTable() const { return Info.Kind == wasm::WASM_SYMBOL_TYPE_TABLE; }

  bool isTypeData() const { return Info.Kind == wasm::WASM_SYMBOL_TYPE_DATA; }

  bool isTypeGlobal() const {
    return Info.Kind == wasm::WASM_SYMBOL_TYPE_GLOBAL;
  }

  bool isTypeSection() const {
    return Info.Kind == wasm::WASM_SYMBOL_TYPE_SECTION;
  }

  bool isTypeTag() const { return Info.Kind == wasm::WASM_SYMBOL_TYPE_TAG; }

  bool isDefined() const { return !isUndefined(); }

  bool isUndefined() const {
    return (Info.Flags & wasm::WASM_SYMBOL_UNDEFINED) != 0;
  }

  bool isBindingWeak() const {
    return getBinding() == wasm::WASM_SYMBOL_BINDING_WEAK;
  }

  bool isBindingGlobal() const {
    return getBinding() == wasm::WASM_SYMBOL_BINDING_GLOBAL;
  }

  bool isBindingLocal() const {
    return getBinding() == wasm::WASM_SYMBOL_BINDING_LOCAL;
  }

  unsigned getBinding() const {
    return Info.Flags & wasm::WASM_SYMBOL_BINDING_MASK;
  }

  bool isHidden() const {
    return getVisibility() == wasm::WASM_SYMBOL_VISIBILITY_HIDDEN;
  }

  unsigned getVisibility() const {
    return Info.Flags & wasm::WASM_SYMBOL_VISIBILITY_MASK;
  }

  void print(raw_ostream &Out) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const;
#endif
};

struct WasmSection {
  WasmSection() = default;

  uint32_t Type = 0;
  uint32_t Offset = 0;       // Offset within the file
  StringRef Name;            // Section name (User-defined sections only)
  uint32_t Comdat = UINT32_MAX; // From the "comdat info" section
  ArrayRef<uint8_t> Content;
  std::vector<wasm::WasmRelocation> Relocations;
  // Length of the LEB encoding of the section header's size field
  std::optional<uint8_t> HeaderSecSizeEncodingLen;
};

struct WasmSegment {
  uint32_t SectionOffset;
  wasm::WasmDataSegment Data;
};

class WasmObjectFile : public ObjectFile {

public:
  WasmObjectFile(MemoryBufferRef Object, Error &Err);

  const wasm::WasmObjectHeader &getHeader() const;
  const WasmSymbol &getWasmSymbol(const DataRefImpl &Symb) const;
  const WasmSymbol &getWasmSymbol(const SymbolRef &Symbol) const;
  const WasmSection &getWasmSection(const SectionRef &Section) const;
  const wasm::WasmRelocation &getWasmRelocation(const RelocationRef &Ref) const;

  static bool classof(const Binary *v) { return v->isWasm(); }

  const wasm::WasmDylinkInfo &dylinkInfo() const { return DylinkInfo; }
  const wasm::WasmProducerInfo &getProducerInfo() const { return ProducerInfo; }
  ArrayRef<wasm::WasmFeatureEntry> getTargetFeatures() const {
    return TargetFeatures;
  }
  ArrayRef<wasm::WasmSignature> types() const { return Signatures; }
  ArrayRef<wasm::WasmImport> imports() const { return Imports; }
  ArrayRef<wasm::WasmTable> tables() const { return Tables; }
  ArrayRef<wasm::WasmLimits> memories() const { return Memories; }
  ArrayRef<wasm::WasmGlobal> globals() const { return Globals; }
  ArrayRef<wasm::WasmTag> tags() const { return Tags; }
  ArrayRef<wasm::WasmExport> exports() const { return Exports; }
  const wasm::WasmLinkingData &linkingData() const { return LinkingData; }
  uint32_t getNumberOfSymbols() const { return Symbols.size(); }
  ArrayRef<wasm::WasmElemSegment> elements() const { return ElemSegments; }
  ArrayRef<WasmSegment> dataSegments() const { return DataSegments; }
  ArrayRef<wasm::WasmFunction> functions() const { return Functions; }
  ArrayRef<wasm::WasmDebugName> debugNames() const { return DebugNames; }
  uint32_t startFunction() const { return StartFunction; }
  uint32_t getNumImportedGlobals() const { return NumImportedGlobals; }
  uint32_t getNumImportedTables() const { return NumImportedTables; }
  uint32_t getNumImportedFunctions() const { return NumImportedFunctions; }
  uint32_t getNumImportedTags() const { return NumImportedTags; }
  uint32_t getNumSections() const { return Sections.size(); }
  void moveSymbolNext(DataRefImpl &Symb) const override;

  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;

  basic_symbol_iterator symbol_begin() const override;

  basic_symbol_iterator symbol_end() const override;
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;

  bool is64Bit() const override { return false; }

  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  uint64_t getWasmSymbolValue(const WasmSymbol &Sym) const;
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  uint32_t getSymbolAlignment(DataRefImpl Symb) const override;
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;
  uint32_t getSymbolSectionId(SymbolRef Sym) const;
  uint32_t getSymbolSize(SymbolRef Sym) const;

  // Overrides from SectionRef.
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
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;

  // Overrides from RelocationRef.
  void moveRelocationNext(DataRefImpl &Rel) const override;
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;

  section_iterator section_begin() const override;
  section_iterator section_end() const override;
  uint8_t getBytesInAddress() const override;
  StringRef getFileFormatName() const override;
  Triple::ArchType getArch() const override;
  Expected<SubtargetFeatures> getFeatures() const override;
  bool isRelocatableObject() const override;
  bool isSharedObject() const;
  bool hasUnmodeledTypes() const { return HasUnmodeledTypes; }

  struct ReadContext {
    const uint8_t *Start;
    const uint8_t *Ptr;
    const uint8_t *End;
  };

private:
  bool isValidFunctionIndex(uint32_t Index) const;
  bool isDefinedFunctionIndex(uint32_t Index) const;
  bool isValidGlobalIndex(uint32_t Index) const;
  bool isValidTableNumber(uint32_t Index) const;
  bool isDefinedGlobalIndex(uint32_t Index) const;
  bool isDefinedTableNumber(uint32_t Index) const;
  bool isValidTagIndex(uint32_t Index) const;
  bool isDefinedTagIndex(uint32_t Index) const;
  bool isValidFunctionSymbol(uint32_t Index) const;
  bool isValidTableSymbol(uint32_t Index) const;
  bool isValidGlobalSymbol(uint32_t Index) const;
  bool isValidTagSymbol(uint32_t Index) const;
  bool isValidDataSymbol(uint32_t Index) const;
  bool isValidSectionSymbol(uint32_t Index) const;
  wasm::WasmFunction &getDefinedFunction(uint32_t Index);
  const wasm::WasmFunction &getDefinedFunction(uint32_t Index) const;
  const wasm::WasmGlobal &getDefinedGlobal(uint32_t Index) const;
  wasm::WasmTag &getDefinedTag(uint32_t Index);

  const WasmSection &getWasmSection(DataRefImpl Ref) const;
  const wasm::WasmRelocation &getWasmRelocation(DataRefImpl Ref) const;
  uint32_t getSymbolSectionIdImpl(const WasmSymbol &Symb) const;

  Error parseSection(WasmSection &Sec);
  Error parseCustomSection(WasmSection &Sec, ReadContext &Ctx);

  // Standard section types
  Error parseTypeSection(ReadContext &Ctx);
  Error parseImportSection(ReadContext &Ctx);
  Error parseFunctionSection(ReadContext &Ctx);
  Error parseTableSection(ReadContext &Ctx);
  Error parseMemorySection(ReadContext &Ctx);
  Error parseTagSection(ReadContext &Ctx);
  Error parseGlobalSection(ReadContext &Ctx);
  Error parseExportSection(ReadContext &Ctx);
  Error parseStartSection(ReadContext &Ctx);
  Error parseElemSection(ReadContext &Ctx);
  Error parseCodeSection(ReadContext &Ctx);
  Error parseDataSection(ReadContext &Ctx);
  Error parseDataCountSection(ReadContext &Ctx);

  // Custom section types
  Error parseDylinkSection(ReadContext &Ctx);
  Error parseDylink0Section(ReadContext &Ctx);
  Error parseNameSection(ReadContext &Ctx);
  Error parseLinkingSection(ReadContext &Ctx);
  Error parseLinkingSectionSymtab(ReadContext &Ctx);
  Error parseLinkingSectionComdat(ReadContext &Ctx);
  Error parseProducersSection(ReadContext &Ctx);
  Error parseTargetFeaturesSection(ReadContext &Ctx);
  Error parseRelocSection(StringRef Name, ReadContext &Ctx);

  wasm::WasmObjectHeader Header;
  std::vector<WasmSection> Sections;
  wasm::WasmDylinkInfo DylinkInfo;
  wasm::WasmProducerInfo ProducerInfo;
  std::vector<wasm::WasmFeatureEntry> TargetFeatures;
  std::vector<wasm::WasmSignature> Signatures;
  std::vector<wasm::WasmTable> Tables;
  std::vector<wasm::WasmLimits> Memories;
  std::vector<wasm::WasmGlobal> Globals;
  std::vector<wasm::WasmTag> Tags;
  std::vector<wasm::WasmImport> Imports;
  std::vector<wasm::WasmExport> Exports;
  std::vector<wasm::WasmElemSegment> ElemSegments;
  std::vector<WasmSegment> DataSegments;
  std::optional<size_t> DataCount;
  std::vector<wasm::WasmFunction> Functions;
  std::vector<WasmSymbol> Symbols;
  std::vector<wasm::WasmDebugName> DebugNames;
  uint32_t StartFunction = -1;
  bool HasLinkingSection = false;
  bool HasDylinkSection = false;
  bool HasMemory64 = false;
  bool HasUnmodeledTypes = false;
  wasm::WasmLinkingData LinkingData;
  uint32_t NumImportedGlobals = 0;
  uint32_t NumImportedTables = 0;
  uint32_t NumImportedFunctions = 0;
  uint32_t NumImportedTags = 0;
  uint32_t CodeSection = 0;
  uint32_t DataSection = 0;
  uint32_t TagSection = 0;
  uint32_t GlobalSection = 0;
  uint32_t TableSection = 0;
};

class WasmSectionOrderChecker {
public:
  // We define orders for all core wasm sections and known custom sections.
  enum : int {
    // Sentinel, must be zero
    WASM_SEC_ORDER_NONE = 0,

    // Core sections
    WASM_SEC_ORDER_TYPE,
    WASM_SEC_ORDER_IMPORT,
    WASM_SEC_ORDER_FUNCTION,
    WASM_SEC_ORDER_TABLE,
    WASM_SEC_ORDER_MEMORY,
    WASM_SEC_ORDER_TAG,
    WASM_SEC_ORDER_GLOBAL,
    WASM_SEC_ORDER_EXPORT,
    WASM_SEC_ORDER_START,
    WASM_SEC_ORDER_ELEM,
    WASM_SEC_ORDER_DATACOUNT,
    WASM_SEC_ORDER_CODE,
    WASM_SEC_ORDER_DATA,

    // Custom sections
    // "dylink" should be the very first section in the module
    WASM_SEC_ORDER_DYLINK,
    // "linking" section requires DATA section in order to validate data symbols
    WASM_SEC_ORDER_LINKING,
    // Must come after "linking" section in order to validate reloc indexes.
    WASM_SEC_ORDER_RELOC,
    // "name" section must appear after DATA. Comes after "linking" to allow
    // symbol table to set default function name.
    WASM_SEC_ORDER_NAME,
    // "producers" section must appear after "name" section.
    WASM_SEC_ORDER_PRODUCERS,
    // "target_features" section must appear after producers section
    WASM_SEC_ORDER_TARGET_FEATURES,

    // Must be last
    WASM_NUM_SEC_ORDERS

  };

  // Sections that may or may not be present, but cannot be predecessors
  static int DisallowedPredecessors[WASM_NUM_SEC_ORDERS][WASM_NUM_SEC_ORDERS];

  bool isValidSectionOrder(unsigned ID, StringRef CustomSectionName = "");

private:
  bool Seen[WASM_NUM_SEC_ORDERS] = {}; // Sections that have been seen already

  // Returns -1 for unknown sections.
  int getSectionOrder(unsigned ID, StringRef CustomSectionName = "");
};

} // end namespace object

inline raw_ostream &operator<<(raw_ostream &OS, const object::WasmSymbol &Sym) {
  Sym.print(OS);
  return OS;
}

} // end namespace llvm

#endif // LLVM_OBJECT_WASM_H
