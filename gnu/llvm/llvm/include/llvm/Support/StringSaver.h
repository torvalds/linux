//===- llvm/Support/StringSaver.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_STRINGSAVER_H
#define LLVM_SUPPORT_STRINGSAVER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Allocator.h"

namespace llvm {

/// Saves strings in the provided stable storage and returns a
/// StringRef with a stable character pointer.
class StringSaver final {
  BumpPtrAllocator &Alloc;

public:
  StringSaver(BumpPtrAllocator &Alloc) : Alloc(Alloc) {}

  BumpPtrAllocator &getAllocator() const { return Alloc; }

  // All returned strings are null-terminated: *save(S).end() == 0.
  StringRef save(const char *S) { return save(StringRef(S)); }
  StringRef save(StringRef S);
  StringRef save(const Twine &S);
  StringRef save(const std::string &S) { return save(StringRef(S)); }
};

/// Saves strings in the provided stable storage and returns a StringRef with a
/// stable character pointer. Saving the same string yields the same StringRef.
///
/// Compared to StringSaver, it does more work but avoids saving the same string
/// multiple times.
///
/// Compared to StringPool, it performs fewer allocations but doesn't support
/// refcounting/deletion.
class UniqueStringSaver final {
  StringSaver Strings;
  llvm::DenseSet<llvm::StringRef> Unique;

public:
  UniqueStringSaver(BumpPtrAllocator &Alloc) : Strings(Alloc) {}

  // All returned strings are null-terminated: *save(S).end() == 0.
  StringRef save(const char *S) { return save(StringRef(S)); }
  StringRef save(StringRef S);
  StringRef save(const Twine &S);
  StringRef save(const std::string &S) { return save(StringRef(S)); }
};

} // namespace llvm
#endif
