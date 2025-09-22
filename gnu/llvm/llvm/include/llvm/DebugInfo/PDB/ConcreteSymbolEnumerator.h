//===- ConcreteSymbolEnumerator.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_CONCRETESYMBOLENUMERATOR_H
#define LLVM_DEBUGINFO_PDB_CONCRETESYMBOLENUMERATOR_H

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Casting.h"
#include <algorithm>
#include <cstdint>
#include <memory>

namespace llvm {
namespace pdb {

template <typename ChildType>
class ConcreteSymbolEnumerator : public IPDBEnumChildren<ChildType> {
public:
  ConcreteSymbolEnumerator(std::unique_ptr<IPDBEnumSymbols> SymbolEnumerator)
      : Enumerator(std::move(SymbolEnumerator)) {}

  ~ConcreteSymbolEnumerator() override = default;

  uint32_t getChildCount() const override {
    return Enumerator->getChildCount();
  }

  std::unique_ptr<ChildType> getChildAtIndex(uint32_t Index) const override {
    std::unique_ptr<PDBSymbol> Child = Enumerator->getChildAtIndex(Index);
    return unique_dyn_cast_or_null<ChildType>(Child);
  }

  std::unique_ptr<ChildType> getNext() override {
    return unique_dyn_cast_or_null<ChildType>(Enumerator->getNext());
  }

  void reset() override { Enumerator->reset(); }

private:

  std::unique_ptr<IPDBEnumSymbols> Enumerator;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_CONCRETESYMBOLENUMERATOR_H
