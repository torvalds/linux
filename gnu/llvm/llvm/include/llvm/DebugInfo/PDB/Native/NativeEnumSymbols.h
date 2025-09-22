//==- NativeEnumSymbols.h - Native Symbols Enumerator impl -------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMSYMBOLS_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEENUMSYMBOLS_H

#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include <vector>

namespace llvm {
namespace pdb {

class NativeSession;

class NativeEnumSymbols : public IPDBEnumChildren<PDBSymbol> {
public:
  NativeEnumSymbols(NativeSession &Session, std::vector<SymIndexId> Symbols);

  uint32_t getChildCount() const override;
  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override;
  std::unique_ptr<PDBSymbol> getNext() override;
  void reset() override;

private:
  std::vector<SymIndexId> Symbols;
  uint32_t Index;
  NativeSession &Session;
};

} // namespace pdb
} // namespace llvm

#endif
