//===- InputChunks.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputChunks.h"
#include "Config.h"
#include "OutputSegment.h"
#include "WriterUtils.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/LLVM.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/xxhash.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::wasm;
using namespace llvm::support::endian;

namespace lld {
StringRef relocTypeToString(uint8_t relocType) {
  switch (relocType) {
#define WASM_RELOC(NAME, REL)                                                  \
  case REL:                                                                    \
    return #NAME;
#include "llvm/BinaryFormat/WasmRelocs.def"
#undef WASM_RELOC
  }
  llvm_unreachable("unknown reloc type");
}

bool relocIs64(uint8_t relocType) {
  switch (relocType) {
  case R_WASM_MEMORY_ADDR_LEB64:
  case R_WASM_MEMORY_ADDR_SLEB64:
  case R_WASM_MEMORY_ADDR_REL_SLEB64:
  case R_WASM_MEMORY_ADDR_I64:
  case R_WASM_TABLE_INDEX_SLEB64:
  case R_WASM_TABLE_INDEX_I64:
  case R_WASM_FUNCTION_OFFSET_I64:
  case R_WASM_TABLE_INDEX_REL_SLEB64:
  case R_WASM_MEMORY_ADDR_TLS_SLEB64:
    return true;
  default:
    return false;
  }
}

std::string toString(const wasm::InputChunk *c) {
  return (toString(c->file) + ":(" + c->name + ")").str();
}

namespace wasm {
StringRef InputChunk::getComdatName() const {
  uint32_t index = getComdat();
  if (index == UINT32_MAX)
    return StringRef();
  return file->getWasmObj()->linkingData().Comdats[index];
}

uint32_t InputChunk::getSize() const {
  if (const auto *ms = dyn_cast<SyntheticMergedChunk>(this))
    return ms->builder.getSize();

  if (const auto *f = dyn_cast<InputFunction>(this)) {
    if (config->compressRelocations && f->file) {
      return f->getCompressedSize();
    }
  }

  return data().size();
}

uint32_t InputChunk::getInputSize() const {
  if (const auto *f = dyn_cast<InputFunction>(this))
    return f->function->Size;
  return getSize();
}

// Copy this input chunk to an mmap'ed output file and apply relocations.
void InputChunk::writeTo(uint8_t *buf) const {
  if (const auto *f = dyn_cast<InputFunction>(this)) {
    if (file && config->compressRelocations)
      return f->writeCompressed(buf);
  } else if (const auto *ms = dyn_cast<SyntheticMergedChunk>(this)) {
    ms->builder.write(buf + outSecOff);
    // Apply relocations
    ms->relocate(buf + outSecOff);
    return;
  }

  // Copy contents
  memcpy(buf + outSecOff, data().data(), data().size());

  // Apply relocations
  relocate(buf + outSecOff);
}

void InputChunk::relocate(uint8_t *buf) const {
  if (relocations.empty())
    return;

  LLVM_DEBUG(dbgs() << "applying relocations: " << toString(this)
                    << " count=" << relocations.size() << "\n");
  int32_t inputSectionOffset = getInputSectionOffset();
  uint64_t tombstone = getTombstone();

  for (const WasmRelocation &rel : relocations) {
    uint8_t *loc = buf + rel.Offset - inputSectionOffset;
    LLVM_DEBUG(dbgs() << "apply reloc: type=" << relocTypeToString(rel.Type));
    if (rel.Type != R_WASM_TYPE_INDEX_LEB)
      LLVM_DEBUG(dbgs() << " sym=" << file->getSymbols()[rel.Index]->getName());
    LLVM_DEBUG(dbgs() << " addend=" << rel.Addend << " index=" << rel.Index
                      << " offset=" << rel.Offset << "\n");
    // TODO(sbc): Check that the value is within the range of the
    // relocation type below.  Most likely we must error out here
    // if its not with range.
    uint64_t value = file->calcNewValue(rel, tombstone, this);

    switch (rel.Type) {
    case R_WASM_TYPE_INDEX_LEB:
    case R_WASM_FUNCTION_INDEX_LEB:
    case R_WASM_GLOBAL_INDEX_LEB:
    case R_WASM_TAG_INDEX_LEB:
    case R_WASM_MEMORY_ADDR_LEB:
    case R_WASM_TABLE_NUMBER_LEB:
      encodeULEB128(static_cast<uint32_t>(value), loc, 5);
      break;
    case R_WASM_MEMORY_ADDR_LEB64:
      encodeULEB128(value, loc, 10);
      break;
    case R_WASM_TABLE_INDEX_SLEB:
    case R_WASM_TABLE_INDEX_REL_SLEB:
    case R_WASM_MEMORY_ADDR_SLEB:
    case R_WASM_MEMORY_ADDR_REL_SLEB:
    case R_WASM_MEMORY_ADDR_TLS_SLEB:
      encodeSLEB128(static_cast<int32_t>(value), loc, 5);
      break;
    case R_WASM_TABLE_INDEX_SLEB64:
    case R_WASM_TABLE_INDEX_REL_SLEB64:
    case R_WASM_MEMORY_ADDR_SLEB64:
    case R_WASM_MEMORY_ADDR_REL_SLEB64:
    case R_WASM_MEMORY_ADDR_TLS_SLEB64:
      encodeSLEB128(static_cast<int64_t>(value), loc, 10);
      break;
    case R_WASM_TABLE_INDEX_I32:
    case R_WASM_MEMORY_ADDR_I32:
    case R_WASM_FUNCTION_OFFSET_I32:
    case R_WASM_FUNCTION_INDEX_I32:
    case R_WASM_SECTION_OFFSET_I32:
    case R_WASM_GLOBAL_INDEX_I32:
    case R_WASM_MEMORY_ADDR_LOCREL_I32:
      write32le(loc, value);
      break;
    case R_WASM_TABLE_INDEX_I64:
    case R_WASM_MEMORY_ADDR_I64:
    case R_WASM_FUNCTION_OFFSET_I64:
      write64le(loc, value);
      break;
    default:
      llvm_unreachable("unknown relocation type");
    }
  }
}

// Copy relocation entries to a given output stream.
// This function is used only when a user passes "-r". For a regular link,
// we consume relocations instead of copying them to an output file.
void InputChunk::writeRelocations(raw_ostream &os) const {
  if (relocations.empty())
    return;

  int32_t off = outSecOff - getInputSectionOffset();
  LLVM_DEBUG(dbgs() << "writeRelocations: " << file->getName()
                    << " offset=" << Twine(off) << "\n");

  for (const WasmRelocation &rel : relocations) {
    writeUleb128(os, rel.Type, "reloc type");
    writeUleb128(os, rel.Offset + off, "reloc offset");
    writeUleb128(os, file->calcNewIndex(rel), "reloc index");

    if (relocTypeHasAddend(rel.Type))
      writeSleb128(os, file->calcNewAddend(rel), "reloc addend");
  }
}

uint64_t InputChunk::getTombstone() const {
  if (const auto *s = dyn_cast<InputSection>(this)) {
    return s->tombstoneValue;
  }

  return 0;
}

void InputFunction::setFunctionIndex(uint32_t index) {
  LLVM_DEBUG(dbgs() << "InputFunction::setFunctionIndex: " << name << " -> "
                    << index << "\n");
  assert(!hasFunctionIndex());
  functionIndex = index;
}

void InputFunction::setTableIndex(uint32_t index) {
  LLVM_DEBUG(dbgs() << "InputFunction::setTableIndex: " << name << " -> "
                    << index << "\n");
  assert(!hasTableIndex());
  tableIndex = index;
}

// Write a relocation value without padding and return the number of bytes
// witten.
static unsigned writeCompressedReloc(uint8_t *buf, const WasmRelocation &rel,
                                     uint64_t value) {
  switch (rel.Type) {
  case R_WASM_TYPE_INDEX_LEB:
  case R_WASM_FUNCTION_INDEX_LEB:
  case R_WASM_GLOBAL_INDEX_LEB:
  case R_WASM_TAG_INDEX_LEB:
  case R_WASM_MEMORY_ADDR_LEB:
  case R_WASM_MEMORY_ADDR_LEB64:
  case R_WASM_TABLE_NUMBER_LEB:
    return encodeULEB128(value, buf);
  case R_WASM_TABLE_INDEX_SLEB:
  case R_WASM_TABLE_INDEX_SLEB64:
  case R_WASM_MEMORY_ADDR_SLEB:
  case R_WASM_MEMORY_ADDR_SLEB64:
    return encodeSLEB128(static_cast<int64_t>(value), buf);
  default:
    llvm_unreachable("unexpected relocation type");
  }
}

static unsigned getRelocWidthPadded(const WasmRelocation &rel) {
  switch (rel.Type) {
  case R_WASM_TYPE_INDEX_LEB:
  case R_WASM_FUNCTION_INDEX_LEB:
  case R_WASM_GLOBAL_INDEX_LEB:
  case R_WASM_TAG_INDEX_LEB:
  case R_WASM_MEMORY_ADDR_LEB:
  case R_WASM_TABLE_NUMBER_LEB:
  case R_WASM_TABLE_INDEX_SLEB:
  case R_WASM_MEMORY_ADDR_SLEB:
    return 5;
  case R_WASM_TABLE_INDEX_SLEB64:
  case R_WASM_MEMORY_ADDR_LEB64:
  case R_WASM_MEMORY_ADDR_SLEB64:
    return 10;
  default:
    llvm_unreachable("unexpected relocation type");
  }
}

static unsigned getRelocWidth(const WasmRelocation &rel, uint64_t value) {
  uint8_t buf[10];
  return writeCompressedReloc(buf, rel, value);
}

// Relocations of type LEB and SLEB in the code section are padded to 5 bytes
// so that a fast linker can blindly overwrite them without needing to worry
// about the number of bytes needed to encode the values.
// However, for optimal output the code section can be compressed to remove
// the padding then outputting non-relocatable files.
// In this case we need to perform a size calculation based on the value at each
// relocation.  At best we end up saving 4 bytes for each relocation entry.
//
// This function only computes the final output size.  It must be called
// before getSize() is used to calculate of layout of the code section.
void InputFunction::calculateSize() {
  if (!file || !config->compressRelocations)
    return;

  LLVM_DEBUG(dbgs() << "calculateSize: " << name << "\n");

  const uint8_t *secStart = file->codeSection->Content.data();
  const uint8_t *funcStart = secStart + getInputSectionOffset();
  uint32_t functionSizeLength;
  decodeULEB128(funcStart, &functionSizeLength);

  uint32_t start = getInputSectionOffset();
  uint32_t end = start + function->Size;

  uint64_t tombstone = getTombstone();

  uint32_t lastRelocEnd = start + functionSizeLength;
  for (const WasmRelocation &rel : relocations) {
    LLVM_DEBUG(dbgs() << "  region: " << (rel.Offset - lastRelocEnd) << "\n");
    compressedFuncSize += rel.Offset - lastRelocEnd;
    compressedFuncSize +=
        getRelocWidth(rel, file->calcNewValue(rel, tombstone, this));
    lastRelocEnd = rel.Offset + getRelocWidthPadded(rel);
  }
  LLVM_DEBUG(dbgs() << "  final region: " << (end - lastRelocEnd) << "\n");
  compressedFuncSize += end - lastRelocEnd;

  // Now we know how long the resulting function is we can add the encoding
  // of its length
  uint8_t buf[5];
  compressedSize = compressedFuncSize + encodeULEB128(compressedFuncSize, buf);

  LLVM_DEBUG(dbgs() << "  calculateSize orig: " << function->Size << "\n");
  LLVM_DEBUG(dbgs() << "  calculateSize  new: " << compressedSize << "\n");
}

// Override the default writeTo method so that we can (optionally) write the
// compressed version of the function.
void InputFunction::writeCompressed(uint8_t *buf) const {
  buf += outSecOff;
  uint8_t *orig = buf;
  (void)orig;

  const uint8_t *secStart = file->codeSection->Content.data();
  const uint8_t *funcStart = secStart + getInputSectionOffset();
  const uint8_t *end = funcStart + function->Size;
  uint64_t tombstone = getTombstone();
  uint32_t count;
  decodeULEB128(funcStart, &count);
  funcStart += count;

  LLVM_DEBUG(dbgs() << "write func: " << name << "\n");
  buf += encodeULEB128(compressedFuncSize, buf);
  const uint8_t *lastRelocEnd = funcStart;
  for (const WasmRelocation &rel : relocations) {
    unsigned chunkSize = (secStart + rel.Offset) - lastRelocEnd;
    LLVM_DEBUG(dbgs() << "  write chunk: " << chunkSize << "\n");
    memcpy(buf, lastRelocEnd, chunkSize);
    buf += chunkSize;
    buf += writeCompressedReloc(buf, rel,
                                file->calcNewValue(rel, tombstone, this));
    lastRelocEnd = secStart + rel.Offset + getRelocWidthPadded(rel);
  }

  unsigned chunkSize = end - lastRelocEnd;
  LLVM_DEBUG(dbgs() << "  write final chunk: " << chunkSize << "\n");
  memcpy(buf, lastRelocEnd, chunkSize);
  LLVM_DEBUG(dbgs() << "  total: " << (buf + chunkSize - orig) << "\n");
}

uint64_t InputChunk::getChunkOffset(uint64_t offset) const {
  if (const auto *ms = dyn_cast<MergeInputChunk>(this)) {
    LLVM_DEBUG(dbgs() << "getChunkOffset(merged): " << name << "\n");
    LLVM_DEBUG(dbgs() << "offset: " << offset << "\n");
    LLVM_DEBUG(dbgs() << "parentOffset: " << ms->getParentOffset(offset)
                      << "\n");
    assert(ms->parent);
    return ms->parent->getChunkOffset(ms->getParentOffset(offset));
  }
  return outputSegmentOffset + offset;
}

uint64_t InputChunk::getOffset(uint64_t offset) const {
  return outSecOff + getChunkOffset(offset);
}

uint64_t InputChunk::getVA(uint64_t offset) const {
  return (outputSeg ? outputSeg->startVA : 0) + getChunkOffset(offset);
}

// Generate code to apply relocations to the data section at runtime.
// This is only called when generating shared libraries (PIC) where address are
// not known at static link time.
void InputChunk::generateRelocationCode(raw_ostream &os) const {
  LLVM_DEBUG(dbgs() << "generating runtime relocations: " << name
                    << " count=" << relocations.size() << "\n");

  bool is64 = config->is64.value_or(false);
  unsigned opcode_ptr_const = is64 ? WASM_OPCODE_I64_CONST
                                   : WASM_OPCODE_I32_CONST;
  unsigned opcode_ptr_add = is64 ? WASM_OPCODE_I64_ADD
                                 : WASM_OPCODE_I32_ADD;

  uint64_t tombstone = getTombstone();
  // TODO(sbc): Encode the relocations in the data section and write a loop
  // here to apply them.
  for (const WasmRelocation &rel : relocations) {
    uint64_t offset = getVA(rel.Offset) - getInputSectionOffset();

    Symbol *sym = file->getSymbol(rel);
    if (!ctx.isPic && sym->isDefined())
      continue;

    LLVM_DEBUG(dbgs() << "gen reloc: type=" << relocTypeToString(rel.Type)
                      << " addend=" << rel.Addend << " index=" << rel.Index
                      << " output offset=" << offset << "\n");

    // Calculate the address at which to apply the relocation
    writeU8(os, opcode_ptr_const, "CONST");
    writeSleb128(os, offset, "offset");

    // In PIC mode we need to add the __memory_base
    if (ctx.isPic) {
      writeU8(os, WASM_OPCODE_GLOBAL_GET, "GLOBAL_GET");
      if (isTLS())
        writeUleb128(os, WasmSym::tlsBase->getGlobalIndex(), "tls_base");
      else
        writeUleb128(os, WasmSym::memoryBase->getGlobalIndex(), "memory_base");
      writeU8(os, opcode_ptr_add, "ADD");
    }

    // Now figure out what we want to store at this location
    bool is64 = relocIs64(rel.Type);
    unsigned opcode_reloc_const =
        is64 ? WASM_OPCODE_I64_CONST : WASM_OPCODE_I32_CONST;
    unsigned opcode_reloc_add =
        is64 ? WASM_OPCODE_I64_ADD : WASM_OPCODE_I32_ADD;
    unsigned opcode_reloc_store =
        is64 ? WASM_OPCODE_I64_STORE : WASM_OPCODE_I32_STORE;

    if (sym->hasGOTIndex()) {
      writeU8(os, WASM_OPCODE_GLOBAL_GET, "GLOBAL_GET");
      writeUleb128(os, sym->getGOTIndex(), "global index");
      if (rel.Addend) {
        writeU8(os, opcode_reloc_const, "CONST");
        writeSleb128(os, rel.Addend, "addend");
        writeU8(os, opcode_reloc_add, "ADD");
      }
    } else {
      assert(ctx.isPic);
      const GlobalSymbol* baseSymbol = WasmSym::memoryBase;
      if (rel.Type == R_WASM_TABLE_INDEX_I32 ||
          rel.Type == R_WASM_TABLE_INDEX_I64)
        baseSymbol = WasmSym::tableBase;
      else if (sym->isTLS())
        baseSymbol = WasmSym::tlsBase;
      writeU8(os, WASM_OPCODE_GLOBAL_GET, "GLOBAL_GET");
      writeUleb128(os, baseSymbol->getGlobalIndex(), "base");
      writeU8(os, opcode_reloc_const, "CONST");
      writeSleb128(os, file->calcNewValue(rel, tombstone, this), "offset");
      writeU8(os, opcode_reloc_add, "ADD");
    }

    // Store that value at the virtual address
    writeU8(os, opcode_reloc_store, "I32_STORE");
    writeUleb128(os, 2, "align");
    writeUleb128(os, 0, "offset");
  }
}

// Split WASM_SEG_FLAG_STRINGS section. Such a section is a sequence of
// null-terminated strings.
void MergeInputChunk::splitStrings(ArrayRef<uint8_t> data) {
  LLVM_DEBUG(llvm::dbgs() << "splitStrings\n");
  size_t off = 0;
  StringRef s = toStringRef(data);

  while (!s.empty()) {
    size_t end = s.find(0);
    if (end == StringRef::npos)
      fatal(toString(this) + ": string is not null terminated");
    size_t size = end + 1;

    pieces.emplace_back(off, xxh3_64bits(s.substr(0, size)), true);
    s = s.substr(size);
    off += size;
  }
}

// This function is called after we obtain a complete list of input sections
// that need to be linked. This is responsible to split section contents
// into small chunks for further processing.
//
// Note that this function is called from parallelForEach. This must be
// thread-safe (i.e. no memory allocation from the pools).
void MergeInputChunk::splitIntoPieces() {
  assert(pieces.empty());
  // As of now we only support WASM_SEG_FLAG_STRINGS but in the future we
  // could add other types of splitting (see ELF's splitIntoPieces).
  assert(flags & WASM_SEG_FLAG_STRINGS);
  splitStrings(data());
}

SectionPiece *MergeInputChunk::getSectionPiece(uint64_t offset) {
  if (this->data().size() <= offset)
    fatal(toString(this) + ": offset is outside the section");

  // If Offset is not at beginning of a section piece, it is not in the map.
  // In that case we need to  do a binary search of the original section piece
  // vector.
  auto it = partition_point(
      pieces, [=](SectionPiece p) { return p.inputOff <= offset; });
  return &it[-1];
}

// Returns the offset in an output section for a given input offset.
// Because contents of a mergeable section is not contiguous in output,
// it is not just an addition to a base output offset.
uint64_t MergeInputChunk::getParentOffset(uint64_t offset) const {
  // If Offset is not at beginning of a section piece, it is not in the map.
  // In that case we need to search from the original section piece vector.
  const SectionPiece *piece = getSectionPiece(offset);
  uint64_t addend = offset - piece->inputOff;
  return piece->outputOff + addend;
}

void SyntheticMergedChunk::finalizeContents() {
  // Add all string pieces to the string table builder to create section
  // contents.
  for (MergeInputChunk *sec : chunks)
    for (size_t i = 0, e = sec->pieces.size(); i != e; ++i)
      if (sec->pieces[i].live)
        builder.add(sec->getData(i));

  // Fix the string table content. After this, the contents will never change.
  builder.finalize();

  // finalize() fixed tail-optimized strings, so we can now get
  // offsets of strings. Get an offset for each string and save it
  // to a corresponding SectionPiece for easy access.
  for (MergeInputChunk *sec : chunks)
    for (size_t i = 0, e = sec->pieces.size(); i != e; ++i)
      if (sec->pieces[i].live)
        sec->pieces[i].outputOff = builder.getOffset(sec->getData(i));
}

uint64_t InputSection::getTombstoneForSection(StringRef name) {
  // When a function is not live we need to update relocations referring to it.
  // If they occur in DWARF debug symbols, we want to change the pc of the
  // function to -1 to avoid overlapping with a valid range. However for the
  // debug_ranges and debug_loc sections that would conflict with the existing
  // meaning of -1 so we use -2.
  if (name == ".debug_ranges" || name == ".debug_loc")
    return UINT64_C(-2);
  if (name.starts_with(".debug_"))
    return UINT64_C(-1);
  // If the function occurs in an function attribute section change it to -1 since
  // 0 is a valid function index.
  if (name.starts_with("llvm.func_attr."))
    return UINT64_C(-1);
  // Returning 0 means there is no tombstone value for this section, and relocation
  // will just use the addend.
  return 0;
}

} // namespace wasm
} // namespace lld
