//===- SymbolTable.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_ELF_SYMBOL_TABLE_H
#define LLD_ELF_SYMBOL_TABLE_H

#include "Symbols.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Compiler.h"

namespace lld::elf {

class InputFile;
class SharedFile;

struct ArmCmseEntryFunction {
  Symbol *acleSeSym;
  Symbol *sym;
};

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
// add*() functions, which are called by input files as they are parsed. There
// is one add* function per symbol type.
class SymbolTable {
public:
  ArrayRef<Symbol *> getSymbols() const { return symVector; }

  void wrap(Symbol *sym, Symbol *real, Symbol *wrap);

  Symbol *insert(StringRef name);

  template <typename T> Symbol *addSymbol(const T &newSym) {
    Symbol *sym = insert(newSym.getName());
    sym->resolve(newSym);
    return sym;
  }
  Symbol *addAndCheckDuplicate(const Defined &newSym);

  void scanVersionScript();

  Symbol *find(StringRef name);

  void handleDynamicList();

  Symbol *addUnusedUndefined(StringRef name,
                             uint8_t binding = llvm::ELF::STB_GLOBAL);

  // Set of .so files to not link the same shared object file more than once.
  llvm::DenseMap<llvm::CachedHashStringRef, SharedFile *> soNames;

  // Comdat groups define "link once" sections. If two comdat groups have the
  // same name, only one of them is linked, and the other is ignored. This map
  // is used to uniquify them.
  llvm::DenseMap<llvm::CachedHashStringRef, const InputFile *> comdatGroups;

  // The Map of __acle_se_<sym>, <sym> pairs found in the input objects.
  // Key is the <sym> name.
  llvm::SmallMapVector<StringRef, ArmCmseEntryFunction, 1> cmseSymMap;

  // Map of symbols defined in the Arm CMSE import library. The linker must
  // preserve the addresses in the output objects.
  llvm::StringMap<Defined *> cmseImportLib;

  // True if <sym> from the input Arm CMSE import library is written to the
  // output Arm CMSE import library.
  llvm::StringMap<bool> inCMSEOutImpLib;

private:
  SmallVector<Symbol *, 0> findByVersion(SymbolVersion ver);
  SmallVector<Symbol *, 0> findAllByVersion(SymbolVersion ver,
                                            bool includeNonDefault);

  llvm::StringMap<SmallVector<Symbol *, 0>> &getDemangledSyms();
  bool assignExactVersion(SymbolVersion ver, uint16_t versionId,
                          StringRef versionName, bool includeNonDefault);
  void assignWildcardVersion(SymbolVersion ver, uint16_t versionId,
                             bool includeNonDefault);

  // Global symbols and a map from symbol name to the index. The order is not
  // defined. We can use an arbitrary order, but it has to be deterministic even
  // when cross linking.
  llvm::DenseMap<llvm::CachedHashStringRef, int> symMap;
  SmallVector<Symbol *, 0> symVector;

  // A map from demangled symbol names to their symbol objects.
  // This mapping is 1:N because two symbols with different versions
  // can have the same name. We use this map to handle "extern C++ {}"
  // directive in version scripts.
  std::optional<llvm::StringMap<SmallVector<Symbol *, 0>>> demangledSyms;
};

LLVM_LIBRARY_VISIBILITY extern SymbolTable symtab;

} // namespace lld::elf

#endif
