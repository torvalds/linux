//===- InputChunks.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An InputChunks represents an indivisible opaque region of a input wasm file.
// i.e. a single wasm data segment or a single wasm function.
//
// They are written directly to the mmap'd output file after which relocations
// are applied.  Because each Chunk is independent they can be written in
// parallel.
//
// Chunks are also unit on which garbage collection (--gc-sections) operates.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_INPUT_CHUNKS_H
#define LLD_WASM_INPUT_CHUNKS_H

#include "Config.h"
#include "InputFiles.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Object/Wasm.h"
#include <optional>

namespace lld {
namespace wasm {

class ObjFile;
class OutputSegment;
class OutputSection;

class InputChunk {
public:
  enum Kind {
    DataSegment,
    Merge,
    MergedChunk,
    Function,
    SyntheticFunction,
    Section,
  };

  StringRef name;
  StringRef debugName;

  Kind kind() const { return (Kind)sectionKind; }

  uint32_t getSize() const;
  uint32_t getInputSize() const;

  void writeTo(uint8_t *buf) const;
  void relocate(uint8_t *buf) const;

  ArrayRef<WasmRelocation> getRelocations() const { return relocations; }
  void setRelocations(ArrayRef<WasmRelocation> rs) { relocations = rs; }

  // Translate an offset into the input chunk to an offset in the output
  // section.
  uint64_t getOffset(uint64_t offset) const;
  // Translate an offset into the input chunk into an offset into the output
  // chunk.  For data segments (InputSegment) this will return and offset into
  // the output segment.  For MergeInputChunk, this will return an offset into
  // the parent merged chunk.  For other chunk types this is no-op and we just
  // return unmodified offset.
  uint64_t getChunkOffset(uint64_t offset) const;
  uint64_t getVA(uint64_t offset = 0) const;

  uint32_t getComdat() const { return comdat; }
  StringRef getComdatName() const;
  uint32_t getInputSectionOffset() const { return inputSectionOffset; }

  size_t getNumRelocations() const { return relocations.size(); }
  void writeRelocations(llvm::raw_ostream &os) const;
  void generateRelocationCode(raw_ostream &os) const;

  bool isTLS() const { return flags & llvm::wasm::WASM_SEG_FLAG_TLS; }
  bool isRetained() const { return flags & llvm::wasm::WASM_SEG_FLAG_RETAIN; }

  ObjFile *file;
  OutputSection *outputSec = nullptr;
  uint32_t comdat = UINT32_MAX;
  uint32_t inputSectionOffset = 0;
  uint32_t alignment;
  uint32_t flags;

  // Only applies to data segments.
  uint32_t outputSegmentOffset = 0;
  const OutputSegment *outputSeg = nullptr;

  // After assignAddresses is called, this represents the offset from
  // the beginning of the output section this chunk was assigned to.
  int32_t outSecOff = 0;

  uint8_t sectionKind : 3;

  // Signals that the section is part of the output.  The garbage collector,
  // and COMDAT handling can set a sections' Live bit.
  // If GC is disabled, all sections start out as live by default.
  unsigned live : 1;

  // Signals the chunk was discarded by COMDAT handling.
  unsigned discarded : 1;

protected:
  InputChunk(ObjFile *f, Kind k, StringRef name, uint32_t alignment = 0,
             uint32_t flags = 0)
      : name(name), file(f), alignment(alignment), flags(flags), sectionKind(k),
        live(!config->gcSections), discarded(false) {}
  ArrayRef<uint8_t> data() const { return rawData; }
  uint64_t getTombstone() const;

  ArrayRef<WasmRelocation> relocations;
  ArrayRef<uint8_t> rawData;
};

// Represents a WebAssembly data segment which can be included as part of
// an output data segments.  Note that in WebAssembly, unlike ELF and other
// formats, used the term "data segment" to refer to the continuous regions of
// memory that make on the data section. See:
// https://webassembly.github.io/spec/syntax/modules.html#syntax-data
//
// For example, by default, clang will produce a separate data section for
// each global variable.
class InputSegment : public InputChunk {
public:
  InputSegment(const WasmSegment &seg, ObjFile *f)
      : InputChunk(f, InputChunk::DataSegment, seg.Data.Name,
                   seg.Data.Alignment, seg.Data.LinkingFlags),
        segment(seg) {
    rawData = segment.Data.Content;
    comdat = segment.Data.Comdat;
    inputSectionOffset = segment.SectionOffset;
  }

