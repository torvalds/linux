//===- SymbolTable.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_SYMBOL_TABLE_H
#define LLD_MACHO_SYMBOL_TABLE_H

#include "Symbols.h"

#include "lld/Common/LLVM.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Object/Archive.h"

namespace lld::macho {

class ArchiveFile;
class DylibFile;
class InputFile;
class ObjFile;
class InputSection;
class MachHeaderSection;
class Symbol;
class Defined;
class Undefined;

/*
 * Note that the SymbolTable handles name collisions by calling
 * replaceSymbol(), which does an in-place update of the Symbol via `placement
 * new`. Therefore, there is no need to update any relocations that hold
 * pointers the "old" Symbol -- they will automatically point to the new one.
 */
class SymbolTable {
public:
  Defined *addDefined(StringRef name, InputFile *, InputSection *,
                      uint64_t value, uint64_t size, bool isWeakDef,
                      bool isPrivateExtern, bool isReferencedDynamically,
                      bool noDeadStrip, bool isWeakDefCanBeHidden);

  Defined *aliasDefined(Defined *src, StringRef target, InputFile *newFile,
                        bool makePrivateExtern = false);

  Symbol *addUndefined(StringRef name, InputFile *, bool isWeakRef);

  Symbol *addCommon(StringRef name, InputFile *, uint64_t size, uint32_t align,
                    bool isPrivateExtern);

  Symbol *addDylib(StringRef name, DylibFile *file, bool isWeakDef, bool isTlv);
  Symbol *addDynamicLookup(StringRef name);

  Symbol *addLazyArchive(StringRef name, ArchiveFile *file,
                         const llvm::object::Archive::Symbol &sym);
  Symbol *addLazyObject(StringRef name, InputFile &file);

  Defined *addSynthetic(StringRef name, InputSection *, uint64_t value,
                        bool isPrivateExtern, bool includeInSymtab,
                        bool referencedDynamically);

  ArrayRef<Symbol *> getSymbols() const { return symVector; }
  Symbol *find(llvm::CachedHashStringRef name);
  Symbol *find(StringRef name) { return find(llvm::CachedHashStringRef(name)); }

private:
  std::pair<Symbol *, bool> insert(StringRef name, const InputFile *);
  llvm::DenseMap<llvm::CachedHashStringRef, int> symMap;
  std::vector<Symbol *> symVector;
};

void reportPendingUndefinedSymbols();
void reportPendingDuplicateSymbols();

// Call reportPendingUndefinedSymbols() to emit diagnostics.
void treatUndefinedSymbol(const Undefined &, StringRef source);
void treatUndefinedSymbol(const Undefined &, const InputSection *,
                          uint64_t offset);

extern std::unique_ptr<SymbolTable> symtab;

} // namespace lld::macho

#endif
