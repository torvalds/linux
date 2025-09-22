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
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_SYNTHETIC_SECTIONS_H
#define LLD_WASM_SYNTHETIC_SECTIONS_H

#include "OutputSections.h"

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/BinaryFormat/WasmTraits.h"
#include <optional>

#define DEBUG_TYPE "lld"

namespace lld::wasm {

// An init entry to be written to either the synthetic init func or the
// linking metadata.
struct WasmInitEntry {
  const FunctionSymbol *sym;
  uint32_t priority;
};

class SyntheticSection : public OutputSection {
public:
  SyntheticSection(uint32_t type, std::string name = "")
      : OutputSection(type, name), bodyOutputStream(body) {
    if (!name.empty())
      writeStr(bodyOutputStream, name, "section name");
  }

  void writeTo(uint8_t *buf) override {
    assert(offset);
    log("writing " + toString(*this));
    memcpy(buf + offset, header.data(), header.size());
    memcpy(buf + offset + header.size(), body.data(), body.size());
  }

  size_t getSize() const override { return header.size() + body.size(); }

  virtual void writeBody() {}

  virtual void assignIndexes() {}

  void finalizeContents() override {
    writeBody();
    bodyOutputStream.flush();
    createHeader(body.size());
  }

  raw_ostream &getStream() { return bodyOutputStream; }

  std::string body;

protected:
  llvm::raw_string_ostream bodyOutputStream;
};

// Create the custom "dylink" section containing information for the dynamic
// linker.
// See
// https://github.com/WebAssembly/tool-conventions/blob/main/DynamicLinking.md
class DylinkSection : public SyntheticSection {
public:
  DylinkSection() : SyntheticSection(llvm::wasm::WASM_SEC_CUSTOM, "dylink.0") {}
  bool isNeeded() const override;
  void writeBody() override;

  uint32_t memAlign = 0;
  uint32_t memSize = 0;
};

class TypeSection : public SyntheticSection {
public:
  TypeSection() : SyntheticSection(llvm::wasm::WASM_SEC_TYPE) {}

  bool isNeeded() const override { return types.size() > 0; };
  void writeBody() override;
  uint32_t registerType(const WasmSignature &sig);
  uint32_t lookupType(const WasmSignature &sig);

protected:
  std::vector<const WasmSignature *> types;
  llvm::DenseMap<WasmSignature, int32_t> typeIndices;
};

/**
 * A key for some kind of imported entity of type `T`.
 *
 * Used when de-duplicating imports.
 */
template <typename T> struct ImportKey {
public:
  enum class State { Plain, Empty, Tombstone };

public:
  T type;
  std::optional<StringRef> importModule;
  std::optional<StringRef> importName;
  State state;

public:
  ImportKey(T type) : type(type), state(State::Plain) {}
  ImportKey(T type, State state) : type(type), state(state) {}
  ImportKey(T type, std::optional<StringRef> importModule,
            std::optional<StringRef> importName)
      : type(type), importModule(importModule), importName(importName),
        state(State::Plain) {}
};

template <typename T>
inline bool operator==(const ImportKey<T> &lhs, const ImportKey<T> &rhs) {
  return lhs.state == rhs.state && lhs.importModule == rhs.importModule &&
         lhs.importName == rhs.importName && lhs.type == rhs.type;
}

} // namespace wasm::lld

// `ImportKey<T>` can be used as a key in a `DenseMap` if `T` can be used as a
// key in a `DenseMap`.
namespace llvm {
template <typename T> struct DenseMapInfo<lld::wasm::ImportKey<T>> {
  static lld::wasm::ImportKey<T> getEmptyKey() {
    typename lld::wasm::ImportKey<T> key(llvm::DenseMapInfo<T>::getEmptyKey());
    key.state = lld::wasm::ImportKey<T>::State::Empty;
    return key;
  }
  static lld::wasm::ImportKey<T> getTombstoneKey() {
    typename lld::wasm::ImportKey<T> key(llvm::DenseMapInfo<T>::getEmptyKey());
    key.state = lld::wasm::ImportKey<T>::State::Tombstone;
    return key;
  }
  static unsigned getHashValue(const lld::wasm::ImportKey<T> &key) {
    uintptr_t hash = hash_value(key.importModule);
    hash = hash_combine(hash, key.importName);
    hash = hash_combine(hash, llvm::DenseMapInfo<T>::getHashValue(key.type));
    hash = hash_combine(hash, key.state);
    return hash;
  }
  static bool isEqual(const lld::wasm::ImportKey<T> &lhs,
                      const lld::wasm::ImportKey<T> &rhs) {
    return lhs == rhs;
  }
};
} // end namespace llvm

