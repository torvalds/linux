//===- IRSymtab.h - data definitions for IR symbol tables -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains data definitions and a reader and builder for a symbol
// table for LLVM IR. Its purpose is to allow linkers and other consumers of
// bitcode files to efficiently read the symbol table for symbol resolution
// purposes without needing to construct a module in memory.
//
// As with most object files the symbol table has two parts: the symbol table
// itself and a string table which is referenced by the symbol table.
//
// A symbol table corresponds to a single bitcode file, which may consist of
// multiple modules, so symbol tables may likewise contain symbols for multiple
// modules.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_IRSYMTAB_H
#define LLVM_OBJECT_IRSYMTAB_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstdint>
#include <vector>

namespace llvm {

struct BitcodeFileContents;
class StringTableBuilder;

namespace irsymtab {

namespace storage {

// The data structures in this namespace define the low-level serialization
// format. Clients that just want to read a symbol table should use the
// irsymtab::Reader class.

using Word = support::ulittle32_t;

/// A reference to a string in the string table.
struct Str {
  Word Offset, Size;

  StringRef get(StringRef Strtab) const {
    return {Strtab.data() + Offset, Size};
  }
};

/// A reference to a range of objects in the symbol table.
template <typename T> struct Range {
  Word Offset, Size;

  ArrayRef<T> get(StringRef Symtab) const {
    return {reinterpret_cast<const T *>(Symtab.data() + Offset), Size};
  }
};

/// Describes the range of a particular module's symbols within the symbol
/// table.
struct Module {
  Word Begin, End;

  /// The index of the first Uncommon for this Module.
  Word UncBegin;
};

/// This is equivalent to an IR comdat.
struct Comdat {
  Str Name;

  // llvm::Comdat::SelectionKind
  Word SelectionKind;
};

/// Contains the information needed by linkers for symbol resolution, as well as
/// by the LTO implementation itself.
struct Symbol {
  /// The mangled symbol name.
  Str Name;

  /// The unmangled symbol name, or the empty string if this is not an IR
  /// symbol.
  Str IRName;

  /// The index into Header::Comdats, or -1 if not a comdat member.
  Word ComdatIndex;

  Word Flags;
  enum FlagBits {
    FB_visibility, // 2 bits
    FB_has_uncommon = FB_visibility + 2,
    FB_undefined,
    FB_weak,
    FB_common,
    FB_indirect,
    FB_used,
    FB_tls,
    FB_may_omit,
    FB_global,
    FB_format_specific,
    FB_unnamed_addr,
    FB_executable,
  };
};

/// This data structure contains rarely used symbol fields and is optionally
/// referenced by a Symbol.
struct Uncommon {
  Word CommonSize, CommonAlign;

  /// COFF-specific: the name of the symbol that a weak external resolves to
  /// if not defined.
  Str COFFWeakExternFallbackName;

  /// Specified section name, if any.
  Str SectionName;
};


struct Header {
  /// Version number of the symtab format. This number should be incremented
  /// when the format changes, but it does not need to be incremented if a
  /// change to LLVM would cause it to create a different symbol table.
  Word Version;
  enum { kCurrentVersion = 3 };

  /// The producer's version string (LLVM_VERSION_STRING " " LLVM_REVISION).
  /// Consumers should rebuild the symbol table from IR if the producer's
  /// version does not match the consumer's version due to potential differences
  /// in symbol table format, symbol enumeration order and so on.
  Str Producer;

  Range<Module> Modules;
  Range<Comdat> Comdats;
  Range<Symbol> Symbols;
  Range<Uncommon> Uncommons;

  Str TargetTriple, SourceFileName;

  /// COFF-specific: linker directives.
  Str COFFLinkerOpts;

