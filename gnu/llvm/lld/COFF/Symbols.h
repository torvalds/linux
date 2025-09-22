//===- Symbols.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_SYMBOLS_H
#define LLD_COFF_SYMBOLS_H

#include "Chunks.h"
#include "Config.h"
#include "lld/Common/LLVM.h"
#include "lld/Common/Memory.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include <atomic>
#include <memory>
#include <vector>

namespace lld {

namespace coff {

using llvm::object::Archive;
using llvm::object::COFFSymbolRef;
using llvm::object::coff_import_header;
using llvm::object::coff_symbol_generic;

class ArchiveFile;
class COFFLinkerContext;
class InputFile;
class ObjFile;
class SymbolTable;

// The base class for real symbol classes.
class Symbol {
public:
  enum Kind {
    // The order of these is significant. We start with the regular defined
    // symbols as those are the most prevalent and the zero tag is the cheapest
    // to set. Among the defined kinds, the lower the kind is preferred over
    // the higher kind when testing whether one symbol should take precedence
    // over another.
    DefinedRegularKind = 0,
    DefinedCommonKind,
    DefinedLocalImportKind,
    DefinedImportThunkKind,
    DefinedImportDataKind,
    DefinedAbsoluteKind,
    DefinedSyntheticKind,

    UndefinedKind,
    LazyArchiveKind,
    LazyObjectKind,
    LazyDLLSymbolKind,

    LastDefinedCOFFKind = DefinedCommonKind,
    LastDefinedKind = DefinedSyntheticKind,
  };

  Kind kind() const { return static_cast<Kind>(symbolKind); }

  // Returns the symbol name.
  StringRef getName() {
    // COFF symbol names are read lazily for a performance reason.
    // Non-external symbol names are never used by the linker except for logging
    // or debugging. Their internal references are resolved not by name but by
    // symbol index. And because they are not external, no one can refer them by
    // name. Object files contain lots of non-external symbols, and creating
    // StringRefs for them (which involves lots of strlen() on the string table)
    // is a waste of time.
    if (nameData == nullptr)
      computeName();
    return StringRef(nameData, nameSize);
  }

  void replaceKeepingName(Symbol *other, size_t size);

  // Returns the file from which this symbol was created.
  InputFile *getFile();

  // Indicates that this symbol will be included in the final image. Only valid
  // after calling markLive.
  bool isLive() const;

  bool isLazy() const {
    return symbolKind == LazyArchiveKind || symbolKind == LazyObjectKind ||
           symbolKind == LazyDLLSymbolKind;
  }

private:
  void computeName();

protected:
  friend SymbolTable;
  explicit Symbol(Kind k, StringRef n = "")
      : symbolKind(k), isExternal(true), isCOMDAT(false),
        writtenToSymtab(false), isUsedInRegularObj(false),
        pendingArchiveLoad(false), isGCRoot(false), isRuntimePseudoReloc(false),
        deferUndefined(false), canInline(true), isWeak(false),
        nameSize(n.size()), nameData(n.empty() ? nullptr : n.data()) {
    assert((!n.empty() || k <= LastDefinedCOFFKind) &&
           "If the name is empty, the Symbol must be a DefinedCOFF.");
  }

  unsigned symbolKind : 8;
  unsigned isExternal : 1;

public:
  // This bit is used by the \c DefinedRegular subclass.
  unsigned isCOMDAT : 1;

  // This bit is used by Writer::createSymbolAndStringTable() to prevent
  // symbols from being written to the symbol table more than once.
  unsigned writtenToSymtab : 1;

  // True if this symbol was referenced by a regular (non-bitcode) object.
  unsigned isUsedInRegularObj : 1;

  // True if we've seen both a lazy and an undefined symbol with this symbol
  // name, which means that we have enqueued an archive member load and should
  // not load any more archive members to resolve the same symbol.
  unsigned pendingArchiveLoad : 1;

  /// True if we've already added this symbol to the list of GC roots.
  unsigned isGCRoot : 1;

  unsigned isRuntimePseudoReloc : 1;

  // True if we want to allow this symbol to be undefined in the early
  // undefined check pass in SymbolTable::reportUnresolvable(), as it
  // might be fixed up later.
  unsigned deferUndefined : 1;

  // False if LTO shouldn't inline whatever this symbol points to. If a symbol
  // is overwritten after LTO, LTO shouldn't inline the symbol because it
  // doesn't know the final contents of the symbol.
  unsigned canInline : 1;