namespace lld {
namespace wasm {

class ImportSection : public SyntheticSection {
public:
  ImportSection() : SyntheticSection(llvm::wasm::WASM_SEC_IMPORT) {}
  bool isNeeded() const override { return getNumImports() > 0; }
  void writeBody() override;
  void addImport(Symbol *sym);
  void addGOTEntry(Symbol *sym);
  void seal() { isSealed = true; }
  uint32_t getNumImports() const;
  uint32_t getNumImportedGlobals() const {
    assert(isSealed);
    return numImportedGlobals;
  }
  uint32_t getNumImportedFunctions() const {
    assert(isSealed);
    return numImportedFunctions;
  }
  uint32_t getNumImportedTags() const {
    assert(isSealed);
    return numImportedTags;
  }
  uint32_t getNumImportedTables() const {
    assert(isSealed);
    return numImportedTables;
  }

  std::vector<const Symbol *> importedSymbols;
  std::vector<const Symbol *> gotSymbols;

protected:
  bool isSealed = false;
  unsigned numImportedGlobals = 0;
  unsigned numImportedFunctions = 0;
  unsigned numImportedTags = 0;
  unsigned numImportedTables = 0;
  llvm::DenseMap<ImportKey<WasmGlobalType>, uint32_t> importedGlobals;
  llvm::DenseMap<ImportKey<WasmSignature>, uint32_t> importedFunctions;
  llvm::DenseMap<ImportKey<WasmTableType>, uint32_t> importedTables;
  llvm::DenseMap<ImportKey<WasmSignature>, uint32_t> importedTags;
};

class FunctionSection : public SyntheticSection {
public:
  FunctionSection() : SyntheticSection(llvm::wasm::WASM_SEC_FUNCTION) {}

  bool isNeeded() const override { return inputFunctions.size() > 0; };
  void writeBody() override;
  void addFunction(InputFunction *func);

  std::vector<InputFunction *> inputFunctions;

protected:
};

class TableSection : public SyntheticSection {
public:
  TableSection() : SyntheticSection(llvm::wasm::WASM_SEC_TABLE) {}

  bool isNeeded() const override { return inputTables.size() > 0; };
  void assignIndexes() override;
  void writeBody() override;
  void addTable(InputTable *table);

  std::vector<InputTable *> inputTables;
};

class MemorySection : public SyntheticSection {
public:
  MemorySection() : SyntheticSection(llvm::wasm::WASM_SEC_MEMORY) {}

  bool isNeeded() const override { return !config->memoryImport.has_value(); }
  void writeBody() override;

  uint64_t numMemoryPages = 0;
  uint64_t maxMemoryPages = 0;
};

// The tag section contains a list of declared wasm tags associated with the
// module. Currently the only supported tag kind is exceptions. All C++
// exceptions are represented by a single tag. A tag entry in this section
// contains information on what kind of tag it is (e.g. exception) and the type
// of values associated with the tag. (In Wasm, a tag can contain multiple
// values of primitive types. But for C++ exceptions, we just throw a pointer
// which is an i32 value (for wasm32 architecture), so the signature of C++
// exception is (i32)->(void), because all exception tag types are assumed to
// have void return type to share WasmSignature with functions.)
class TagSection : public SyntheticSection {
public:
  TagSection() : SyntheticSection(llvm::wasm::WASM_SEC_TAG) {}
  void writeBody() override;
  bool isNeeded() const override { return inputTags.size() > 0; }
  void addTag(InputTag *tag);

