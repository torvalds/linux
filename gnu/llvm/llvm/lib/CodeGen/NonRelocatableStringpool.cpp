//===-- NonRelocatableStringpool.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/NonRelocatableStringpool.h"
#include "llvm/ADT/STLExtras.h"

namespace llvm {

DwarfStringPoolEntryRef NonRelocatableStringpool::getEntry(StringRef S) {
  auto I = Strings.insert({S, DwarfStringPoolEntry()});
  auto &Entry = I.first->second;
  if (I.second || !Entry.isIndexed()) {
    Entry.Index = NumEntries++;
    Entry.Offset = CurrentEndOffset;
    Entry.Symbol = nullptr;
    CurrentEndOffset += S.size() + 1;
  }
  return DwarfStringPoolEntryRef(*I.first);
}

StringRef NonRelocatableStringpool::internString(StringRef S) {
  DwarfStringPoolEntry Entry{nullptr, 0, DwarfStringPoolEntry::NotIndexed};

  auto InsertResult = Strings.insert({S, Entry});
  return InsertResult.first->getKey();
}

std::vector<DwarfStringPoolEntryRef>
NonRelocatableStringpool::getEntriesForEmission() const {
  std::vector<DwarfStringPoolEntryRef> Result;
  Result.reserve(Strings.size());
  for (const auto &E : Strings)
    if (E.getValue().isIndexed())
      Result.emplace_back(E);
  llvm::sort(Result, [](const DwarfStringPoolEntryRef A,
                        const DwarfStringPoolEntryRef B) {
    return A.getIndex() < B.getIndex();
  });
  return Result;
}

} // namespace llvm
