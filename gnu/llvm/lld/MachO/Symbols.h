//===- Symbols.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_SYMBOLS_H
#define LLD_MACHO_SYMBOLS_H

#include "Config.h"
#include "InputFiles.h"
#include "Target.h"

#include "llvm/Object/Archive.h"
#include "llvm/Support/MathExtras.h"

namespace lld {
namespace macho {

class MachHeaderSection;

struct StringRefZ {
  StringRefZ(const char *s) : data(s), size(-1) {}
  StringRefZ(StringRef s) : data(s.data()), size(s.size()) {}

  const char *data;
  const uint32_t size;
};

class Symbol {
public:
  enum Kind {
    DefinedKind,
    UndefinedKind,
    CommonKind,
    DylibKind,
    LazyArchiveKind,
    LazyObjectKind,
    AliasKind,
  };

  virtual ~Symbol() {}

  Kind kind() const { return symbolKind; }

  StringRef getName() const {
    if (nameSize == (uint32_t)-1)
      nameSize = strlen(nameData);
    return {nameData, nameSize};
  }

  bool isLive() const { return used; }
  bool isLazy() const {
    return symbolKind == LazyArchiveKind || symbolKind == LazyObjectKind;
  }

  virtual uint64_t getVA() const { return 0; }

  virtual bool isWeakDef() const { return false; }

  // Only undefined or dylib symbols can be weak references. A weak reference
  // need not be satisfied at runtime, e.g. due to the symbol not being
  // available on a given target platform.
  virtual bool isWeakRef() const { return false; }

  virtual bool isTlv() const { return false; }

  // Whether this symbol is in the GOT or TLVPointer sections.
  bool isInGot() const { return gotIndex != UINT32_MAX; }

  // Whether this symbol is in the StubsSection.
  bool isInStubs() const { return stubsIndex != UINT32_MAX; }

  uint64_t getStubVA() const;
  uint64_t getLazyPtrVA() const;
  uint64_t getGotVA() const;
  uint64_t getTlvVA() const;
  uint64_t resolveBranchVA() const {
    assert(isa<Defined>(this) || isa<DylibSymbol>(this));
    return isInStubs() ? getStubVA() : getVA();
  }
  uint64_t resolveGotVA() const { return isInGot() ? getGotVA() : getVA(); }
  uint64_t resolveTlvVA() const { return isInGot() ? getTlvVA() : getVA(); }

  // The index of this symbol in the GOT or the TLVPointer section, depending
  // on whether it is a thread-local. A given symbol cannot be referenced by
  // both these sections at once.
  uint32_t gotIndex = UINT32_MAX;
  uint32_t lazyBindOffset = UINT32_MAX;
  uint32_t stubsHelperIndex = UINT32_MAX;
  uint32_t stubsIndex = UINT32_MAX;
  uint32_t symtabIndex = UINT32_MAX;

  InputFile *getFile() const { return file; }

protected:
  Symbol(Kind k, StringRefZ name, InputFile *file)
      : symbolKind(k), nameData(name.data), file(file), nameSize(name.size),
        isUsedInRegularObj(!file || isa<ObjFile>(file)),
        used(!config->deadStrip) {}

  Kind symbolKind;
  const char *nameData;
  InputFile *file;
  mutable uint32_t nameSize;

public:
  // True if this symbol was referenced by a regular (non-bitcode) object.
  bool isUsedInRegularObj : 1;

  // True if this symbol is used from a live section.
  bool used : 1;
};

class Defined : public Symbol {
public:
  Defined(StringRefZ name, InputFile *file, InputSection *isec, uint64_t value,
          uint64_t size, bool isWeakDef, bool isExternal, bool isPrivateExtern,
          bool includeInSymtab, bool isReferencedDynamically, bool noDeadStrip,
          bool canOverrideWeakDef = false, bool isWeakDefCanBeHidden = false,
          bool interposable = false);

  bool isWeakDef() const override { return weakDef; }
  bool isExternalWeakDef() const {
    return isWeakDef() && isExternal() && !privateExtern;
  }
  bool isTlv() const override;

  bool isExternal() const { return external; }
  bool isAbsolute() const { return originalIsec == nullptr; }

  uint64_t getVA() const override;

  // Returns the object file that this symbol was defined in. This value differs
  // from `getFile()` if the symbol originated from a bitcode file.
  ObjFile *getObjectFile() const;

  std::string getSourceLocation();

  // Get the canonical InputSection of the symbol.
  InputSection *isec() const;

  // Get the canonical unwind entry of the symbol.
  ConcatInputSection *unwindEntry() const;

  static bool classof(const Symbol *s) { return s->kind() == DefinedKind; }

