//===- NonRelocatableStringpool.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_NONRELOCATABLESTRINGPOOL_H
#define LLVM_CODEGEN_NONRELOCATABLESTRINGPOOL_H

#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/Support/Allocator.h"
#include <cstdint>
#include <vector>

namespace llvm {

/// A string table that doesn't need relocations.
///
/// Use this class when a string table doesn't need relocations.
/// This class provides this ability by just associating offsets with strings.
class NonRelocatableStringpool {
public:
  /// Entries are stored into the StringMap and simply linked together through
  /// the second element of this pair in order to keep track of insertion
  /// order.
  using MapTy = StringMap<DwarfStringPoolEntry, BumpPtrAllocator>;

  NonRelocatableStringpool(bool PutEmptyString = false) {
    if (PutEmptyString)
      getEntry("");
  }

  DwarfStringPoolEntryRef getEntry(StringRef S);

  /// Get the offset of string \p S in the string table. This can insert a new
  /// element or return the offset of a pre-existing one.
  uint64_t getStringOffset(StringRef S) { return getEntry(S).getOffset(); }

  /// Get permanent storage for \p S (but do not necessarily emit \p S in the
  /// output section). A latter call to getStringOffset() with the same string
  /// will chain it though.
  ///
  /// \returns The StringRef that points to permanent storage to use
  /// in place of \p S.
  StringRef internString(StringRef S);

  uint64_t getSize() { return CurrentEndOffset; }

  /// Return the list of strings to be emitted. This does not contain the
  /// strings which were added via internString only.
  std::vector<DwarfStringPoolEntryRef> getEntriesForEmission() const;

private:
  MapTy Strings;
  uint64_t CurrentEndOffset = 0;
  unsigned NumEntries = 0;
};

/// Helper for making strong types.
template <typename T, typename S> class StrongType : public T {
public:
  template <typename... Args>
  explicit StrongType(Args... A) : T(std::forward<Args>(A)...) {}
};

/// It's very easy to introduce bugs by passing the wrong string pool.
/// By using strong types the interface enforces that the right
/// kind of pool is used.
struct UniqueTag {};
struct OffsetsTag {};
using UniquingStringPool = StrongType<NonRelocatableStringpool, UniqueTag>;
using OffsetsStringPool = StrongType<NonRelocatableStringpool, OffsetsTag>;

} // end namespace llvm

#endif // LLVM_CODEGEN_NONRELOCATABLESTRINGPOOL_H
