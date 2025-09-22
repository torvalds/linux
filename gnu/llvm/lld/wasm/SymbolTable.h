//===- SymbolTable.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_WASM_SYMBOL_TABLE_H
#define LLD_WASM_SYMBOL_TABLE_H

#include "InputFiles.h"
#include "LTO.h"
#include "Symbols.h"
#include "lld/Common/LLVM.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/BinaryFormat/WasmTraits.h"
#include <optional>

namespace lld::wasm {

class InputSegment;

// SymbolTable is a bucket of all known symbols, including defined,
// undefined, or lazy symbols (the last one is symbols in archive
// files whose archive members are not yet loaded).
//
// We put all symbols of all files to a SymbolTable, and the
// SymbolTable selects the "best" symbols if there are name
// conflicts. For example, obviously, a defined symbol is better than
// an undefined symbol. Or, if there's a conflict between a lazy and a
// undefined, it'll read an archive member to read a real definition
// to replace the lazy symbol. The logic is implemented in the
// add*() functions, which are called by input files as they are parsed.
// There is one add* function per symbol type.
class SymbolTable {
public:
  ArrayRef<Symbol *> symbols() const { return symVector; }

  void wrap(Symbol *sym, Symbol *real, Symbol *wrap);

  void addFile(InputFile *file, StringRef symName = {});

  void compileBitcodeFiles();

  Symbol *find(StringRef name);

  void replace(StringRef name, Symbol* sym);

  void trace(StringRef name);

  Symbol *addSharedFunction(StringRef name, uint32_t flags, InputFile *file,
                            const WasmSignature *sig);
  Symbol *addSharedData(StringRef name, uint32_t flags, InputFile *file);
  Symbol *addDefinedFunction(StringRef name, uint32_t flags, InputFile *file,
                             InputFunction *function);
  Symbol *addDefinedData(StringRef name, uint32_t flags, InputFile *file,
                         InputChunk *segment, uint64_t address, uint64_t size);
  Symbol *addDefinedGlobal(StringRef name, uint32_t flags, InputFile *file,
                           InputGlobal *g);
  Symbol *addDefinedTag(StringRef name, uint32_t flags, InputFile *file,
                        InputTag *t);
  Symbol *addDefinedTable(StringRef name, uint32_t flags, InputFile *file,
                          InputTable *t);

  Symbol *addUndefinedFunction(StringRef name,
                               std::optional<StringRef> importName,
                               std::optional<StringRef> importModule,
                               uint32_t flags, InputFile *file,
                               const WasmSignature *signature,
                               bool isCalledDirectly);
  Symbol *addUndefinedData(StringRef name, uint32_t flags, InputFile *file);
  Symbol *addUndefinedGlobal(StringRef name,
                             std::optional<StringRef> importName,
                             std::optional<StringRef> importModule,
                             uint32_t flags, InputFile *file,
                             const WasmGlobalType *type);
  Symbol *addUndefinedTable(StringRef name, std::optional<StringRef> importName,
                            std::optional<StringRef> importModule,
                            uint32_t flags, InputFile *file,
                            const WasmTableType *type);
  Symbol *addUndefinedTag(StringRef name, std::optional<StringRef> importName,
                          std::optional<StringRef> importModule, uint32_t flags,
                          InputFile *file, const WasmSignature *sig);

  TableSymbol *resolveIndirectFunctionTable(bool required);

  void addLazy(StringRef name, InputFile *f);

  bool addComdat(StringRef name);

  DefinedData *addSyntheticDataSymbol(StringRef name, uint32_t flags);
  DefinedGlobal *addSyntheticGlobal(StringRef name, uint32_t flags,
                                    InputGlobal *global);
  DefinedFunction *addSyntheticFunction(StringRef name, uint32_t flags,
                                        InputFunction *function);
  DefinedData *addOptionalDataSymbol(StringRef name, uint64_t value = 0);
  DefinedGlobal *addOptionalGlobalSymbol(StringRef name, InputGlobal *global);
  DefinedTable *addSyntheticTable(StringRef name, uint32_t flags,
                                  InputTable *global);

  void handleSymbolVariants();
  void handleWeakUndefines();
  DefinedFunction *createUndefinedStub(const WasmSignature &sig);

private:
  std::pair<Symbol *, bool> insert(StringRef name, const InputFile *file);
  std::pair<Symbol *, bool> insertName(StringRef name);

  bool getFunctionVariant(Symbol* sym, const WasmSignature *sig,
                          const InputFile *file, Symbol **out);
  InputFunction *replaceWithUnreachable(Symbol *sym, const WasmSignature &sig,
                                        StringRef debugName);
  void replaceWithUndefined(Symbol *sym);

  TableSymbol *createDefinedIndirectFunctionTable(StringRef name);
  TableSymbol *createUndefinedIndirectFunctionTable(StringRef name);

  // Maps symbol names to index into the symVector.  -1 means that symbols
  // is to not yet in the vector but it should have tracing enabled if it is
  // ever added.
  llvm::DenseMap<llvm::CachedHashStringRef, int> symMap;
  std::vector<Symbol *> symVector;

  // For certain symbols types, e.g. function symbols, we allow for multiple
  // variants of the same symbol with different signatures.
  llvm::DenseMap<llvm::CachedHashStringRef, std::vector<Symbol *>> symVariants;
  llvm::DenseMap<WasmSignature, DefinedFunction *> stubFunctions;

  // Comdat groups define "link once" sections. If two comdat groups have the
  // same name, only one of them is linked, and the other is ignored. This set
  // is used to uniquify them.
  llvm::DenseSet<llvm::CachedHashStringRef> comdatGroups;

  // For LTO.
  std::unique_ptr<BitcodeCompiler> lto;
};

extern SymbolTable *symtab;

} // namespace lld::wasm

#endif