  static bool classof(const InputChunk *c) { return c->kind() == DataSegment; }

protected:
  const WasmSegment &segment;
};

class SyntheticMergedChunk;

// Merge segment handling copied from lld/ELF/InputSection.h.  Keep in sync
// where possible.

// SectionPiece represents a piece of splittable segment contents.
// We allocate a lot of these and binary search on them. This means that they
// have to be as compact as possible, which is why we don't store the size (can
// be found by looking at the next one).
struct SectionPiece {
  SectionPiece(size_t off, uint32_t hash, bool live)
      : inputOff(off), live(live || !config->gcSections), hash(hash >> 1) {}

  uint32_t inputOff;
  uint32_t live : 1;
  uint32_t hash : 31;
  uint64_t outputOff = 0;
};

static_assert(sizeof(SectionPiece) == 16, "SectionPiece is too big");

// This corresponds segments marked as WASM_SEG_FLAG_STRINGS.
class MergeInputChunk : public InputChunk {
public:
  MergeInputChunk(const WasmSegment &seg, ObjFile *f)
      : InputChunk(f, Merge, seg.Data.Name, seg.Data.Alignment,
                   seg.Data.LinkingFlags) {
    rawData = seg.Data.Content;
    comdat = seg.Data.Comdat;
    inputSectionOffset = seg.SectionOffset;
  }

  MergeInputChunk(const WasmSection &s, ObjFile *f)
      : InputChunk(f, Merge, s.Name, 0, llvm::wasm::WASM_SEG_FLAG_STRINGS) {
    assert(s.Type == llvm::wasm::WASM_SEC_CUSTOM);
    comdat = s.Comdat;
    rawData = s.Content;
  }

  static bool classof(const InputChunk *s) { return s->kind() == Merge; }
  void splitIntoPieces();

  // Translate an offset in the input section to an offset in the parent
  // MergeSyntheticSection.
  uint64_t getParentOffset(uint64_t offset) const;

  // Splittable sections are handled as a sequence of data
  // rather than a single large blob of data.
  std::vector<SectionPiece> pieces;

  // Returns I'th piece's data. This function is very hot when
  // string merging is enabled, so we want to inline.
  LLVM_ATTRIBUTE_ALWAYS_INLINE
  llvm::CachedHashStringRef getData(size_t i) const {
    size_t begin = pieces[i].inputOff;
    size_t end =
        (pieces.size() - 1 == i) ? data().size() : pieces[i + 1].inputOff;
    return {toStringRef(data().slice(begin, end - begin)), pieces[i].hash};
  }

  // Returns the SectionPiece at a given input section offset.
  SectionPiece *getSectionPiece(uint64_t offset);
  const SectionPiece *getSectionPiece(uint64_t offset) const {
    return const_cast<MergeInputChunk *>(this)->getSectionPiece(offset);
  }

  SyntheticMergedChunk *parent = nullptr;

private:
  void splitStrings(ArrayRef<uint8_t> a);
};

// SyntheticMergedChunk is a class that allows us to put mergeable
// sections with different attributes in a single output sections. To do that we
// put them into SyntheticMergedChunk synthetic input sections which are
// attached to regular output sections.
class SyntheticMergedChunk : public InputChunk {
public:
  SyntheticMergedChunk(StringRef name, uint32_t alignment, uint32_t flags)
      : InputChunk(nullptr, InputChunk::MergedChunk, name, alignment, flags),
        builder(llvm::StringTableBuilder::RAW, llvm::Align(1ULL << alignment)) {
  }

  static bool classof(const InputChunk *c) {
    return c->kind() == InputChunk::MergedChunk;
  }

  void addMergeChunk(MergeInputChunk *ms) {
    comdat = ms->getComdat();
    ms->parent = this;
    chunks.push_back(ms);
  }

  void finalizeContents();

