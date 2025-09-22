//===- StringSet.h - An efficient set built on StringMap --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///  StringSet - A set-like wrapper for the StringMap.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STRINGSET_H
#define LLVM_ADT_STRINGSET_H

#include "llvm/ADT/StringMap.h"

namespace llvm {

/// StringSet - A wrapper for StringMap that provides set-like functionality.
template <class AllocatorTy = MallocAllocator>
class StringSet : public StringMap<std::nullopt_t, AllocatorTy> {
  using Base = StringMap<std::nullopt_t, AllocatorTy>;

public:
  StringSet() = default;
  StringSet(std::initializer_list<StringRef> initializer) {
    for (StringRef str : initializer)
      insert(str);
  }
  template <typename Container> explicit StringSet(Container &&C) {
    for (auto &&Str : C)
      insert(Str);
  }
  explicit StringSet(AllocatorTy a) : Base(a) {}

  std::pair<typename Base::iterator, bool> insert(StringRef key) {
    return Base::try_emplace(key);
  }

  template <typename InputIt>
  void insert(InputIt begin, InputIt end) {
    for (auto it = begin; it != end; ++it)
      insert(*it);
  }

  template <typename ValueTy>
  std::pair<typename Base::iterator, bool>
  insert(const StringMapEntry<ValueTy> &mapEntry) {
    return insert(mapEntry.getKey());
  }

  /// Check if the set contains the given \c key.
  bool contains(StringRef key) const { return Base::FindKey(key) != -1; }
};

} // end namespace llvm

#endif // LLVM_ADT_STRINGSET_H