  // True if the symbol is weak. This is only tracked for bitcode/LTO symbols.
  // This information isn't written to the output; rather, it's used for
  // managing weak symbol overrides.
  unsigned isWeak : 1;

protected:
  // Symbol name length. Assume symbol lengths fit in a 32-bit integer.
  uint32_t nameSize;

  const char *nameData;
};

// The base class for any defined symbols, including absolute symbols,
// etc.
class Defined : public Symbol {
public:
  Defined(Kind k, StringRef n) : Symbol(k, n) {}

  static bool classof(const Symbol *s) { return s->kind() <= LastDefinedKind; }

  // Returns the RVA (relative virtual address) of this symbol. The
  // writer sets and uses RVAs.
  uint64_t getRVA();

  // Returns the chunk containing this symbol. Absolute symbols and __ImageBase
  // do not have chunks, so this may return null.
  Chunk *getChunk();
};

// Symbols defined via a COFF object file or bitcode file.  For COFF files, this
// stores a coff_symbol_generic*, and names of internal symbols are lazily
// loaded through that. For bitcode files, Sym is nullptr and the name is stored
// as a decomposed StringRef.
class DefinedCOFF : public Defined {
  friend Symbol;

public:
  DefinedCOFF(Kind k, InputFile *f, StringRef n, const coff_symbol_generic *s)
      : Defined(k, n), file(f), sym(s) {}

  static bool classof(const Symbol *s) {
    return s->kind() <= LastDefinedCOFFKind;
  }

  InputFile *getFile() { return file; }

  COFFSymbolRef getCOFFSymbol();

  InputFile *file;

protected:
  const coff_symbol_generic *sym;
};

// Regular defined symbols read from object file symbol tables.
class DefinedRegular : public DefinedCOFF {
public:
  DefinedRegular(InputFile *f, StringRef n, bool isCOMDAT,
                 bool isExternal = false,
                 const coff_symbol_generic *s = nullptr,
                 SectionChunk *c = nullptr, bool isWeak = false)
      : DefinedCOFF(DefinedRegularKind, f, n, s), data(c ? &c->repl : nullptr) {
    this->isExternal = isExternal;
    this->isCOMDAT = isCOMDAT;
    this->isWeak = isWeak;
  }

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedRegularKind;
  }

  uint64_t getRVA() const { return (*data)->getRVA() + sym->Value; }
  SectionChunk *getChunk() const { return *data; }
  uint32_t getValue() const { return sym->Value; }

  SectionChunk **data;
};

class DefinedCommon : public DefinedCOFF {
public:
  DefinedCommon(InputFile *f, StringRef n, uint64_t size,
                const coff_symbol_generic *s = nullptr,
                CommonChunk *c = nullptr)
      : DefinedCOFF(DefinedCommonKind, f, n, s), data(c), size(size) {
    this->isExternal = true;
  }

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedCommonKind;
  }

  uint64_t getRVA() { return data->getRVA(); }
  CommonChunk *getChunk() { return data; }

private:
  friend SymbolTable;
  uint64_t getSize() const { return size; }
  CommonChunk *data;
  uint64_t size;
};

// Absolute symbols.
class DefinedAbsolute : public Defined {
public:
  DefinedAbsolute(const COFFLinkerContext &c, StringRef n, COFFSymbolRef s)
      : Defined(DefinedAbsoluteKind, n), va(s.getValue()), ctx(c) {
    isExternal = s.isExternal();
  }

  DefinedAbsolute(const COFFLinkerContext &c, StringRef n, uint64_t v)
      : Defined(DefinedAbsoluteKind, n), va(v), ctx(c) {}

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedAbsoluteKind;
  }

  uint64_t getRVA();
  void setVA(uint64_t v) { va = v; }
  uint64_t getVA() const { return va; }

private:
  uint64_t va;
  const COFFLinkerContext &ctx;
};

// This symbol is used for linker-synthesized symbols like __ImageBase and
// __safe_se_handler_table.
class DefinedSynthetic : public Defined {
public:
  explicit DefinedSynthetic(StringRef name, Chunk *c, uint32_t offset = 0)
      : Defined(DefinedSyntheticKind, name), c(c), offset(offset) {}

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedSyntheticKind;
  }

  // A null chunk indicates that this is __ImageBase. Otherwise, this is some
  // other synthesized chunk, like SEHTableChunk.
  uint32_t getRVA() { return c ? c->getRVA() + offset : 0; }
  Chunk *getChunk() { return c; }

