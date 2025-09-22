//==- NativeEnumModules.cpp - Native Symbol Enumerator impl ------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeEnumModules.h"

#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"

namespace llvm {
namespace pdb {

NativeEnumModules::NativeEnumModules(NativeSession &PDBSession, uint32_t Index)
    : Session(PDBSession), Index(Index) {}

uint32_t NativeEnumModules::getChildCount() const {
  return Session.getSymbolCache().getNumCompilands();
}

std::unique_ptr<PDBSymbol>
NativeEnumModules::getChildAtIndex(uint32_t N) const {
  return Session.getSymbolCache().getOrCreateCompiland(N);
}

std::unique_ptr<PDBSymbol> NativeEnumModules::getNext() {
  if (Index >= getChildCount())
    return nullptr;
  return getChildAtIndex(Index++);
}

void NativeEnumModules::reset() { Index = 0; }

}
}