  /// Dependent Library Specifiers
  Range<Str> DependentLibraries;
};

} // end namespace storage

/// Fills in Symtab and StrtabBuilder with a valid symbol and string table for
/// Mods.
Error build(ArrayRef<Module *> Mods, SmallVector<char, 0> &Symtab,
            StringTableBuilder &StrtabBuilder, BumpPtrAllocator &Alloc);

/// This represents a symbol that has been read from a storage::Symbol and
/// possibly a storage::Uncommon.
struct Symbol {
  // Copied from storage::Symbol.
  StringRef Name, IRName;
  int ComdatIndex;
  uint32_t Flags;

  // Copied from storage::Uncommon.
  uint32_t CommonSize, CommonAlign;
  StringRef COFFWeakExternFallbackName;
  StringRef SectionName;

  /// Returns the mangled symbol name.
  StringRef getName() const { return Name; }

  /// Returns the unmangled symbol name, or the empty string if this is not an
  /// IR symbol.
  StringRef getIRName() const { return IRName; }

  /// Returns the index into the comdat table (see Reader::getComdatTable()), or
  /// -1 if not a comdat member.
  int getComdatIndex() const { return ComdatIndex; }

  using S = storage::Symbol;

  GlobalValue::VisibilityTypes getVisibility() const {
    return GlobalValue::VisibilityTypes((Flags >> S::FB_visibility) & 3);
  }

  bool isUndefined() const { return (Flags >> S::FB_undefined) & 1; }
  bool isWeak() const { return (Flags >> S::FB_weak) & 1; }
  bool isCommon() const { return (Flags >> S::FB_common) & 1; }
  bool isIndirect() const { return (Flags >> S::FB_indirect) & 1; }
  bool isUsed() const { return (Flags >> S::FB_used) & 1; }
  bool isTLS() const { return (Flags >> S::FB_tls) & 1; }

  bool canBeOmittedFromSymbolTable() const {
    return (Flags >> S::FB_may_omit) & 1;
  }

  bool isGlobal() const { return (Flags >> S::FB_global) & 1; }
  bool isFormatSpecific() const { return (Flags >> S::FB_format_specific) & 1; }
  bool isUnnamedAddr() const { return (Flags >> S::FB_unnamed_addr) & 1; }
  bool isExecutable() const { return (Flags >> S::FB_executable) & 1; }

  uint64_t getCommonSize() const {
    assert(isCommon());
    return CommonSize;
  }

  uint32_t getCommonAlignment() const {
    assert(isCommon());
    return CommonAlign;
  }

  /// COFF-specific: for weak externals, returns the name of the symbol that is
  /// used as a fallback if the weak external remains undefined.
  StringRef getCOFFWeakExternalFallback() const {
    assert(isWeak() && isIndirect());
    return COFFWeakExternFallbackName;
  }

  StringRef getSectionName() const { return SectionName; }
};

/// This class can be used to read a Symtab and Strtab produced by
/// irsymtab::build.
class Reader {
  StringRef Symtab, Strtab;

  ArrayRef<storage::Module> Modules;
  ArrayRef<storage::Comdat> Comdats;
  ArrayRef<storage::Symbol> Symbols;
  ArrayRef<storage::Uncommon> Uncommons;
  ArrayRef<storage::Str> DependentLibraries;

  StringRef str(storage::Str S) const { return S.get(Strtab); }

  template <typename T> ArrayRef<T> range(storage::Range<T> R) const {
    return R.get(Symtab);
  }

  const storage::Header &header() const {
    return *reinterpret_cast<const storage::Header *>(Symtab.data());
  }

public:
  class SymbolRef;

  Reader() = default;
  Reader(StringRef Symtab, StringRef Strtab) : Symtab(Symtab), Strtab(Strtab) {
    Modules = range(header().Modules);
    Comdats = range(header().Comdats);
    Symbols = range(header().Symbols);
    Uncommons = range(header().Uncommons);
    DependentLibraries = range(header().DependentLibraries);
  }

  using symbol_range = iterator_range<object::content_iterator<SymbolRef>>;

  /// Returns the symbol table for the entire bitcode file.
  /// The symbols enumerated by this method are ephemeral, but they can be
  /// copied into an irsymtab::Symbol object.
  symbol_range symbols() const;

  size_t getNumModules() const { return Modules.size(); }

