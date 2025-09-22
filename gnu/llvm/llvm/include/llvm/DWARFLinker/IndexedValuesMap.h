//===- IndexedValuesMap.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_INDEXEDVALUESMAP_H
#define LLVM_DWARFLINKER_INDEXEDVALUESMAP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>
#include <utility>

namespace llvm {
namespace dwarf_linker {

/// This class stores values sequentually and assigns index to the each value.
template <typename T> class IndexedValuesMap {
public:
  uint64_t getValueIndex(T Value) {
    typename ValueToIndexMapTy::iterator It = ValueToIndexMap.find(Value);
    if (It == ValueToIndexMap.end()) {
      It = ValueToIndexMap.insert(std::make_pair(Value, Values.size())).first;
      Values.push_back(Value);
    }
    return It->second;
  }

  const SmallVector<T> &getValues() const { return Values; }

  void clear() {
    ValueToIndexMap.clear();
    Values.clear();
  }

  bool empty() { return Values.empty(); }

protected:
  using ValueToIndexMapTy = DenseMap<T, uint64_t>;
  ValueToIndexMapTy ValueToIndexMap;
  SmallVector<T> Values;
};

} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_INDEXEDVALUESMAP_H