  std::vector<InputTag *> inputTags;
};

class GlobalSection : public SyntheticSection {
public:
  GlobalSection() : SyntheticSection(llvm::wasm::WASM_SEC_GLOBAL) {}

  static bool classof(const OutputSection *sec) {
    return sec->type == llvm::wasm::WASM_SEC_GLOBAL;
  }

  uint32_t numGlobals() const {
    assert(isSealed);
    return inputGlobals.size() + dataAddressGlobals.size() +
           internalGotSymbols.size();
  }
  bool isNeeded() const override { return numGlobals() > 0; }
  void assignIndexes() override;
  void writeBody() override;
  void addGlobal(InputGlobal *global);

  // Add an internal GOT entry global that corresponds to the given symbol.
  // Normally GOT entries are imported and assigned by the external dynamic
  // linker.  However, when linking PIC code statically or when linking with
  // -Bsymbolic we can internalize GOT entries by declaring globals the hold
  // symbol addresses.
  //
  // For the static linking case these internal globals can be completely
  // eliminated by a post-link optimizer such as wasm-opt.
  //
  // TODO(sbc): Another approach to optimizing these away could be to use
  // specific relocation types combined with linker relaxation which could
  // transform a `global.get` to an `i32.const`.
  void addInternalGOTEntry(Symbol *sym);
  bool needsRelocations() {
    if (config->extendedConst)
      return false;
    return llvm::any_of(internalGotSymbols,
                        [=](Symbol *sym) { return !sym->isTLS(); });
  }
  bool needsTLSRelocations() {
    return llvm::any_of(internalGotSymbols,
                        [=](Symbol *sym) { return sym->isTLS(); });
  }
  void generateRelocationCode(raw_ostream &os, bool TLS) const;

  std::vector<DefinedData *> dataAddressGlobals;
  std::vector<InputGlobal *> inputGlobals;
  std::vector<Symbol *> internalGotSymbols;

protected:
  bool isSealed = false;
};

class ExportSection : public SyntheticSection {
public:
  ExportSection() : SyntheticSection(llvm::wasm::WASM_SEC_EXPORT) {}
  bool isNeeded() const override { return exports.size() > 0; }
  void writeBody() override;

  std::vector<llvm::wasm::WasmExport> exports;
  std::vector<const Symbol *> exportedSymbols;
};

class StartSection : public SyntheticSection {
public:
  StartSection() : SyntheticSection(llvm::wasm::WASM_SEC_START) {}
  bool isNeeded() const override;
  void writeBody() override;
};

class ElemSection : public SyntheticSection {
public:
  ElemSection()
      : SyntheticSection(llvm::wasm::WASM_SEC_ELEM) {}
  bool isNeeded() const override { return indirectFunctions.size() > 0; };
  void writeBody() override;
  void addEntry(FunctionSymbol *sym);
  uint32_t numEntries() const { return indirectFunctions.size(); }

protected:
  std::vector<const FunctionSymbol *> indirectFunctions;
};

class DataCountSection : public SyntheticSection {
public:
  DataCountSection(ArrayRef<OutputSegment *> segments);
  bool isNeeded() const override;
  void writeBody() override;

protected:
  uint32_t numSegments;
};

// Create the custom "linking" section containing linker metadata.
// This is only created when relocatable output is requested.
class LinkingSection : public SyntheticSection {
public:
  LinkingSection(const std::vector<WasmInitEntry> &initFunctions,
                 const std::vector<OutputSegment *> &dataSegments)
      : SyntheticSection(llvm::wasm::WASM_SEC_CUSTOM, "linking"),
        initFunctions(initFunctions), dataSegments(dataSegments) {}
  bool isNeeded() const override {
    return config->relocatable || config->emitRelocs;
  }
  void writeBody() override;
  void addToSymtab(Symbol *sym);

protected:
  std::vector<const Symbol *> symtabEntries;
  llvm::StringMap<uint32_t> sectionSymbolIndices;
  const std::vector<WasmInitEntry> &initFunctions;
  const std::vector<OutputSegment *> &dataSegments;
};

// Create the custom "name" section containing debug symbol names.
class NameSection : public SyntheticSection {
public:
  NameSection(ArrayRef<OutputSegment *> segments)
      : SyntheticSection(llvm::wasm::WASM_SEC_CUSTOM, "name"),
        segments(segments) {}
  bool isNeeded() const override {
    if (config->stripAll && !config->keepSections.count(name))
      return false;
    return numNames() > 0;
  }
  void writeBody() override;
  unsigned numNames() const {
    // We always write at least one name which is the name of the
    // module itself.
    return 1 + numNamedGlobals() + numNamedFunctions();
  }
  unsigned numNamedGlobals() const;
  unsigned numNamedFunctions() const;
  unsigned numNamedDataSegments() const;

protected:
  ArrayRef<OutputSegment *> segments;
};

class ProducersSection : public SyntheticSection {
public:
  ProducersSection()
      : SyntheticSection(llvm::wasm::WASM_SEC_CUSTOM, "producers") {}
  bool isNeeded() const override {
    if (config->stripAll && !config->keepSections.count(name))
      return false;
    return fieldCount() > 0;
  }
  void writeBody() override;
  void addInfo(const llvm::wasm::WasmProducerInfo &info);

protected:
  int fieldCount() const {
    return int(!languages.empty()) + int(!tools.empty()) + int(!sDKs.empty());
  }
  SmallVector<std::pair<std::string, std::string>, 8> languages;
  SmallVector<std::pair<std::string, std::string>, 8> tools;
  SmallVector<std::pair<std::string, std::string>, 8> sDKs;
};

class TargetFeaturesSection : public SyntheticSection {
public:
  TargetFeaturesSection()
      : SyntheticSection(llvm::wasm::WASM_SEC_CUSTOM, "target_features") {}
  bool isNeeded() const override {
    if (config->stripAll && !config->keepSections.count(name))
      return false;
    return features.size() > 0;
  }
  void writeBody() override;