  /// Returns a slice of the symbol table for the I'th module in the file.
  /// The symbols enumerated by this method are ephemeral, but they can be
  /// copied into an irsymtab::Symbol object.
  symbol_range module_symbols(unsigned I) const;

  StringRef getTargetTriple() const { return str(header().TargetTriple); }

  /// Returns the source file path specified at compile time.
  StringRef getSourceFileName() const { return str(header().SourceFileName); }

  /// Returns a table with all the comdats used by this file.
  std::vector<std::pair<StringRef, llvm::Comdat::SelectionKind>>
  getComdatTable() const {
    std::vector<std::pair<StringRef, llvm::Comdat::SelectionKind>> ComdatTable;
    ComdatTable.reserve(Comdats.size());
    for (auto C : Comdats)
      ComdatTable.push_back({str(C.Name), llvm::Comdat::SelectionKind(
                                              uint32_t(C.SelectionKind))});
    return ComdatTable;
  }

  /// COFF-specific: returns linker options specified in the input file.
  StringRef getCOFFLinkerOpts() const { return str(header().COFFLinkerOpts); }

  /// Returns dependent library specifiers
  std::vector<StringRef> getDependentLibraries() const {
    std::vector<StringRef> Specifiers;
    Specifiers.reserve(DependentLibraries.size());
    for (auto S : DependentLibraries) {
      Specifiers.push_back(str(S));
    }
    return Specifiers;
  }
};

/// Ephemeral symbols produced by Reader::symbols() and
/// Reader::module_symbols().
class Reader::SymbolRef : public Symbol {
  const storage::Symbol *SymI, *SymE;
  const storage::Uncommon *UncI;
  const Reader *R;

  void read() {
    if (SymI == SymE)
      return;

    Name = R->str(SymI->Name);
    IRName = R->str(SymI->IRName);
    ComdatIndex = SymI->ComdatIndex;
    Flags = SymI->Flags;

    if (Flags & (1 << storage::Symbol::FB_has_uncommon)) {
      CommonSize = UncI->CommonSize;
      CommonAlign = UncI->CommonAlign;
      COFFWeakExternFallbackName = R->str(UncI->COFFWeakExternFallbackName);
      SectionName = R->str(UncI->SectionName);
    } else
      // Reset this field so it can be queried unconditionally for all symbols.
      SectionName = "";
  }

public:
  SymbolRef(const storage::Symbol *SymI, const storage::Symbol *SymE,
            const storage::Uncommon *UncI, const Reader *R)
      : SymI(SymI), SymE(SymE), UncI(UncI), R(R) {
    read();
  }

  void moveNext() {
    ++SymI;
    if (Flags & (1 << storage::Symbol::FB_has_uncommon))
      ++UncI;
    read();
  }

  bool operator==(const SymbolRef &Other) const { return SymI == Other.SymI; }
};

inline Reader::symbol_range Reader::symbols() const {
  return {SymbolRef(Symbols.begin(), Symbols.end(), Uncommons.begin(), this),
          SymbolRef(Symbols.end(), Symbols.end(), nullptr, this)};
}

inline Reader::symbol_range Reader::module_symbols(unsigned I) const {
  const storage::Module &M = Modules[I];
  const storage::Symbol *MBegin = Symbols.begin() + M.Begin,
                        *MEnd = Symbols.begin() + M.End;
  return {SymbolRef(MBegin, MEnd, Uncommons.begin() + M.UncBegin, this),
          SymbolRef(MEnd, MEnd, nullptr, this)};
}

/// The contents of the irsymtab in a bitcode file. Any underlying data for the
/// irsymtab are owned by Symtab and Strtab.
struct FileContents {
  SmallVector<char, 0> Symtab, Strtab;
  Reader TheReader;
};

/// Reads the contents of a bitcode file, creating its irsymtab if necessary.
Expected<FileContents> readBitcode(const BitcodeFileContents &BFC);

} // end namespace irsymtab
} // end namespace llvm

#endif // LLVM_OBJECT_IRSYMTAB_H