  // Place the bitfields first so that they can get placed in the tail padding
  // of the parent class, on platforms which support it.
  bool overridesWeakDef : 1;
  // Whether this symbol should appear in the output binary's export trie.
  bool privateExtern : 1;
  // Whether this symbol should appear in the output symbol table.
  bool includeInSymtab : 1;
  // Whether this symbol was folded into a different symbol during ICF.
  bool wasIdenticalCodeFolded : 1;
  // Symbols marked referencedDynamically won't be removed from the output's
  // symbol table by tools like strip. In theory, this could be set on arbitrary
  // symbols in input object files. In practice, it's used solely for the
  // synthetic __mh_execute_header symbol.
  // This is information for the static linker, and it's also written to the
  // output file's symbol table for tools running later (such as `strip`).
  bool referencedDynamically : 1;
  // Set on symbols that should not be removed by dead code stripping.
  // Set for example on `__attribute__((used))` globals, or on some Objective-C
  // metadata. This is information only for the static linker and not written
  // to the output.
  bool noDeadStrip : 1;
  // Whether references to this symbol can be interposed at runtime to point to
  // a different symbol definition (with the same name). For example, if both
  // dylib A and B define an interposable symbol _foo, and we load A before B at
  // runtime, then all references to _foo within dylib B will point to the
  // definition in dylib A.
  //
  // Only extern symbols may be interposable.
  bool interposable : 1;

  bool weakDefCanBeHidden : 1;

private:
  const bool weakDef : 1;
  const bool external : 1;

public:
  // The native InputSection of the symbol. The symbol may be moved to another
  // InputSection in which case originalIsec->canonical() will point to the new
  // InputSection
  InputSection *originalIsec;
  // Contains the offset from the containing subsection. Note that this is
  // different from nlist::n_value, which is the absolute address of the symbol.
  uint64_t value;
  // size is only calculated for regular (non-bitcode) symbols.
  uint64_t size;
  // This can be a subsection of either __compact_unwind or __eh_frame.
  ConcatInputSection *originalUnwindEntry = nullptr;
};

// This enum does double-duty: as a symbol property, it indicates whether & how
// a dylib symbol is referenced. As a DylibFile property, it indicates the kind
// of referenced symbols contained within the file. If there are both weak
// and strong references to the same file, we will count the file as
// strongly-referenced.
enum class RefState : uint8_t { Unreferenced = 0, Weak = 1, Strong = 2 };

class Undefined : public Symbol {
public:
  Undefined(StringRefZ name, InputFile *file, RefState refState,
            bool wasBitcodeSymbol)
      : Symbol(UndefinedKind, name, file), refState(refState),
        wasBitcodeSymbol(wasBitcodeSymbol) {
    assert(refState != RefState::Unreferenced);
  }

  bool isWeakRef() const override { return refState == RefState::Weak; }

  static bool classof(const Symbol *s) { return s->kind() == UndefinedKind; }

  RefState refState : 2;
  bool wasBitcodeSymbol;
};

// On Unix, it is traditionally allowed to write variable definitions without
// initialization expressions (such as "int foo;") to header files. These are
// called tentative definitions.
//
// Using tentative definitions is usually considered a bad practice; you should
// write only declarations (such as "extern int foo;") to header files.
// Nevertheless, the linker and the compiler have to do something to support
// bad code by allowing duplicate definitions for this particular case.
//
// The compiler creates common symbols when it sees tentative definitions.
// (You can suppress this behavior and let the compiler create a regular
// defined symbol by passing -fno-common. -fno-common is the default in clang
// as of LLVM 11.0.) When linking the final binary, if there are remaining
// common symbols after name resolution is complete, the linker converts them
// to regular defined symbols in a __common section.
class CommonSymbol : public Symbol {
public:
  CommonSymbol(StringRefZ name, InputFile *file, uint64_t size, uint32_t align,
               bool isPrivateExtern)
      : Symbol(CommonKind, name, file), size(size),
        align(align != 1 ? align : llvm::PowerOf2Ceil(size)),
        privateExtern(isPrivateExtern) {
    // TODO: cap maximum alignment
  }

  static bool classof(const Symbol *s) { return s->kind() == CommonKind; }

  const uint64_t size;
  const uint32_t align;
  const bool privateExtern;
};

class DylibSymbol : public Symbol {
public:
  DylibSymbol(DylibFile *file, StringRefZ name, bool isWeakDef,
              RefState refState, bool isTlv)
      : Symbol(DylibKind, name, file), shouldReexport(false),
        refState(refState), weakDef(isWeakDef), tlv(isTlv) {
    if (file && refState > RefState::Unreferenced)
      file->numReferencedSymbols++;
  }

  uint64_t getVA() const override;
  bool isWeakDef() const override { return weakDef; }

