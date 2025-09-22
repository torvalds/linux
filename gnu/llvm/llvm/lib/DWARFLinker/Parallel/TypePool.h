//===- TypePool.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_PARALLEL_TYPEPOOL_H
#define LLVM_DWARFLINKER_PARALLEL_TYPEPOOL_H

#include "ArrayList.h"
#include "llvm/ADT/ConcurrentHashtable.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/Support/Allocator.h"
#include <atomic>

namespace llvm {
namespace dwarf_linker {
namespace parallel {

class TypePool;
class CompileUnit;
class TypeEntryBody;

using TypeEntry = StringMapEntry<std::atomic<TypeEntryBody *>>;

/// Keeps cloned data for the type DIE.
class TypeEntryBody {
public:
  /// Returns copy of type DIE which should be emitted into resulting file.
  DIE &getFinalDie() const {
    if (Die)
      return *Die;

    assert(DeclarationDie);
    return *DeclarationDie;
  }

  /// Returns true if type die entry has only declaration die.
  bool hasOnlyDeclaration() const { return Die == nullptr; }

  /// Creates type DIE for the specified name.
  static TypeEntryBody *
  create(llvm::parallel::PerThreadBumpPtrAllocator &Allocator) {
    TypeEntryBody *Result = Allocator.Allocate<TypeEntryBody>();
    new (Result) TypeEntryBody(Allocator);
    return Result;
  }

  /// TypeEntryBody keeps partially cloned DIEs corresponding to this type.
  /// The two kinds of DIE can be kept: declaration and definition.
  /// If definition DIE was met while parsing input DWARF then this DIE would
  /// be used as a final DIE for this type. If definition DIE is not met then
  /// declaration DIE would be used as a final DIE.

  // Keeps definition die.
  std::atomic<DIE *> Die = {nullptr};

  // Keeps declaration die.
  std::atomic<DIE *> DeclarationDie = {nullptr};

  // True if parent type die is declaration.
  std::atomic<bool> ParentIsDeclaration = {true};

  /// Children for current type.
  ArrayList<TypeEntry *, 5> Children;

protected:
  TypeEntryBody() = delete;
  TypeEntryBody(const TypeEntryBody &RHS) = delete;
  TypeEntryBody(TypeEntryBody &&RHS) = delete;
  TypeEntryBody &operator=(const TypeEntryBody &RHS) = delete;
  TypeEntryBody &operator=(const TypeEntryBody &&RHS) = delete;

  TypeEntryBody(llvm::parallel::PerThreadBumpPtrAllocator &Allocator)
      : Children(&Allocator) {}
};

class TypeEntryInfo {
public:
  /// \returns Hash value for the specified \p Key.
  static inline uint64_t getHashValue(const StringRef &Key) {
    return xxh3_64bits(Key);
  }

  /// \returns true if both \p LHS and \p RHS are equal.
  static inline bool isEqual(const StringRef &LHS, const StringRef &RHS) {
    return LHS == RHS;
  }

  /// \returns key for the specified \p KeyData.
  static inline StringRef getKey(const TypeEntry &KeyData) {
    return KeyData.getKey();
  }

  /// \returns newly created object of KeyDataTy type.
  static inline TypeEntry *
  create(const StringRef &Key,
         llvm::parallel::PerThreadBumpPtrAllocator &Allocator) {
    return TypeEntry::create(Key, Allocator);
  }
};

/// TypePool keeps type descriptors which contain partially cloned DIE
/// correspinding to each type. Types are identified by names.
class TypePool
    : ConcurrentHashTableByPtr<StringRef, TypeEntry,
                               llvm::parallel::PerThreadBumpPtrAllocator,
                               TypeEntryInfo> {
public:
  TypePool()
      : ConcurrentHashTableByPtr<StringRef, TypeEntry,
                                 llvm::parallel::PerThreadBumpPtrAllocator,
                                 TypeEntryInfo>(Allocator) {
    Root = TypeEntry::create("", Allocator);
    Root->getValue().store(TypeEntryBody::create(Allocator));
  }

  TypeEntry *insert(StringRef Name) {
    return ConcurrentHashTableByPtr<StringRef, TypeEntry,
                                    llvm::parallel::PerThreadBumpPtrAllocator,
                                    TypeEntryInfo>::insert(Name)
        .first;
  }

  /// Create or return existing type entry body for the specified \p Entry.
  /// Link that entry as child for the specified \p ParentEntry.
  /// \returns The existing or created type entry body.
  TypeEntryBody *getOrCreateTypeEntryBody(TypeEntry *Entry,
                                          TypeEntry *ParentEntry) {
    TypeEntryBody *DIE = Entry->getValue().load();
    if (DIE)
      return DIE;

    TypeEntryBody *NewDIE = TypeEntryBody::create(Allocator);
    if (Entry->getValue().compare_exchange_weak(DIE, NewDIE)) {
      ParentEntry->getValue().load()->Children.add(Entry);
      return NewDIE;
    }

    return DIE;
  }

  /// Sort children for each kept type entry.
  void sortTypes() {
    std::function<void(TypeEntry * Entry)> SortChildrenRec =
        [&](TypeEntry *Entry) {
          Entry->getValue().load()->Children.sort(TypesComparator);
          Entry->getValue().load()->Children.forEach(SortChildrenRec);
        };

    SortChildrenRec(getRoot());
  }

  /// Return root for all type entries.
  TypeEntry *getRoot() const { return Root; }

  /// Return thread local allocator used by pool.
  BumpPtrAllocator &getThreadLocalAllocator() {
    return Allocator.getThreadLocalAllocator();
  }

protected:
  std::function<bool(const TypeEntry *LHS, const TypeEntry *RHS)>
      TypesComparator = [](const TypeEntry *LHS, const TypeEntry *RHS) -> bool {
    return LHS->getKey() < RHS->getKey();
  };

  // Root of all type entries.
  TypeEntry *Root = nullptr;

private:
  llvm::parallel::PerThreadBumpPtrAllocator Allocator;
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_PARALLEL_TYPEPOOL_H
