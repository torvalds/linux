//===- InputFiles.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputElement.h"
#include "OutputSegment.h"
#include "SymbolTable.h"
#include "lld/Common/Args.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Reproduce.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/Wasm.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TarWriter.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::wasm;
using namespace llvm::sys;

namespace lld {

// Returns a string in the format of "foo.o" or "foo.a(bar.o)".
std::string toString(const wasm::InputFile *file) {
  if (!file)
    return "<internal>";

  if (file->archiveName.empty())
    return std::string(file->getName());

  return (file->archiveName + "(" + file->getName() + ")").str();
}

namespace wasm {

void InputFile::checkArch(Triple::ArchType arch) const {
  bool is64 = arch == Triple::wasm64;
  if (is64 && !config->is64) {
    fatal(toString(this) +
          ": must specify -mwasm64 to process wasm64 object files");
  } else if (config->is64.value_or(false) != is64) {
    fatal(toString(this) +
          ": wasm32 object file can't be linked in wasm64 mode");
  }
}

std::unique_ptr<llvm::TarWriter> tar;

std::optional<MemoryBufferRef> readFile(StringRef path) {
  log("Loading: " + path);

  auto mbOrErr = MemoryBuffer::getFile(path);
  if (auto ec = mbOrErr.getError()) {
    error("cannot open " + path + ": " + ec.message());
    return std::nullopt;
  }
  std::unique_ptr<MemoryBuffer> &mb = *mbOrErr;
  MemoryBufferRef mbref = mb->getMemBufferRef();
  make<std::unique_ptr<MemoryBuffer>>(std::move(mb)); // take MB ownership

  if (tar)
    tar->append(relativeToRoot(path), mbref.getBuffer());
  return mbref;
}

InputFile *createObjectFile(MemoryBufferRef mb, StringRef archiveName,
                            uint64_t offsetInArchive, bool lazy) {
  file_magic magic = identify_magic(mb.getBuffer());
  if (magic == file_magic::wasm_object) {
    std::unique_ptr<Binary> bin =
        CHECK(createBinary(mb), mb.getBufferIdentifier());
    auto *obj = cast<WasmObjectFile>(bin.get());
    if (obj->hasUnmodeledTypes())
      fatal(toString(mb.getBufferIdentifier()) +
            "file has unmodeled reference or GC types");
    if (obj->isSharedObject())
      return make<SharedFile>(mb);
    return make<ObjFile>(mb, archiveName, lazy);
  }

  assert(magic == file_magic::bitcode);
  return make<BitcodeFile>(mb, archiveName, offsetInArchive, lazy);
}

// Relocations contain either symbol or type indices.  This function takes a
// relocation and returns relocated index (i.e. translates from the input
// symbol/type space to the output symbol/type space).
uint32_t ObjFile::calcNewIndex(const WasmRelocation &reloc) const {
  if (reloc.Type == R_WASM_TYPE_INDEX_LEB) {
    assert(typeIsUsed[reloc.Index]);
    return typeMap[reloc.Index];
  }
  const Symbol *sym = symbols[reloc.Index];
  if (auto *ss = dyn_cast<SectionSymbol>(sym))
    sym = ss->getOutputSectionSymbol();
  return sym->getOutputSymbolIndex();
}

// Relocations can contain addend for combined sections. This function takes a
// relocation and returns updated addend by offset in the output section.
int64_t ObjFile::calcNewAddend(const WasmRelocation &reloc) const {
  switch (reloc.Type) {
  case R_WASM_MEMORY_ADDR_LEB:
  case R_WASM_MEMORY_ADDR_LEB64:
  case R_WASM_MEMORY_ADDR_SLEB64:
  case R_WASM_MEMORY_ADDR_SLEB:
  case R_WASM_MEMORY_ADDR_REL_SLEB:
  case R_WASM_MEMORY_ADDR_REL_SLEB64:
  case R_WASM_MEMORY_ADDR_I32:
  case R_WASM_MEMORY_ADDR_I64:
  case R_WASM_MEMORY_ADDR_TLS_SLEB:
  case R_WASM_MEMORY_ADDR_TLS_SLEB64:
  case R_WASM_FUNCTION_OFFSET_I32:
  case R_WASM_FUNCTION_OFFSET_I64:
  case R_WASM_MEMORY_ADDR_LOCREL_I32:
    return reloc.Addend;
  case R_WASM_SECTION_OFFSET_I32:
    return getSectionSymbol(reloc.Index)->section->getOffset(reloc.Addend);
  default:
    llvm_unreachable("unexpected relocation type");
  }
}

// Translate from the relocation's index into the final linked output value.
uint64_t ObjFile::calcNewValue(const WasmRelocation &reloc, uint64_t tombstone,
                               const InputChunk *chunk) const {
  const Symbol* sym = nullptr;
  if (reloc.Type != R_WASM_TYPE_INDEX_LEB) {
    sym = symbols[reloc.Index];

    // We can end up with relocations against non-live symbols.  For example
    // in debug sections. We return a tombstone value in debug symbol sections
    // so this will not produce a valid range conflicting with ranges of actual
    // code. In other sections we return reloc.Addend.

    if (!isa<SectionSymbol>(sym) && !sym->isLive())
      return tombstone ? tombstone : reloc.Addend;
  }

  switch (reloc.Type) {
  case R_WASM_TABLE_INDEX_I32:
  case R_WASM_TABLE_INDEX_I64:
  case R_WASM_TABLE_INDEX_SLEB:
  case R_WASM_TABLE_INDEX_SLEB64:
  case R_WASM_TABLE_INDEX_REL_SLEB:
  case R_WASM_TABLE_INDEX_REL_SLEB64: {
    if (!getFunctionSymbol(reloc.Index)->hasTableIndex())
      return 0;
    uint32_t index = getFunctionSymbol(reloc.Index)->getTableIndex();
    if (reloc.Type == R_WASM_TABLE_INDEX_REL_SLEB ||
        reloc.Type == R_WASM_TABLE_INDEX_REL_SLEB64)
      index -= config->tableBase;
    return index;
  }
  case R_WASM_MEMORY_ADDR_LEB:
  case R_WASM_MEMORY_ADDR_LEB64:
  case R_WASM_MEMORY_ADDR_SLEB:
  case R_WASM_MEMORY_ADDR_SLEB64:
  case R_WASM_MEMORY_ADDR_REL_SLEB:
  case R_WASM_MEMORY_ADDR_REL_SLEB64:
  case R_WASM_MEMORY_ADDR_I32:
  case R_WASM_MEMORY_ADDR_I64:
  case R_WASM_MEMORY_ADDR_TLS_SLEB:
  case R_WASM_MEMORY_ADDR_TLS_SLEB64:
  case R_WASM_MEMORY_ADDR_LOCREL_I32: {
    if (isa<UndefinedData>(sym) || sym->isShared() || sym->isUndefWeak())
      return 0;
    auto D = cast<DefinedData>(sym);
    uint64_t value = D->getVA() + reloc.Addend;
    if (reloc.Type == R_WASM_MEMORY_ADDR_LOCREL_I32) {
      const auto *segment = cast<InputSegment>(chunk);
      uint64_t p = segment->outputSeg->startVA + segment->outputSegmentOffset +
                   reloc.Offset - segment->getInputSectionOffset();
      value -= p;
    }
    return value;
  }
  case R_WASM_TYPE_INDEX_LEB:
    return typeMap[reloc.Index];
  case R_WASM_FUNCTION_INDEX_LEB:
  case R_WASM_FUNCTION_INDEX_I32:
    return getFunctionSymbol(reloc.Index)->getFunctionIndex();
  case R_WASM_GLOBAL_INDEX_LEB:
  case R_WASM_GLOBAL_INDEX_I32:
    if (auto gs = dyn_cast<GlobalSymbol>(sym))
      return gs->getGlobalIndex();
    return sym->getGOTIndex();
  case R_WASM_TAG_INDEX_LEB:
    return getTagSymbol(reloc.Index)->getTagIndex();
  case R_WASM_FUNCTION_OFFSET_I32:
  case R_WASM_FUNCTION_OFFSET_I64: {
    if (isa<UndefinedFunction>(sym)) {
      return tombstone ? tombstone : reloc.Addend;
    }
    auto *f = cast<DefinedFunction>(sym);
    return f->function->getOffset(f->function->getFunctionCodeOffset() +
                                  reloc.Addend);
  }
  case R_WASM_SECTION_OFFSET_I32:
    return getSectionSymbol(reloc.Index)->section->getOffset(reloc.Addend);
  case R_WASM_TABLE_NUMBER_LEB:
    return getTableSymbol(reloc.Index)->getTableNumber();
  default:
    llvm_unreachable("unknown relocation type");
  }
}

template <class T>
static void setRelocs(const std::vector<T *> &chunks,
                      const WasmSection *section) {
  if (!section)
    return;

  ArrayRef<WasmRelocation> relocs = section->Relocations;
  assert(llvm::is_sorted(
      relocs, [](const WasmRelocation &r1, const WasmRelocation &r2) {
        return r1.Offset < r2.Offset;
      }));
  assert(llvm::is_sorted(chunks, [](InputChunk *c1, InputChunk *c2) {
    return c1->getInputSectionOffset() < c2->getInputSectionOffset();
  }));

  auto relocsNext = relocs.begin();
  auto relocsEnd = relocs.end();
  auto relocLess = [](const WasmRelocation &r, uint32_t val) {
    return r.Offset < val;
  };
  for (InputChunk *c : chunks) {
    auto relocsStart = std::lower_bound(relocsNext, relocsEnd,
                                        c->getInputSectionOffset(), relocLess);
    relocsNext = std::lower_bound(
        relocsStart, relocsEnd, c->getInputSectionOffset() + c->getInputSize(),
        relocLess);
    c->setRelocations(ArrayRef<WasmRelocation>(relocsStart, relocsNext));
  }
}

// An object file can have two approaches to tables.  With the reference-types
// feature enabled, input files that define or use tables declare the tables
// using symbols, and record each use with a relocation.  This way when the
// linker combines inputs, it can collate the tables used by the inputs,
// assigning them distinct table numbers, and renumber all the uses as
// appropriate.  At the same time, the linker has special logic to build the
// indirect function table if it is needed.
//
// However, MVP object files (those that target WebAssembly 1.0, the "minimum
// viable product" version of WebAssembly) neither write table symbols nor
// record relocations.  These files can have at most one table, the indirect
// function table used by call_indirect and which is the address space for
// function pointers.  If this table is present, it is always an import.  If we
// have a file with a table import but no table symbols, it is an MVP object
// file.  synthesizeMVPIndirectFunctionTableSymbolIfNeeded serves as a shim when
// loading these input files, defining the missing symbol to allow the indirect
// function table to be built.
//
// As indirect function table table usage in MVP objects cannot be relocated,
// the linker must ensure that this table gets assigned index zero.
void ObjFile::addLegacyIndirectFunctionTableIfNeeded(
    uint32_t tableSymbolCount) {
  uint32_t tableCount = wasmObj->getNumImportedTables() + tables.size();

  // If there are symbols for all tables, then all is good.
  if (tableCount == tableSymbolCount)
    return;

  // It's possible for an input to define tables and also use the indirect
  // function table, but forget to compile with -mattr=+reference-types.
  // For these newer files, we require symbols for all tables, and
  // relocations for all of their uses.
  if (tableSymbolCount != 0) {
    error(toString(this) +
          ": expected one symbol table entry for each of the " +
          Twine(tableCount) + " table(s) present, but got " +
          Twine(tableSymbolCount) + " symbol(s) instead.");
    return;
  }

  // An MVP object file can have up to one table import, for the indirect
  // function table, but will have no table definitions.
  if (tables.size()) {
    error(toString(this) +
          ": unexpected table definition(s) without corresponding "
          "symbol-table entries.");
    return;
  }

  // An MVP object file can have only one table import.
  if (tableCount != 1) {
    error(toString(this) +
          ": multiple table imports, but no corresponding symbol-table "
          "entries.");
    return;
  }

  const WasmImport *tableImport = nullptr;
  for (const auto &import : wasmObj->imports()) {
    if (import.Kind == WASM_EXTERNAL_TABLE) {
      assert(!tableImport);
      tableImport = &import;
    }
  }
  assert(tableImport);

  // We can only synthesize a symtab entry for the indirect function table; if
  // it has an unexpected name or type, assume that it's not actually the
  // indirect function table.
  if (tableImport->Field != functionTableName ||
      tableImport->Table.ElemType != ValType::FUNCREF) {
    error(toString(this) + ": table import " + Twine(tableImport->Field) +
          " is missing a symbol table entry.");
    return;
  }

  WasmSymbolInfo info;
  info.Name = tableImport->Field;
  info.Kind = WASM_SYMBOL_TYPE_TABLE;
  info.ImportModule = tableImport->Module;
  info.ImportName = tableImport->Field;
  info.Flags = WASM_SYMBOL_UNDEFINED | WASM_SYMBOL_NO_STRIP;
  info.ElementIndex = 0;
  LLVM_DEBUG(dbgs() << "Synthesizing symbol for table import: " << info.Name
                    << "\n");
  const WasmGlobalType *globalType = nullptr;
  const WasmSignature *signature = nullptr;
  auto *wasmSym =
      make<WasmSymbol>(info, globalType, &tableImport->Table, signature);
  Symbol *sym = createUndefined(*wasmSym, false);
  // We're only sure it's a TableSymbol if the createUndefined succeeded.
  if (errorCount())
    return;
  symbols.push_back(sym);
  // Because there are no TABLE_NUMBER relocs, we can't compute accurate
  // liveness info; instead, just mark the symbol as always live.
  sym->markLive();

  // We assume that this compilation unit has unrelocatable references to
  // this table.
  ctx.legacyFunctionTable = true;
}

static bool shouldMerge(const WasmSection &sec) {
  if (config->optimize == 0)
    return false;
  // Sadly we don't have section attributes yet for custom sections, so we
  // currently go by the name alone.
  // TODO(sbc): Add ability for wasm sections to carry flags so we don't
  // need to use names here.
  // For now, keep in sync with uses of wasm::WASM_SEG_FLAG_STRINGS in
  // MCObjectFileInfo::initWasmMCObjectFileInfo which creates these custom
  // sections.
  return sec.Name == ".debug_str" || sec.Name == ".debug_str.dwo" ||
         sec.Name == ".debug_line_str";
}

static bool shouldMerge(const WasmSegment &seg) {
  // As of now we only support merging strings, and only with single byte
  // alignment (2^0).
  if (!(seg.Data.LinkingFlags & WASM_SEG_FLAG_STRINGS) ||
      (seg.Data.Alignment != 0))
    return false;

  // On a regular link we don't merge sections if -O0 (default is -O1). This
  // sometimes makes the linker significantly faster, although the output will
  // be bigger.
  if (config->optimize == 0)
    return false;

  // A mergeable section with size 0 is useless because they don't have
  // any data to merge. A mergeable string section with size 0 can be
  // argued as invalid because it doesn't end with a null character.
  // We'll avoid a mess by handling them as if they were non-mergeable.
  if (seg.Data.Content.size() == 0)
    return false;

  return true;
}

void ObjFile::parseLazy() {
  LLVM_DEBUG(dbgs() << "ObjFile::parseLazy: " << toString(this) << " "
                    << wasmObj.get() << "\n");
  for (const SymbolRef &sym : wasmObj->symbols()) {
    const WasmSymbol &wasmSym = wasmObj->getWasmSymbol(sym.getRawDataRefImpl());
    if (!wasmSym.isDefined())
      continue;
    symtab->addLazy(wasmSym.Info.Name, this);
    // addLazy() may trigger this->extract() if an existing symbol is an
    // undefined symbol. If that happens, this function has served its purpose,
    // and we can exit from the loop early.
    if (!lazy)
      break;
  }
}

ObjFile::ObjFile(MemoryBufferRef m, StringRef archiveName, bool lazy)
    : WasmFileBase(ObjectKind, m) {
  this->lazy = lazy;
  this->archiveName = std::string(archiveName);

  // Currently we only do this check for regular object file, and not for shared
  // object files.  This is because architecture detection for shared objects is
  // currently based on a heuristic, which is fallable:
  // https://github.com/llvm/llvm-project/issues/98778
  checkArch(wasmObj->getArch());

  // If this isn't part of an archive, it's eagerly linked, so mark it live.
  if (archiveName.empty())
    markLive();
}

void SharedFile::parse() {
  assert(wasmObj->isSharedObject());

  for (const SymbolRef &sym : wasmObj->symbols()) {
    const WasmSymbol &wasmSym = wasmObj->getWasmSymbol(sym.getRawDataRefImpl());
    if (wasmSym.isDefined()) {
      StringRef name = wasmSym.Info.Name;
      // Certain shared library exports are known to be DSO-local so we
      // don't want to add them to the symbol table.
      // TODO(sbc): Instead of hardcoding these here perhaps we could add
      // this as extra metadata in the `dylink` section.
      if (name == "__wasm_apply_data_relocs" || name == "__wasm_call_ctors" ||
          name.starts_with("__start_") || name.starts_with("__stop_"))
        continue;
      uint32_t flags = wasmSym.Info.Flags;
      Symbol *s;
      LLVM_DEBUG(dbgs() << "shared symbol: " << name << "\n");
      switch (wasmSym.Info.Kind) {
      case WASM_SYMBOL_TYPE_FUNCTION:
        s = symtab->addSharedFunction(name, flags, this, wasmSym.Signature);
        break;
      case WASM_SYMBOL_TYPE_DATA:
        s = symtab->addSharedData(name, flags, this);
        break;
      default:
        continue;
      }
      symbols.push_back(s);
    }
  }
}

WasmFileBase::WasmFileBase(Kind k, MemoryBufferRef m) : InputFile(k, m) {
  // Parse a memory buffer as a wasm file.
  LLVM_DEBUG(dbgs() << "Reading object: " << toString(this) << "\n");
  std::unique_ptr<Binary> bin = CHECK(createBinary(mb), toString(this));

  auto *obj = dyn_cast<WasmObjectFile>(bin.get());
  if (!obj)
    fatal(toString(this) + ": not a wasm file");

  bin.release();
  wasmObj.reset(obj);
}

void ObjFile::parse(bool ignoreComdats) {
  // Parse a memory buffer as a wasm file.
  LLVM_DEBUG(dbgs() << "ObjFile::parse: " << toString(this) << "\n");

  if (!wasmObj->isRelocatableObject())
    fatal(toString(this) + ": not a relocatable wasm file");

  // Build up a map of function indices to table indices for use when
  // verifying the existing table index relocations
  uint32_t totalFunctions =
      wasmObj->getNumImportedFunctions() + wasmObj->functions().size();
  tableEntriesRel.resize(totalFunctions);
  tableEntries.resize(totalFunctions);
  for (const WasmElemSegment &seg : wasmObj->elements()) {
    int64_t offset;
    if (seg.Offset.Extended)
      fatal(toString(this) + ": extended init exprs not supported");
    else if (seg.Offset.Inst.Opcode == WASM_OPCODE_I32_CONST)
      offset = seg.Offset.Inst.Value.Int32;
    else if (seg.Offset.Inst.Opcode == WASM_OPCODE_I64_CONST)
      offset = seg.Offset.Inst.Value.Int64;
    else
      fatal(toString(this) + ": invalid table elements");
    for (size_t index = 0; index < seg.Functions.size(); index++) {
      auto functionIndex = seg.Functions[index];
      tableEntriesRel[functionIndex] = index;
      tableEntries[functionIndex] = offset + index;
    }
  }

  ArrayRef<StringRef> comdats = wasmObj->linkingData().Comdats;
  for (StringRef comdat : comdats) {
    bool isNew = ignoreComdats || symtab->addComdat(comdat);
    keptComdats.push_back(isNew);
  }

  uint32_t sectionIndex = 0;

  // Bool for each symbol, true if called directly.  This allows us to implement
  // a weaker form of signature checking where undefined functions that are not
  // called directly (i.e. only address taken) don't have to match the defined
  // function's signature.  We cannot do this for directly called functions
  // because those signatures are checked at validation times.
  // See https://github.com/llvm/llvm-project/issues/39758
  std::vector<bool> isCalledDirectly(wasmObj->getNumberOfSymbols(), false);
  for (const SectionRef &sec : wasmObj->sections()) {
    const WasmSection &section = wasmObj->getWasmSection(sec);
    // Wasm objects can have at most one code and one data section.
    if (section.Type == WASM_SEC_CODE) {
      assert(!codeSection);
      codeSection = &section;
    } else if (section.Type == WASM_SEC_DATA) {
      assert(!dataSection);
      dataSection = &section;
    } else if (section.Type == WASM_SEC_CUSTOM) {
      InputChunk *customSec;
      if (shouldMerge(section))
        customSec = make<MergeInputChunk>(section, this);
      else
        customSec = make<InputSection>(section, this);
      customSec->discarded = isExcludedByComdat(customSec);
      customSections.emplace_back(customSec);
      customSections.back()->setRelocations(section.Relocations);
      customSectionsByIndex[sectionIndex] = customSections.back();
    }
    sectionIndex++;
    // Scans relocations to determine if a function symbol is called directly.
    for (const WasmRelocation &reloc : section.Relocations)
      if (reloc.Type == R_WASM_FUNCTION_INDEX_LEB)
        isCalledDirectly[reloc.Index] = true;
  }

  typeMap.resize(getWasmObj()->types().size());
  typeIsUsed.resize(getWasmObj()->types().size(), false);


  // Populate `Segments`.
  for (const WasmSegment &s : wasmObj->dataSegments()) {
    InputChunk *seg;
    if (shouldMerge(s))
      seg = make<MergeInputChunk>(s, this);
    else
      seg = make<InputSegment>(s, this);
    seg->discarded = isExcludedByComdat(seg);
    // Older object files did not include WASM_SEG_FLAG_TLS and instead
    // relied on the naming convention.  To maintain compat with such objects
    // we still imply the TLS flag based on the name of the segment.
    if (!seg->isTLS() &&
        (seg->name.starts_with(".tdata") || seg->name.starts_with(".tbss")))
      seg->flags |= WASM_SEG_FLAG_TLS;
    segments.emplace_back(seg);
  }
  setRelocs(segments, dataSection);

  // Populate `Functions`.
  ArrayRef<WasmFunction> funcs = wasmObj->functions();
  ArrayRef<WasmSignature> types = wasmObj->types();
  functions.reserve(funcs.size());

  for (auto &f : funcs) {
    auto *func = make<InputFunction>(types[f.SigIndex], &f, this);
    func->discarded = isExcludedByComdat(func);
    functions.emplace_back(func);
  }
  setRelocs(functions, codeSection);

  // Populate `Tables`.
  for (const WasmTable &t : wasmObj->tables())
    tables.emplace_back(make<InputTable>(t, this));

  // Populate `Globals`.
  for (const WasmGlobal &g : wasmObj->globals())
    globals.emplace_back(make<InputGlobal>(g, this));

  // Populate `Tags`.
  for (const WasmTag &t : wasmObj->tags())
    tags.emplace_back(make<InputTag>(types[t.SigIndex], t, this));

  // Populate `Symbols` based on the symbols in the object.
  symbols.reserve(wasmObj->getNumberOfSymbols());
  uint32_t tableSymbolCount = 0;
  for (const SymbolRef &sym : wasmObj->symbols()) {
    const WasmSymbol &wasmSym = wasmObj->getWasmSymbol(sym.getRawDataRefImpl());
    if (wasmSym.isTypeTable())
      tableSymbolCount++;
    if (wasmSym.isDefined()) {
      // createDefined may fail if the symbol is comdat excluded in which case
      // we fall back to creating an undefined symbol
      if (Symbol *d = createDefined(wasmSym)) {
        symbols.push_back(d);
        continue;
      }
    }
    size_t idx = symbols.size();
    symbols.push_back(createUndefined(wasmSym, isCalledDirectly[idx]));
  }

  addLegacyIndirectFunctionTableIfNeeded(tableSymbolCount);
}

bool ObjFile::isExcludedByComdat(const InputChunk *chunk) const {
  uint32_t c = chunk->getComdat();
  if (c == UINT32_MAX)
    return false;
  return !keptComdats[c];
}

FunctionSymbol *ObjFile::getFunctionSymbol(uint32_t index) const {
  return cast<FunctionSymbol>(symbols[index]);
}

GlobalSymbol *ObjFile::getGlobalSymbol(uint32_t index) const {
  return cast<GlobalSymbol>(symbols[index]);
}

TagSymbol *ObjFile::getTagSymbol(uint32_t index) const {
  return cast<TagSymbol>(symbols[index]);
}

TableSymbol *ObjFile::getTableSymbol(uint32_t index) const {
  return cast<TableSymbol>(symbols[index]);
}

SectionSymbol *ObjFile::getSectionSymbol(uint32_t index) const {
  return cast<SectionSymbol>(symbols[index]);
}

DataSymbol *ObjFile::getDataSymbol(uint32_t index) const {
  return cast<DataSymbol>(symbols[index]);
}

Symbol *ObjFile::createDefined(const WasmSymbol &sym) {
  StringRef name = sym.Info.Name;
  uint32_t flags = sym.Info.Flags;

  switch (sym.Info.Kind) {
  case WASM_SYMBOL_TYPE_FUNCTION: {
    InputFunction *func =
        functions[sym.Info.ElementIndex - wasmObj->getNumImportedFunctions()];
    if (sym.isBindingLocal())
      return make<DefinedFunction>(name, flags, this, func);
    if (func->discarded)
      return nullptr;
    return symtab->addDefinedFunction(name, flags, this, func);
  }
  case WASM_SYMBOL_TYPE_DATA: {
    InputChunk *seg = segments[sym.Info.DataRef.Segment];
    auto offset = sym.Info.DataRef.Offset;
    auto size = sym.Info.DataRef.Size;
    // Support older (e.g. llvm 13) object files that pre-date the per-symbol
    // TLS flag, and symbols were assumed to be TLS by being defined in a TLS
    // segment.
    if (!(flags & WASM_SYMBOL_TLS) && seg->isTLS())
      flags |= WASM_SYMBOL_TLS;
    if (sym.isBindingLocal())
      return make<DefinedData>(name, flags, this, seg, offset, size);
    if (seg->discarded)
      return nullptr;
    return symtab->addDefinedData(name, flags, this, seg, offset, size);
  }
  case WASM_SYMBOL_TYPE_GLOBAL: {
    InputGlobal *global =
        globals[sym.Info.ElementIndex - wasmObj->getNumImportedGlobals()];
    if (sym.isBindingLocal())
      return make<DefinedGlobal>(name, flags, this, global);
    return symtab->addDefinedGlobal(name, flags, this, global);
  }
  case WASM_SYMBOL_TYPE_SECTION: {
    InputChunk *section = customSectionsByIndex[sym.Info.ElementIndex];
    assert(sym.isBindingLocal());
    // Need to return null if discarded here? data and func only do that when
    // binding is not local.
    if (section->discarded)
      return nullptr;
    return make<SectionSymbol>(flags, section, this);
  }
  case WASM_SYMBOL_TYPE_TAG: {
    InputTag *tag = tags[sym.Info.ElementIndex - wasmObj->getNumImportedTags()];
    if (sym.isBindingLocal())
      return make<DefinedTag>(name, flags, this, tag);
    return symtab->addDefinedTag(name, flags, this, tag);
  }
  case WASM_SYMBOL_TYPE_TABLE: {
    InputTable *table =
        tables[sym.Info.ElementIndex - wasmObj->getNumImportedTables()];
    if (sym.isBindingLocal())
      return make<DefinedTable>(name, flags, this, table);
    return symtab->addDefinedTable(name, flags, this, table);
  }
  }
  llvm_unreachable("unknown symbol kind");
}

Symbol *ObjFile::createUndefined(const WasmSymbol &sym, bool isCalledDirectly) {
  StringRef name = sym.Info.Name;
  uint32_t flags = sym.Info.Flags | WASM_SYMBOL_UNDEFINED;

  switch (sym.Info.Kind) {
  case WASM_SYMBOL_TYPE_FUNCTION:
    if (sym.isBindingLocal())
      return make<UndefinedFunction>(name, sym.Info.ImportName,
                                     sym.Info.ImportModule, flags, this,
                                     sym.Signature, isCalledDirectly);
    return symtab->addUndefinedFunction(name, sym.Info.ImportName,
                                        sym.Info.ImportModule, flags, this,
                                        sym.Signature, isCalledDirectly);
  case WASM_SYMBOL_TYPE_DATA:
    if (sym.isBindingLocal())
      return make<UndefinedData>(name, flags, this);
    return symtab->addUndefinedData(name, flags, this);
  case WASM_SYMBOL_TYPE_GLOBAL:
    if (sym.isBindingLocal())
      return make<UndefinedGlobal>(name, sym.Info.ImportName,
                                   sym.Info.ImportModule, flags, this,
                                   sym.GlobalType);
    return symtab->addUndefinedGlobal(name, sym.Info.ImportName,
                                      sym.Info.ImportModule, flags, this,
                                      sym.GlobalType);
  case WASM_SYMBOL_TYPE_TABLE:
    if (sym.isBindingLocal())
      return make<UndefinedTable>(name, sym.Info.ImportName,
                                  sym.Info.ImportModule, flags, this,
                                  sym.TableType);
    return symtab->addUndefinedTable(name, sym.Info.ImportName,
                                     sym.Info.ImportModule, flags, this,
                                     sym.TableType);
  case WASM_SYMBOL_TYPE_TAG:
    if (sym.isBindingLocal())
      return make<UndefinedTag>(name, sym.Info.ImportName,
                                sym.Info.ImportModule, flags, this,
                                sym.Signature);
    return symtab->addUndefinedTag(name, sym.Info.ImportName,
                                   sym.Info.ImportModule, flags, this,
                                   sym.Signature);
  case WASM_SYMBOL_TYPE_SECTION:
    llvm_unreachable("section symbols cannot be undefined");
  }
  llvm_unreachable("unknown symbol kind");
}

StringRef strip(StringRef s) { return s.trim(' '); }

void StubFile::parse() {
  bool first = true;

  SmallVector<StringRef> lines;
  mb.getBuffer().split(lines, '\n');
  for (StringRef line : lines) {
    line = line.trim();

    // File must begin with #STUB
    if (first) {
      assert(line == "#STUB");
      first = false;
    }

    // Lines starting with # are considered comments
    if (line.starts_with("#"))
      continue;

    StringRef sym;
    StringRef rest;
    std::tie(sym, rest) = line.split(':');
    sym = strip(sym);
    rest = strip(rest);

    symbolDependencies[sym] = {};

    while (rest.size()) {
      StringRef dep;
      std::tie(dep, rest) = rest.split(',');
      dep = strip(dep);
      symbolDependencies[sym].push_back(dep);
    }
  }
}

static uint8_t mapVisibility(GlobalValue::VisibilityTypes gvVisibility) {
  switch (gvVisibility) {
  case GlobalValue::DefaultVisibility:
    return WASM_SYMBOL_VISIBILITY_DEFAULT;
  case GlobalValue::HiddenVisibility:
  case GlobalValue::ProtectedVisibility:
    return WASM_SYMBOL_VISIBILITY_HIDDEN;
  }
  llvm_unreachable("unknown visibility");
}

static Symbol *createBitcodeSymbol(const std::vector<bool> &keptComdats,
                                   const lto::InputFile::Symbol &objSym,
                                   BitcodeFile &f) {
  StringRef name = saver().save(objSym.getName());

  uint32_t flags = objSym.isWeak() ? WASM_SYMBOL_BINDING_WEAK : 0;
  flags |= mapVisibility(objSym.getVisibility());

  int c = objSym.getComdatIndex();
  bool excludedByComdat = c != -1 && !keptComdats[c];

  if (objSym.isUndefined() || excludedByComdat) {
    flags |= WASM_SYMBOL_UNDEFINED;
    if (objSym.isExecutable())
      return symtab->addUndefinedFunction(name, std::nullopt, std::nullopt,
                                          flags, &f, nullptr, true);
    return symtab->addUndefinedData(name, flags, &f);
  }

  if (objSym.isExecutable())
    return symtab->addDefinedFunction(name, flags, &f, nullptr);
  return symtab->addDefinedData(name, flags, &f, nullptr, 0, 0);
}

BitcodeFile::BitcodeFile(MemoryBufferRef m, StringRef archiveName,
                         uint64_t offsetInArchive, bool lazy)
    : InputFile(BitcodeKind, m) {
  this->lazy = lazy;
  this->archiveName = std::string(archiveName);

  std::string path = mb.getBufferIdentifier().str();

  // ThinLTO assumes that all MemoryBufferRefs given to it have a unique
  // name. If two archives define two members with the same name, this
  // causes a collision which result in only one of the objects being taken
  // into consideration at LTO time (which very likely causes undefined
  // symbols later in the link stage). So we append file offset to make
  // filename unique.
  StringRef name = archiveName.empty()
                       ? saver().save(path)
                       : saver().save(archiveName + "(" + path::filename(path) +
                                      " at " + utostr(offsetInArchive) + ")");
  MemoryBufferRef mbref(mb.getBuffer(), name);

  obj = check(lto::InputFile::create(mbref));

  // If this isn't part of an archive, it's eagerly linked, so mark it live.
  if (archiveName.empty())
    markLive();
}

bool BitcodeFile::doneLTO = false;

void BitcodeFile::parseLazy() {
  for (auto [i, irSym] : llvm::enumerate(obj->symbols())) {
    if (irSym.isUndefined())
      continue;
    StringRef name = saver().save(irSym.getName());
    symtab->addLazy(name, this);
    // addLazy() may trigger this->extract() if an existing symbol is an
    // undefined symbol. If that happens, this function has served its purpose,
    // and we can exit from the loop early.
    if (!lazy)
      break;
  }
}

void BitcodeFile::parse(StringRef symName) {
  if (doneLTO) {
    error(toString(this) + ": attempt to add bitcode file after LTO (" + symName + ")");
    return;
  }

  Triple t(obj->getTargetTriple());
  if (!t.isWasm()) {
    error(toString(this) + ": machine type must be wasm32 or wasm64");
    return;
  }
  checkArch(t.getArch());
  std::vector<bool> keptComdats;
  // TODO Support nodeduplicate
  // https://github.com/llvm/llvm-project/issues/49875
  for (std::pair<StringRef, Comdat::SelectionKind> s : obj->getComdatTable())
    keptComdats.push_back(symtab->addComdat(s.first));

  for (const lto::InputFile::Symbol &objSym : obj->symbols())
    symbols.push_back(createBitcodeSymbol(keptComdats, objSym, *this));
}

} // namespace wasm
} // namespace lld