  llvm::StringTableBuilder builder;

protected:
  std::vector<MergeInputChunk *> chunks;
};

// Represents a single wasm function within and input file.  These are
// combined to create the final output CODE section.
class InputFunction : public InputChunk {
public:
  InputFunction(const WasmSignature &s, const WasmFunction *func, ObjFile *f)
      : InputChunk(f, InputChunk::Function, func->SymbolName), signature(s),
        function(func),
        exportName(func && func->ExportName ? (*func->ExportName).str()
                                            : std::optional<std::string>()) {
    inputSectionOffset = function->CodeSectionOffset;
    rawData =
        file->codeSection->Content.slice(inputSectionOffset, function->Size);
    debugName = function->DebugName;
    comdat = function->Comdat;
    assert(s.Kind != WasmSignature::Placeholder);
  }

  InputFunction(StringRef name, const WasmSignature &s)
      : InputChunk(nullptr, InputChunk::Function, name), signature(s) {
    assert(s.Kind == WasmSignature::Function);
  }

  static bool classof(const InputChunk *c) {
    return c->kind() == InputChunk::Function ||
           c->kind() == InputChunk::SyntheticFunction;
  }

  std::optional<StringRef> getExportName() const {
    return exportName ? std::optional<StringRef>(*exportName)
                      : std::optional<StringRef>();
  }
  void setExportName(std::string exportName) { this->exportName = exportName; }
  uint32_t getFunctionInputOffset() const { return getInputSectionOffset(); }
  uint32_t getFunctionCodeOffset() const {
    // For generated synthetic functions, such as unreachable stubs generated
    // for signature mismatches, 'function' reference does not exist. This
    // function is used to get function offsets for .debug_info section, and for
    // those generated stubs function offsets are not meaningful anyway. So just
    // return 0 in those cases.
    return function ? function->CodeOffset : 0;
  }
  uint32_t getFunctionIndex() const { return *functionIndex; }
  bool hasFunctionIndex() const { return functionIndex.has_value(); }
  void setFunctionIndex(uint32_t index);
  uint32_t getTableIndex() const { return *tableIndex; }
  bool hasTableIndex() const { return tableIndex.has_value(); }
  void setTableIndex(uint32_t index);
  void writeCompressed(uint8_t *buf) const;

  // The size of a given input function can depend on the values of the
  // LEB relocations within it.  This finalizeContents method is called after
  // all the symbol values have be calculated but before getSize() is ever
  // called.
  void calculateSize();

  const WasmSignature &signature;

  uint32_t getCompressedSize() const {
    assert(compressedSize);
    return compressedSize;
  }

  const WasmFunction *function = nullptr;

protected:
  std::optional<std::string> exportName;
  std::optional<uint32_t> functionIndex;
  std::optional<uint32_t> tableIndex;
  uint32_t compressedFuncSize = 0;
  uint32_t compressedSize = 0;
};

class SyntheticFunction : public InputFunction {
public:
  SyntheticFunction(const WasmSignature &s, StringRef name,
                    StringRef debugName = {})
      : InputFunction(name, s) {
    sectionKind = InputChunk::SyntheticFunction;
    this->debugName = debugName;
  }

  static bool classof(const InputChunk *c) {
    return c->kind() == InputChunk::SyntheticFunction;
  }

  void setBody(ArrayRef<uint8_t> body) { rawData = body; }
};

// Represents a single Wasm Section within an input file.
class InputSection : public InputChunk {
public:
  InputSection(const WasmSection &s, ObjFile *f)
      : InputChunk(f, InputChunk::Section, s.Name),
        tombstoneValue(getTombstoneForSection(s.Name)), section(s) {
    assert(section.Type == llvm::wasm::WASM_SEC_CUSTOM);
    comdat = section.Comdat;
    rawData = section.Content;
  }

  static bool classof(const InputChunk *c) {
    return c->kind() == InputChunk::Section;
  }

  const uint64_t tombstoneValue;

protected:
  static uint64_t getTombstoneForSection(StringRef name);
  const WasmSection &section;
};

} // namespace wasm

std::string toString(const wasm::InputChunk *);
StringRef relocTypeToString(uint8_t relocType);

} // namespace lld

#endif // LLD_WASM_INPUT_CHUNKS_H
