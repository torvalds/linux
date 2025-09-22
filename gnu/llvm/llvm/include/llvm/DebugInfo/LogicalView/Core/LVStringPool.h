//===-- LVStringPool.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LVStringPool class, which is used to implement a
// basic string pool table.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSTRINGPOOL_H
#define LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSTRINGPOOL_H

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <iomanip>
#include <vector>

namespace llvm {
namespace logicalview {

class LVStringPool {
  static constexpr size_t BadIndex = std::numeric_limits<size_t>::max();
  using TableType = StringMap<size_t, BumpPtrAllocator>;
  using ValueType = TableType::value_type;
  BumpPtrAllocator Allocator;
  TableType StringTable;
  std::vector<ValueType *> Entries;

public:
  LVStringPool() { getIndex(""); }
  LVStringPool(LVStringPool const &other) = delete;
  LVStringPool(LVStringPool &&other) = delete;
  ~LVStringPool() = default;

  bool isValidIndex(size_t Index) const { return Index != BadIndex; }

  // Return number of strings in the pool. The empty string is allocated
  // at the slot zero. We substract 1 to indicate the number of non empty
  // strings.
  size_t getSize() const { return Entries.size() - 1; }

  // Return the index for the specified key, otherwise 'BadIndex'.
  size_t findIndex(StringRef Key) const {
    TableType::const_iterator Iter = StringTable.find(Key);
    if (Iter != StringTable.end())
      return Iter->second;
    return BadIndex;
  }

  // Return an index for the specified key.
  size_t getIndex(StringRef Key) {
    size_t Index = findIndex(Key);
    if (isValidIndex(Index))
      return Index;
    size_t Value = Entries.size();
    ValueType *Entry = ValueType::create(Key, Allocator, Value);
    StringTable.insert(Entry);
    Entries.push_back(Entry);
    return Value;
  }

  // Given the index, return its corresponding string.
  StringRef getString(size_t Index) const {
    return (Index >= Entries.size()) ? StringRef() : Entries[Index]->getKey();
  }

  void print(raw_ostream &OS) const {
    if (!Entries.empty()) {
      OS << "\nString Pool:\n";
      for (const ValueType *Entry : Entries)
        OS << "Index: " << Entry->getValue() << ", "
           << "Key: '" << Entry->getKey() << "'\n";
    }
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const { print(dbgs()); }
#endif
};

} // namespace logicalview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_CORE_LVSTRINGPOOL_H