private:
  Chunk *c;
  uint32_t offset;
};

// This class represents a symbol defined in an archive file. It is
// created from an archive file header, and it knows how to load an
// object file from an archive to replace itself with a defined
// symbol. If the resolver finds both Undefined and LazyArchive for
// the same name, it will ask the LazyArchive to load a file.
class LazyArchive : public Symbol {
public:
  LazyArchive(ArchiveFile *f, const Archive::Symbol s)
      : Symbol(LazyArchiveKind, s.getName()), file(f), sym(s) {}

  static bool classof(const Symbol *s) { return s->kind() == LazyArchiveKind; }

  MemoryBufferRef getMemberBuffer();

  ArchiveFile *file;
  const Archive::Symbol sym;
};

class LazyObject : public Symbol {
public:
  LazyObject(InputFile *f, StringRef n) : Symbol(LazyObjectKind, n), file(f) {}
  static bool classof(const Symbol *s) { return s->kind() == LazyObjectKind; }
  InputFile *file;
};

// MinGW only.
class LazyDLLSymbol : public Symbol {
public:
  LazyDLLSymbol(DLLFile *f, DLLFile::Symbol *s, StringRef n)
      : Symbol(LazyDLLSymbolKind, n), file(f), sym(s) {}
  static bool classof(const Symbol *s) {
    return s->kind() == LazyDLLSymbolKind;
  }

  DLLFile *file;
  DLLFile::Symbol *sym;
};

// Undefined symbols.
class Undefined : public Symbol {
public:
  explicit Undefined(StringRef n) : Symbol(UndefinedKind, n) {}

  static bool classof(const Symbol *s) { return s->kind() == UndefinedKind; }

  // An undefined symbol can have a fallback symbol which gives an
  // undefined symbol a second chance if it would remain undefined.
  // If it remains undefined, it'll be replaced with whatever the
  // Alias pointer points to.
  Symbol *weakAlias = nullptr;

  // If this symbol is external weak, try to resolve it to a defined
  // symbol by searching the chain of fallback symbols. Returns the symbol if
  // successful, otherwise returns null.
  Defined *getWeakAlias();
};

// Windows-specific classes.

// This class represents a symbol imported from a DLL. This has two
// names for internal use and external use. The former is used for
// name resolution, and the latter is used for the import descriptor
// table in an output. The former has "__imp_" prefix.
class DefinedImportData : public Defined {
public:
  DefinedImportData(StringRef n, ImportFile *f)
      : Defined(DefinedImportDataKind, n), file(f) {
  }

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedImportDataKind;
  }

  uint64_t getRVA() { return file->location->getRVA(); }
  Chunk *getChunk() { return file->location; }
  void setLocation(Chunk *addressTable) { file->location = addressTable; }

  StringRef getDLLName() { return file->dllName; }
  StringRef getExternalName() { return file->externalName; }
  uint16_t getOrdinal() { return file->hdr->OrdinalHint; }

  ImportFile *file;

  // This is a pointer to the synthetic symbol associated with the load thunk
  // for this symbol that will be called if the DLL is delay-loaded. This is
  // needed for Control Flow Guard because if this DefinedImportData symbol is a
  // valid call target, the corresponding load thunk must also be marked as a
  // valid call target.
  DefinedSynthetic *loadThunkSym = nullptr;
};

// This class represents a symbol for a jump table entry which jumps
// to a function in a DLL. Linker are supposed to create such symbols
// without "__imp_" prefix for all function symbols exported from
// DLLs, so that you can call DLL functions as regular functions with
// a regular name. A function pointer is given as a DefinedImportData.
class DefinedImportThunk : public Defined {
public:
  DefinedImportThunk(COFFLinkerContext &ctx, StringRef name,
                     DefinedImportData *s, uint16_t machine);

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedImportThunkKind;
  }

  uint64_t getRVA() { return data->getRVA(); }
  Chunk *getChunk() { return data; }

  DefinedImportData *wrappedSym;

private:
  Chunk *data;
};

// If you have a symbol "foo" in your object file, a symbol name
// "__imp_foo" becomes automatically available as a pointer to "foo".
// This class is for such automatically-created symbols.
// Yes, this is an odd feature. We didn't intend to implement that.
// This is here just for compatibility with MSVC.
class DefinedLocalImport : public Defined {
public:
  DefinedLocalImport(COFFLinkerContext &ctx, StringRef n, Defined *s)
      : Defined(DefinedLocalImportKind, n),
        data(make<LocalImportChunk>(ctx, s)) {}

