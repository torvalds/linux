//===- Relocations.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Relocations.h"

#include "InputChunks.h"
#include "OutputSegment.h"
#include "SymbolTable.h"
#include "SyntheticSections.h"

using namespace llvm;
using namespace llvm::wasm;

namespace lld::wasm {

static bool requiresGOTAccess(const Symbol *sym) {
  if (sym->isShared())
    return true;
  if (!ctx.isPic &&
      config->unresolvedSymbols != UnresolvedPolicy::ImportDynamic)
    return false;
  if (sym->isHidden() || sym->isLocal())
    return false;
  // With `-Bsymbolic` (or when building an executable) as don't need to use
  // the GOT for symbols that are defined within the current module.
  if (sym->isDefined() && (!config->shared || config->bsymbolic))
    return false;
  return true;
}

static bool allowUndefined(const Symbol* sym) {
  // Symbols that are explicitly imported are always allowed to be undefined at
  // link time.
  if (sym->isImported())
    return true;
  if (isa<UndefinedFunction>(sym) && config->importUndefined)
    return true;

  return config->allowUndefinedSymbols.count(sym->getName()) != 0;
}

static void reportUndefined(ObjFile *file, Symbol *sym) {
  if (!allowUndefined(sym)) {
    switch (config->unresolvedSymbols) {
    case UnresolvedPolicy::ReportError:
      error(toString(file) + ": undefined symbol: " + toString(*sym));
      break;
    case UnresolvedPolicy::Warn:
      warn(toString(file) + ": undefined symbol: " + toString(*sym));
      break;
    case UnresolvedPolicy::Ignore:
      LLVM_DEBUG(dbgs() << "ignoring undefined symbol: " + toString(*sym) +
                               "\n");
      break;
    case UnresolvedPolicy::ImportDynamic:
      break;
    }

    if (auto *f = dyn_cast<UndefinedFunction>(sym)) {
      if (!f->stubFunction &&
          config->unresolvedSymbols != UnresolvedPolicy::ImportDynamic &&
          !config->importUndefined) {
        f->stubFunction = symtab->createUndefinedStub(*f->getSignature());
        f->stubFunction->markLive();
        // Mark the function itself as a stub which prevents it from being
        // assigned a table entry.
        f->isStub = true;
      }
    }
  }
}

static void addGOTEntry(Symbol *sym) {
  if (requiresGOTAccess(sym))
    out.importSec->addGOTEntry(sym);
  else
    out.globalSec->addInternalGOTEntry(sym);
}

void scanRelocations(InputChunk *chunk) {
  if (!chunk->live)
    return;
  ObjFile *file = chunk->file;
  ArrayRef<WasmSignature> types = file->getWasmObj()->types();
  for (const WasmRelocation &reloc : chunk->getRelocations()) {
    if (reloc.Type == R_WASM_TYPE_INDEX_LEB) {
      // Mark target type as live
      file->typeMap[reloc.Index] =
          out.typeSec->registerType(types[reloc.Index]);
      file->typeIsUsed[reloc.Index] = true;
      continue;
    }

    // Other relocation types all have a corresponding symbol
    Symbol *sym = file->getSymbols()[reloc.Index];

    switch (reloc.Type) {
    case R_WASM_TABLE_INDEX_I32:
    case R_WASM_TABLE_INDEX_I64:
    case R_WASM_TABLE_INDEX_SLEB:
    case R_WASM_TABLE_INDEX_SLEB64:
    case R_WASM_TABLE_INDEX_REL_SLEB:
    case R_WASM_TABLE_INDEX_REL_SLEB64:
      if (requiresGOTAccess(sym))
        break;
      out.elemSec->addEntry(cast<FunctionSymbol>(sym));
      break;
    case R_WASM_GLOBAL_INDEX_LEB:
    case R_WASM_GLOBAL_INDEX_I32:
      if (!isa<GlobalSymbol>(sym))
        addGOTEntry(sym);
      break;
    case R_WASM_MEMORY_ADDR_TLS_SLEB:
    case R_WASM_MEMORY_ADDR_TLS_SLEB64:
      if (!sym->isDefined()) {
        error(toString(file) + ": relocation " + relocTypeToString(reloc.Type) +
              " cannot be used against an undefined symbol `" + toString(*sym) +
              "`");
      }
      // In single-threaded builds TLS is lowered away and TLS data can be
      // merged with normal data and allowing TLS relocation in non-TLS
      // segments.
      if (config->sharedMemory) {
        if (!sym->isTLS()) {
          error(toString(file) + ": relocation " +
                relocTypeToString(reloc.Type) +
                " cannot be used against non-TLS symbol `" + toString(*sym) +
                "`");
        }
        if (auto *D = dyn_cast<DefinedData>(sym)) {
          if (!D->segment->outputSeg->isTLS()) {
            error(toString(file) + ": relocation " +
                  relocTypeToString(reloc.Type) + " cannot be used against `" +
                  toString(*sym) +
                  "` in non-TLS section: " + D->segment->outputSeg->name);
          }
        }
      }
      break;
    }

    if (ctx.isPic ||
        (sym->isUndefined() &&
         config->unresolvedSymbols == UnresolvedPolicy::ImportDynamic)) {
      switch (reloc.Type) {
      case R_WASM_TABLE_INDEX_SLEB:
      case R_WASM_TABLE_INDEX_SLEB64:
      case R_WASM_MEMORY_ADDR_SLEB:
      case R_WASM_MEMORY_ADDR_LEB:
      case R_WASM_MEMORY_ADDR_SLEB64:
      case R_WASM_MEMORY_ADDR_LEB64:
        // Certain relocation types can't be used when building PIC output,
        // since they would require absolute symbol addresses at link time.
        error(toString(file) + ": relocation " + relocTypeToString(reloc.Type) +
              " cannot be used against symbol `" + toString(*sym) +
              "`; recompile with -fPIC");
        break;
      case R_WASM_TABLE_INDEX_I32:
      case R_WASM_TABLE_INDEX_I64:
      case R_WASM_MEMORY_ADDR_I32:
      case R_WASM_MEMORY_ADDR_I64:
        // These relocation types are only present in the data section and
        // will be converted into code by `generateRelocationCode`.  This
        // code requires the symbols to have GOT entries.
        if (requiresGOTAccess(sym))
          addGOTEntry(sym);
        break;
      }
    }

    if (sym->isUndefined() && !config->relocatable && !sym->isWeak()) {
      // Report undefined symbols
      reportUndefined(file, sym);
    }
  }
}

} // namespace lld::wasm