  llvm::SmallSet<std::string, 8> features;
};

class RelocSection : public SyntheticSection {
public:
  RelocSection(StringRef name, OutputSection *sec)
      : SyntheticSection(llvm::wasm::WASM_SEC_CUSTOM, std::string(name)),
        sec(sec) {}
  void writeBody() override;
  bool isNeeded() const override { return sec->getNumRelocations() > 0; };

protected:
  OutputSection *sec;
};

class BuildIdSection : public SyntheticSection {
public:
  BuildIdSection();
  void writeBody() override;
  bool isNeeded() const override {
    return config->buildId != BuildIdKind::None;
  }
  void writeBuildId(llvm::ArrayRef<uint8_t> buf);
  void writeTo(uint8_t *buf) override {
    LLVM_DEBUG(llvm::dbgs()
               << "BuildId writeto buf " << buf << " offset " << offset
               << " headersize " << header.size() << '\n');
    // The actual build ID is derived from a hash of all of the output
    // sections, so it can't be calculated until they are written. Here
    // we write the section leaving zeros in place of the hash.
    SyntheticSection::writeTo(buf);
    // Calculate and store the location where the hash will be written.
    hashPlaceholderPtr = buf + offset + header.size() +
                         +sizeof(buildIdSectionName) /*name string*/ +
                         1 /* hash size */;
  }

  const uint32_t hashSize;

private:
  static constexpr char buildIdSectionName[] = "build_id";
  uint8_t *hashPlaceholderPtr = nullptr;
};

// Linker generated output sections
struct OutStruct {
  DylinkSection *dylinkSec;
  TypeSection *typeSec;
  FunctionSection *functionSec;
  ImportSection *importSec;
  TableSection *tableSec;
  MemorySection *memorySec;
  GlobalSection *globalSec;
  TagSection *tagSec;
  ExportSection *exportSec;
  StartSection *startSec;
  ElemSection *elemSec;
  DataCountSection *dataCountSec;
  LinkingSection *linkingSec;
  NameSection *nameSec;
  ProducersSection *producersSec;
  TargetFeaturesSection *targetFeaturesSec;
  BuildIdSection *buildIdSec;
};

extern OutStruct out;

} // namespace wasm
} // namespace lld

#endif
