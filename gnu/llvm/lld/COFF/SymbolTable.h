//===- SymbolTable.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_SYMBOL_TABLE_H
#define LLD_COFF_SYMBOL_TABLE_H

#include "InputFiles.h"
#include "LTO.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
struct LTOCodeGenerator;
}

namespace lld::coff {

class Chunk;
class CommonChunk;
class COFFLinkerContext;
class Defined;
class DefinedAbsolute;
class DefinedRegular;
class LazyArchive;
class SectionChunk;
class Symbol;

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
  SymbolTable(COFFLinkerContext &c) : ctx(c) {}

  void addFile(InputFile *file);

  // Emit errors for symbols that cannot be resolved.
  void reportUnresolvable();

  // Try to resolve any undefined symbols and update the symbol table
  // accordingly, then print an error message for any remaining undefined
  // symbols and warn about imported local symbols.
  void resolveRemainingUndefines();

  // Load lazy objects that are needed for MinGW automatic import and for
  // doing stdcall fixups.
  void loadMinGWSymbols();
  bool handleMinGWAutomaticImport(Symbol *sym, StringRef name);

  // Returns a list of chunks of selected symbols.
  std::vector<Chunk *> getChunks() const;

  // Returns a symbol for a given name. Returns a nullptr if not found.
  Symbol *find(StringRef name) const;
  Symbol *findUnderscore(StringRef name) const;

  // Occasionally we have to resolve an undefined symbol to its
  // mangled symbol. This function tries to find a mangled name
  // for U from the symbol table, and if found, set the symbol as
  // a weak alias for U.
  Symbol *findMangle(StringRef name);

  // Build a set of COFF objects representing the combined contents of
  // BitcodeFiles and add them to the symbol table. Called after all files are
  // added and before the writer writes results to a file.
  void compileBitcodeFiles();

  // Creates an Undefined symbol for a given name.
  Symbol *addUndefined(StringRef name);

  Symbol *addSynthetic(StringRef n, Chunk *c);
  Symbol *addAbsolute(StringRef n, uint64_t va);

  Symbol *addUndefined(StringRef name, InputFile *f, bool isWeakAlias);
  void addLazyArchive(ArchiveFile *f, const Archive::Symbol &sym);
  void addLazyObject(InputFile *f, StringRef n);
  void addLazyDLLSymbol(DLLFile *f, DLLFile::Symbol *sym, StringRef n);
  Symbol *addAbsolute(StringRef n, COFFSymbolRef s);
  Symbol *addRegular(InputFile *f, StringRef n,
                     const llvm::object::coff_symbol_generic *s = nullptr,
                     SectionChunk *c = nullptr, uint32_t sectionOffset = 0,
                     bool isWeak = false);
  std::pair<DefinedRegular *, bool>
  addComdat(InputFile *f, StringRef n,
            const llvm::object::coff_symbol_generic *s = nullptr);
  Symbol *addCommon(InputFile *f, StringRef n, uint64_t size,
                    const llvm::object::coff_symbol_generic *s = nullptr,
                    CommonChunk *c = nullptr);
  Symbol *addImportData(StringRef n, ImportFile *f);
  Symbol *addImportThunk(StringRef name, DefinedImportData *s,
                         uint16_t machine);
  void addLibcall(StringRef name);
  void addEntryThunk(Symbol *from, Symbol *to);
  void initializeEntryThunks();

  void reportDuplicate(Symbol *existing, InputFile *newFile,
                       SectionChunk *newSc = nullptr,
                       uint32_t newSectionOffset = 0);

  // A list of chunks which to be added to .rdata.
  std::vector<Chunk *> localImportChunks;

  // Iterates symbols in non-determinstic hash table order.
  template <typename T> void forEachSymbol(T callback) {
    for (auto &pair : symMap)
      callback(pair.second);
  }

private:
  /// Given a name without "__imp_" prefix, returns a defined symbol
  /// with the "__imp_" prefix, if it exists.
  Defined *impSymbol(StringRef name);
  /// Inserts symbol if not already present.
  std::pair<Symbol *, bool> insert(StringRef name);
  /// Same as insert(Name), but also sets isUsedInRegularObj.
  std::pair<Symbol *, bool> insert(StringRef name, InputFile *f);

  std::vector<Symbol *> getSymsWithPrefix(StringRef prefix);

  llvm::DenseMap<llvm::CachedHashStringRef, Symbol *> symMap;
  std::unique_ptr<BitcodeCompiler> lto;
  bool ltoCompilationDone = false;
  std::vector<std::pair<Symbol *, Symbol *>> entryThunks;

  COFFLinkerContext &ctx;
};

std::vector<std::string> getSymbolLocations(ObjFile *file, uint32_t symIndex);

StringRef ltrim1(StringRef s, const char *chars);

} // namespace lld::coff

#endif
