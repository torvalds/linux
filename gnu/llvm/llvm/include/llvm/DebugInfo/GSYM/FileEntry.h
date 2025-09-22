//===- FileEntry.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_FILEENTRY_H
#define LLVM_DEBUGINFO_GSYM_FILEENTRY_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include <functional>
#include <stdint.h>

namespace llvm {
namespace gsym {

/// Files in GSYM are contained in FileEntry structs where we split the
/// directory and basename into two different strings in the string
/// table. This allows paths to shared commont directory and filename
/// strings and saves space.
struct FileEntry {

  /// Offsets in the string table.
  /// @{
  uint32_t Dir = 0;
  uint32_t Base = 0;
  /// @}

  FileEntry() = default;
  FileEntry(uint32_t D, uint32_t B) : Dir(D), Base(B) {}

  // Implement operator== so that FileEntry can be used as key in
  // unordered containers.
  bool operator==(const FileEntry &RHS) const {
    return Base == RHS.Base && Dir == RHS.Dir;
  };
  bool operator!=(const FileEntry &RHS) const {
    return Base != RHS.Base || Dir != RHS.Dir;
  };
};

} // namespace gsym

template <> struct DenseMapInfo<gsym::FileEntry> {
  static inline gsym::FileEntry getEmptyKey() {
    uint32_t key = DenseMapInfo<uint32_t>::getEmptyKey();
    return gsym::FileEntry(key, key);
  }
  static inline gsym::FileEntry getTombstoneKey() {
    uint32_t key = DenseMapInfo<uint32_t>::getTombstoneKey();
    return gsym::FileEntry(key, key);
  }
  static unsigned getHashValue(const gsym::FileEntry &Val) {
    return llvm::hash_combine(DenseMapInfo<uint32_t>::getHashValue(Val.Dir),
                              DenseMapInfo<uint32_t>::getHashValue(Val.Base));
  }
  static bool isEqual(const gsym::FileEntry &LHS, const gsym::FileEntry &RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm
#endif // LLVM_DEBUGINFO_GSYM_FILEENTRY_H