  static bool classof(const Symbol *s) {
    return s->kind() == DefinedLocalImportKind;
  }

  uint64_t getRVA() { return data->getRVA(); }
  Chunk *getChunk() { return data; }

private:
  LocalImportChunk *data;
};

inline uint64_t Defined::getRVA() {
  switch (kind()) {
  case DefinedAbsoluteKind:
    return cast<DefinedAbsolute>(this)->getRVA();
  case DefinedSyntheticKind:
    return cast<DefinedSynthetic>(this)->getRVA();
  case DefinedImportDataKind:
    return cast<DefinedImportData>(this)->getRVA();
  case DefinedImportThunkKind:
    return cast<DefinedImportThunk>(this)->getRVA();
  case DefinedLocalImportKind:
    return cast<DefinedLocalImport>(this)->getRVA();
  case DefinedCommonKind:
    return cast<DefinedCommon>(this)->getRVA();
  case DefinedRegularKind:
    return cast<DefinedRegular>(this)->getRVA();
  case LazyArchiveKind:
  case LazyObjectKind:
  case LazyDLLSymbolKind:
  case UndefinedKind:
    llvm_unreachable("Cannot get the address for an undefined symbol.");
  }
  llvm_unreachable("unknown symbol kind");
}

inline Chunk *Defined::getChunk() {
  switch (kind()) {
  case DefinedRegularKind:
    return cast<DefinedRegular>(this)->getChunk();
  case DefinedAbsoluteKind:
    return nullptr;
  case DefinedSyntheticKind:
    return cast<DefinedSynthetic>(this)->getChunk();
  case DefinedImportDataKind:
    return cast<DefinedImportData>(this)->getChunk();
  case DefinedImportThunkKind:
    return cast<DefinedImportThunk>(this)->getChunk();
  case DefinedLocalImportKind:
    return cast<DefinedLocalImport>(this)->getChunk();
  case DefinedCommonKind:
    return cast<DefinedCommon>(this)->getChunk();
  case LazyArchiveKind:
  case LazyObjectKind:
  case LazyDLLSymbolKind:
  case UndefinedKind:
    llvm_unreachable("Cannot get the chunk of an undefined symbol.");
  }
  llvm_unreachable("unknown symbol kind");
}

// A buffer class that is large enough to hold any Symbol-derived
// object. We allocate memory using this class and instantiate a symbol
// using the placement new.
union SymbolUnion {
  alignas(DefinedRegular) char a[sizeof(DefinedRegular)];
  alignas(DefinedCommon) char b[sizeof(DefinedCommon)];
  alignas(DefinedAbsolute) char c[sizeof(DefinedAbsolute)];
  alignas(DefinedSynthetic) char d[sizeof(DefinedSynthetic)];
  alignas(LazyArchive) char e[sizeof(LazyArchive)];
  alignas(Undefined) char f[sizeof(Undefined)];
  alignas(DefinedImportData) char g[sizeof(DefinedImportData)];
  alignas(DefinedImportThunk) char h[sizeof(DefinedImportThunk)];
  alignas(DefinedLocalImport) char i[sizeof(DefinedLocalImport)];
  alignas(LazyObject) char j[sizeof(LazyObject)];
  alignas(LazyDLLSymbol) char k[sizeof(LazyDLLSymbol)];
};

template <typename T, typename... ArgT>
void replaceSymbol(Symbol *s, ArgT &&... arg) {
  static_assert(std::is_trivially_destructible<T>(),
                "Symbol types must be trivially destructible");
  static_assert(sizeof(T) <= sizeof(SymbolUnion), "Symbol too small");
  static_assert(alignof(T) <= alignof(SymbolUnion),
                "SymbolUnion not aligned enough");
  assert(static_cast<Symbol *>(static_cast<T *>(nullptr)) == nullptr &&
         "Not a Symbol");
  bool canInline = s->canInline;
  bool isUsedInRegularObj = s->isUsedInRegularObj;
  new (s) T(std::forward<ArgT>(arg)...);
  s->canInline = canInline;
  s->isUsedInRegularObj = isUsedInRegularObj;
}
} // namespace coff

std::string toString(const coff::COFFLinkerContext &ctx, coff::Symbol &b);
std::string toCOFFString(const coff::COFFLinkerContext &ctx,
                         const llvm::object::Archive::Symbol &b);

} // namespace lld

#endif