  // Symbols from weak libraries/frameworks are also weakly-referenced.
  bool isWeakRef() const override {
    return refState == RefState::Weak ||
           (file && getFile()->umbrella->forceWeakImport);
  }
  bool isReferenced() const { return refState != RefState::Unreferenced; }
  bool isTlv() const override { return tlv; }
  bool isDynamicLookup() const { return file == nullptr; }
  bool hasStubsHelper() const { return stubsHelperIndex != UINT32_MAX; }

  DylibFile *getFile() const {
    assert(!isDynamicLookup());
    return cast<DylibFile>(file);
  }

  static bool classof(const Symbol *s) { return s->kind() == DylibKind; }

  RefState getRefState() const { return refState; }

  void reference(RefState newState) {
    assert(newState > RefState::Unreferenced);
    if (refState == RefState::Unreferenced && file)
      getFile()->numReferencedSymbols++;
    refState = std::max(refState, newState);
  }

  void unreference() {
    // dynamic_lookup symbols have no file.
    if (refState > RefState::Unreferenced && file) {
      assert(getFile()->numReferencedSymbols > 0);
      getFile()->numReferencedSymbols--;
    }
  }

  bool shouldReexport : 1;
private:
  RefState refState : 2;
  const bool weakDef : 1;
  const bool tlv : 1;
};

class LazyArchive : public Symbol {
public:
  LazyArchive(ArchiveFile *file, const llvm::object::Archive::Symbol &sym)
      : Symbol(LazyArchiveKind, sym.getName(), file), sym(sym) {}

  ArchiveFile *getFile() const { return cast<ArchiveFile>(file); }
  void fetchArchiveMember();

  static bool classof(const Symbol *s) { return s->kind() == LazyArchiveKind; }

private:
  const llvm::object::Archive::Symbol sym;
};

// A defined symbol in an ObjFile/BitcodeFile surrounded by --start-lib and
// --end-lib.
class LazyObject : public Symbol {
public:
  LazyObject(InputFile &file, StringRef name)
      : Symbol(LazyObjectKind, name, &file) {
    isUsedInRegularObj = false;
  }

  static bool classof(const Symbol *s) { return s->kind() == LazyObjectKind; }
};

// Represents N_INDR symbols. Note that if we are given valid, linkable inputs,
// then all AliasSymbol instances will be converted into one of the other Symbol
// types after `createAliases()` runs.
class AliasSymbol final : public Symbol {
public:
  AliasSymbol(InputFile *file, StringRef name, StringRef aliasedName,
              bool isPrivateExtern)
      : Symbol(AliasKind, name, file), privateExtern(isPrivateExtern),
        aliasedName(aliasedName) {}

  StringRef getAliasedName() const { return aliasedName; }

  static bool classof(const Symbol *s) { return s->kind() == AliasKind; }

  const bool privateExtern;

private:
  StringRef aliasedName;
};

union SymbolUnion {
  alignas(Defined) char a[sizeof(Defined)];
  alignas(Undefined) char b[sizeof(Undefined)];
  alignas(CommonSymbol) char c[sizeof(CommonSymbol)];
  alignas(DylibSymbol) char d[sizeof(DylibSymbol)];
  alignas(LazyArchive) char e[sizeof(LazyArchive)];
  alignas(LazyObject) char f[sizeof(LazyObject)];
  alignas(AliasSymbol) char g[sizeof(AliasSymbol)];
};

template <typename T, typename... ArgT>
T *replaceSymbol(Symbol *s, ArgT &&...arg) {
  static_assert(sizeof(T) <= sizeof(SymbolUnion), "SymbolUnion too small");
  static_assert(alignof(T) <= alignof(SymbolUnion),
                "SymbolUnion not aligned enough");
  assert(static_cast<Symbol *>(static_cast<T *>(nullptr)) == nullptr &&
         "Not a Symbol");

  bool isUsedInRegularObj = s->isUsedInRegularObj;
  bool used = s->used;
  T *sym = new (s) T(std::forward<ArgT>(arg)...);
  sym->isUsedInRegularObj |= isUsedInRegularObj;
  sym->used |= used;
  return sym;
}

// Can a symbol's address only be resolved at runtime?
inline bool needsBinding(const Symbol *sym) {
  if (isa<DylibSymbol>(sym))
    return true;
  if (const auto *defined = dyn_cast<Defined>(sym))
    return defined->isExternalWeakDef() || defined->interposable;
  return false;
}

// Symbols with `l` or `L` as a prefix are linker-private and never appear in
// the output.
inline bool isPrivateLabel(StringRef name) {
  return name.starts_with("l") || name.starts_with("L");
}
} // namespace macho

std::string toString(const macho::Symbol &);
std::string toMachOString(const llvm::object::Archive::Symbol &);

} // namespace lld

#endif
