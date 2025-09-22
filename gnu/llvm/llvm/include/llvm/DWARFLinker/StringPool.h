//===- StringPool.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_STRINGPOOL_H
#define LLVM_DWARFLINKER_STRINGPOOL_H

#include "llvm/ADT/ConcurrentHashtable.h"
#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/PerThreadBumpPtrAllocator.h"
#include <string_view>

namespace llvm {
namespace dwarf_linker {

/// StringEntry keeps data of the string: the length, external offset
/// and a string body which is placed right after StringEntry.
using StringEntry = StringMapEntry<std::nullopt_t>;

class StringPoolEntryInfo {
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
  static inline StringRef getKey(const StringEntry &KeyData) {
    return KeyData.getKey();
  }

  /// \returns newly created object of KeyDataTy type.
  static inline StringEntry *
  create(const StringRef &Key,
         llvm::parallel::PerThreadBumpPtrAllocator &Allocator) {
    return StringEntry::create(Key, Allocator);
  }
};

class StringPool
    : public ConcurrentHashTableByPtr<StringRef, StringEntry,
                                      llvm::parallel::PerThreadBumpPtrAllocator,
                                      StringPoolEntryInfo> {
public:
  StringPool()
      : ConcurrentHashTableByPtr<StringRef, StringEntry,
                                 llvm::parallel::PerThreadBumpPtrAllocator,
                                 StringPoolEntryInfo>(Allocator) {}

  StringPool(size_t InitialSize)
      : ConcurrentHashTableByPtr<StringRef, StringEntry,
                                 llvm::parallel::PerThreadBumpPtrAllocator,
                                 StringPoolEntryInfo>(Allocator, InitialSize) {}

  llvm::parallel::PerThreadBumpPtrAllocator &getAllocatorRef() {
    return Allocator;
  }

  void clear() { Allocator.Reset(); }

private:
  llvm::parallel::PerThreadBumpPtrAllocator Allocator;
};

} // namespace dwarf_linker
} // end namespace llvm

#endif // LLVM_DWARFLINKER_STRINGPOOL_H
