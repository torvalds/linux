//===- Symbols.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Config.h"
#include "InputChunks.h"
#include "InputElement.h"
#include "InputFiles.h"
#include "OutputSections.h"
#include "OutputSegment.h"
#include "SymbolTable.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Memory.h"
#include "llvm/Demangle/Demangle.h"

#define DEBUG_TYPE "lld"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::wasm;
using namespace lld::wasm;

namespace lld {
std::string toString(const wasm::Symbol &sym) {
  return maybeDemangleSymbol(sym.getName());
}

std::string maybeDemangleSymbol(StringRef name) {
  // WebAssembly requires caller and callee signatures to match, so we mangle
  // `main` in the case where we need to pass it arguments.
  if (name == "__main_argc_argv")
    return "main";
  if (wasm::config->demangle)
    return demangle(name);
  return name.str();
}

std::string toString(wasm::Symbol::Kind kind) {
  switch (kind) {
  case wasm::Symbol::DefinedFunctionKind:
    return "DefinedFunction";
  case wasm::Symbol::DefinedDataKind:
    return "DefinedData";
  case wasm::Symbol::DefinedGlobalKind:
    return "DefinedGlobal";
  case wasm::Symbol::DefinedTableKind:
    return "DefinedTable";
  case wasm::Symbol::DefinedTagKind:
    return "DefinedTag";
  case wasm::Symbol::UndefinedFunctionKind:
    return "UndefinedFunction";
  case wasm::Symbol::UndefinedDataKind:
    return "UndefinedData";
  case wasm::Symbol::UndefinedGlobalKind:
    return "UndefinedGlobal";
  case wasm::Symbol::UndefinedTableKind:
    return "UndefinedTable";
  case wasm::Symbol::UndefinedTagKind:
    return "UndefinedTag";
  case wasm::Symbol::LazyKind:
    return "LazyKind";
  case wasm::Symbol::SectionKind:
    return "SectionKind";
  case wasm::Symbol::OutputSectionKind:
    return "OutputSectionKind";
  case wasm::Symbol::SharedFunctionKind:
    return "SharedFunctionKind";
  case wasm::Symbol::SharedDataKind:
    return "SharedDataKind";
  }
  llvm_unreachable("invalid symbol kind");
}

namespace wasm {
DefinedFunction *WasmSym::callCtors;
DefinedFunction *WasmSym::callDtors;
DefinedFunction *WasmSym::initMemory;
DefinedFunction *WasmSym::applyDataRelocs;
DefinedFunction *WasmSym::applyGlobalRelocs;
DefinedFunction *WasmSym::applyTLSRelocs;
DefinedFunction *WasmSym::applyGlobalTLSRelocs;
DefinedFunction *WasmSym::initTLS;
DefinedFunction *WasmSym::startFunction;
DefinedData *WasmSym::dsoHandle;
DefinedData *WasmSym::dataEnd;
DefinedData *WasmSym::globalBase;
DefinedData *WasmSym::heapBase;
DefinedData *WasmSym::heapEnd;
DefinedData *WasmSym::initMemoryFlag;
GlobalSymbol *WasmSym::stackPointer;
DefinedData *WasmSym::stackLow;
DefinedData *WasmSym::stackHigh;
GlobalSymbol *WasmSym::tlsBase;
GlobalSymbol *WasmSym::tlsSize;
GlobalSymbol *WasmSym::tlsAlign;
UndefinedGlobal *WasmSym::tableBase;
DefinedData *WasmSym::definedTableBase;
UndefinedGlobal *WasmSym::memoryBase;
DefinedData *WasmSym::definedMemoryBase;
TableSymbol *WasmSym::indirectFunctionTable;

WasmSymbolType Symbol::getWasmType() const {
  if (isa<FunctionSymbol>(this))
    return WASM_SYMBOL_TYPE_FUNCTION;
  if (isa<DataSymbol>(this))
    return WASM_SYMBOL_TYPE_DATA;
  if (isa<GlobalSymbol>(this))
    return WASM_SYMBOL_TYPE_GLOBAL;
  if (isa<TagSymbol>(this))
    return WASM_SYMBOL_TYPE_TAG;
  if (isa<TableSymbol>(this))
    return WASM_SYMBOL_TYPE_TABLE;
  if (isa<SectionSymbol>(this) || isa<OutputSectionSymbol>(this))
    return WASM_SYMBOL_TYPE_SECTION;
  llvm_unreachable("invalid symbol kind");
}

const WasmSignature *Symbol::getSignature() const {
  if (auto* f = dyn_cast<FunctionSymbol>(this))
    return f->signature;
  if (auto *t = dyn_cast<TagSymbol>(this))
    return t->signature;
  if (auto *l = dyn_cast<LazySymbol>(this))
    return l->signature;
  return nullptr;
}

InputChunk *Symbol::getChunk() const {
  if (auto *f = dyn_cast<DefinedFunction>(this))
    return f->function;
  if (auto *f = dyn_cast<UndefinedFunction>(this))
    if (f->stubFunction)
      return f->stubFunction->function;
  if (auto *d = dyn_cast<DefinedData>(this))
    return d->segment;
  return nullptr;
}

bool Symbol::isDiscarded() const {
  if (InputChunk *c = getChunk())
    return c->discarded;
  return false;
}

bool Symbol::isLive() const {
  if (auto *g = dyn_cast<DefinedGlobal>(this))
    return g->global->live;
  if (auto *t = dyn_cast<DefinedTag>(this))
    return t->tag->live;
  if (auto *t = dyn_cast<DefinedTable>(this))
    return t->table->live;
  if (InputChunk *c = getChunk())
    return c->live;
  return referenced;
}

void Symbol::markLive() {
  assert(!isDiscarded());
  referenced = true;
  if (file != nullptr && isDefined())
    file->markLive();
  if (auto *g = dyn_cast<DefinedGlobal>(this))
    g->global->live = true;
  if (auto *t = dyn_cast<DefinedTag>(this))
    t->tag->live = true;
  if (auto *t = dyn_cast<DefinedTable>(this))
    t->table->live = true;
  if (InputChunk *c = getChunk()) {
    // Usually, a whole chunk is marked as live or dead, but in mergeable
    // (splittable) sections, each piece of data has independent liveness bit.
    // So we explicitly tell it which offset is in use.
    if (auto *d = dyn_cast<DefinedData>(this)) {
      if (auto *ms = dyn_cast<MergeInputChunk>(c)) {
        ms->getSectionPiece(d->value)->live = true;
      }
    }
    c->live = true;
  }
}

uint32_t Symbol::getOutputSymbolIndex() const {
  assert(outputSymbolIndex != INVALID_INDEX);
  return outputSymbolIndex;
}

void Symbol::setOutputSymbolIndex(uint32_t index) {
  LLVM_DEBUG(dbgs() << "setOutputSymbolIndex " << name << " -> " << index
                    << "\n");
  assert(outputSymbolIndex == INVALID_INDEX);
  outputSymbolIndex = index;
}

void Symbol::setGOTIndex(uint32_t index) {
  LLVM_DEBUG(dbgs() << "setGOTIndex " << name << " -> " << index << "\n");
  assert(gotIndex == INVALID_INDEX);
  gotIndex = index;
}

bool Symbol::isWeak() const {
  return (flags & WASM_SYMBOL_BINDING_MASK) == WASM_SYMBOL_BINDING_WEAK;
}

bool Symbol::isLocal() const {
  return (flags & WASM_SYMBOL_BINDING_MASK) == WASM_SYMBOL_BINDING_LOCAL;
}

bool Symbol::isHidden() const {
  return (flags & WASM_SYMBOL_VISIBILITY_MASK) == WASM_SYMBOL_VISIBILITY_HIDDEN;
}

bool Symbol::isTLS() const { return flags & WASM_SYMBOL_TLS; }

void Symbol::setHidden(bool isHidden) {
  LLVM_DEBUG(dbgs() << "setHidden: " << name << " -> " << isHidden << "\n");
  flags &= ~WASM_SYMBOL_VISIBILITY_MASK;
  if (isHidden)
    flags |= WASM_SYMBOL_VISIBILITY_HIDDEN;
  else
    flags |= WASM_SYMBOL_VISIBILITY_DEFAULT;
}

bool Symbol::isImported() const {
  return isShared() ||
         (isUndefined() && (importName.has_value() || forceImport));
}

bool Symbol::isExported() const {
  if (!isDefined() || isShared() || isLocal())
    return false;

  // Shared libraries must export all weakly defined symbols
  // in case they contain the version that will be chosen by
  // the dynamic linker.
  if (config->shared && isLive() && isWeak() && !isHidden())
    return true;

  if (config->exportAll || (config->exportDynamic && !isHidden()))
    return true;

  return isExportedExplicit();
}

bool Symbol::isExportedExplicit() const {
  return forceExport || flags & WASM_SYMBOL_EXPORTED;
}

bool Symbol::isNoStrip() const {
  return flags & WASM_SYMBOL_NO_STRIP;
}

uint32_t FunctionSymbol::getFunctionIndex() const {
  if (const auto *u = dyn_cast<UndefinedFunction>(this))
    if (u->stubFunction)
      return u->stubFunction->getFunctionIndex();
  if (functionIndex != INVALID_INDEX)
    return functionIndex;
  auto *f = cast<DefinedFunction>(this);
  return f->function->getFunctionIndex();
}

void FunctionSymbol::setFunctionIndex(uint32_t index) {
  LLVM_DEBUG(dbgs() << "setFunctionIndex " << name << " -> " << index << "\n");
  assert(functionIndex == INVALID_INDEX);
  functionIndex = index;
}

bool FunctionSymbol::hasFunctionIndex() const {
  if (auto *f = dyn_cast<DefinedFunction>(this))
    return f->function->hasFunctionIndex();
  return functionIndex != INVALID_INDEX;
}

uint32_t FunctionSymbol::getTableIndex() const {
  if (auto *f = dyn_cast<DefinedFunction>(this))
    return f->function->getTableIndex();
  assert(tableIndex != INVALID_INDEX);
  return tableIndex;
}

bool FunctionSymbol::hasTableIndex() const {
  if (auto *f = dyn_cast<DefinedFunction>(this))
    return f->function->hasTableIndex();
  return tableIndex != INVALID_INDEX;
}

void FunctionSymbol::setTableIndex(uint32_t index) {
  // For imports, we set the table index here on the Symbol; for defined
  // functions we set the index on the InputFunction so that we don't export
  // the same thing twice (keeps the table size down).
  if (auto *f = dyn_cast<DefinedFunction>(this)) {
    f->function->setTableIndex(index);
    return;
  }
  LLVM_DEBUG(dbgs() << "setTableIndex " << name << " -> " << index << "\n");
  assert(tableIndex == INVALID_INDEX);
  tableIndex = index;
}

DefinedFunction::DefinedFunction(StringRef name, uint32_t flags, InputFile *f,
                                 InputFunction *function)
    : FunctionSymbol(name, DefinedFunctionKind, flags, f,
                     function ? &function->signature : nullptr),
      function(function) {}

uint32_t DefinedFunction::getExportedFunctionIndex() const {
  return function->getFunctionIndex();
}

uint64_t DefinedData::getVA() const {
  LLVM_DEBUG(dbgs() << "getVA: " << getName() << "\n");
  // In the shared memory case, TLS symbols are relative to the start of the TLS
  // output segment (__tls_base).  When building without shared memory, TLS
  // symbols absolute, just like non-TLS.
  if (isTLS() && config->sharedMemory)
    return getOutputSegmentOffset();
  if (segment)
    return segment->getVA(value);
  return value;
}

void DefinedData::setVA(uint64_t value_) {
  LLVM_DEBUG(dbgs() << "setVA " << name << " -> " << value_ << "\n");
  assert(!segment);
  value = value_;
}

uint64_t DefinedData::getOutputSegmentOffset() const {
  LLVM_DEBUG(dbgs() << "getOutputSegmentOffset: " << getName() << "\n");
  return segment->getChunkOffset(value);
}

uint64_t DefinedData::getOutputSegmentIndex() const {
  LLVM_DEBUG(dbgs() << "getOutputSegmentIndex: " << getName() << "\n");
  return segment->outputSeg->index;
}

uint32_t GlobalSymbol::getGlobalIndex() const {
  if (auto *f = dyn_cast<DefinedGlobal>(this))
    return f->global->getAssignedIndex();
  assert(globalIndex != INVALID_INDEX);
  return globalIndex;
}

void GlobalSymbol::setGlobalIndex(uint32_t index) {
  LLVM_DEBUG(dbgs() << "setGlobalIndex " << name << " -> " << index << "\n");
  assert(globalIndex == INVALID_INDEX);
  globalIndex = index;
}

bool GlobalSymbol::hasGlobalIndex() const {
  if (auto *f = dyn_cast<DefinedGlobal>(this))
    return f->global->hasAssignedIndex();
  return globalIndex != INVALID_INDEX;
}

DefinedGlobal::DefinedGlobal(StringRef name, uint32_t flags, InputFile *file,
                             InputGlobal *global)
    : GlobalSymbol(name, DefinedGlobalKind, flags, file,
                   global ? &global->getType() : nullptr),
      global(global) {}

uint32_t TagSymbol::getTagIndex() const {
  if (auto *f = dyn_cast<DefinedTag>(this))
    return f->tag->getAssignedIndex();
  assert(tagIndex != INVALID_INDEX);
  return tagIndex;
}

void TagSymbol::setTagIndex(uint32_t index) {
  LLVM_DEBUG(dbgs() << "setTagIndex " << name << " -> " << index << "\n");
  assert(tagIndex == INVALID_INDEX);
  tagIndex = index;
}

bool TagSymbol::hasTagIndex() const {
  if (auto *f = dyn_cast<DefinedTag>(this))
    return f->tag->hasAssignedIndex();
  return tagIndex != INVALID_INDEX;
}

DefinedTag::DefinedTag(StringRef name, uint32_t flags, InputFile *file,
                       InputTag *tag)
    : TagSymbol(name, DefinedTagKind, flags, file,
                tag ? &tag->signature : nullptr),
      tag(tag) {}

void TableSymbol::setLimits(const WasmLimits &limits) {
  if (auto *t = dyn_cast<DefinedTable>(this))
    t->table->setLimits(limits);
  auto *newType = make<WasmTableType>(*tableType);
  newType->Limits = limits;
  tableType = newType;
}

uint32_t TableSymbol::getTableNumber() const {
  if (const auto *t = dyn_cast<DefinedTable>(this))
    return t->table->getAssignedIndex();
  assert(tableNumber != INVALID_INDEX);
  return tableNumber;
}

void TableSymbol::setTableNumber(uint32_t number) {
  if (const auto *t = dyn_cast<DefinedTable>(this))
    return t->table->assignIndex(number);
  LLVM_DEBUG(dbgs() << "setTableNumber " << name << " -> " << number << "\n");
  assert(tableNumber == INVALID_INDEX);
  tableNumber = number;
}

bool TableSymbol::hasTableNumber() const {
  if (const auto *t = dyn_cast<DefinedTable>(this))
    return t->table->hasAssignedIndex();
  return tableNumber != INVALID_INDEX;
}

DefinedTable::DefinedTable(StringRef name, uint32_t flags, InputFile *file,
                           InputTable *table)
    : TableSymbol(name, DefinedTableKind, flags, file,
                  table ? &table->getType() : nullptr),
      table(table) {}

const OutputSectionSymbol *SectionSymbol::getOutputSectionSymbol() const {
  assert(section->outputSec && section->outputSec->sectionSym);
  return section->outputSec->sectionSym;
}

void LazySymbol::extract() {
  if (file->lazy) {
    file->lazy = false;
    symtab->addFile(file, name);
  }
}

void LazySymbol::setWeak() {
  flags |= (flags & ~WASM_SYMBOL_BINDING_MASK) | WASM_SYMBOL_BINDING_WEAK;
}

void printTraceSymbolUndefined(StringRef name, const InputFile* file) {
  message(toString(file) + ": reference to " + name);
}

// Print out a log message for --trace-symbol.
void printTraceSymbol(Symbol *sym) {
  // Undefined symbols are traced via printTraceSymbolUndefined
  if (sym->isUndefined())
    return;

  std::string s;
  if (sym->isLazy())
    s = ": lazy definition of ";
  else
    s = ": definition of ";

  message(toString(sym->getFile()) + s + sym->getName());
}

const char *defaultModule = "env";
const char *functionTableName = "__indirect_function_table";
const char *memoryName = "memory";

} // namespace wasm
} // namespace lld
