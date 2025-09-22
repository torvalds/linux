//==- NativeEnumSymbols.cpp - Native Symbol Enumerator impl ------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeEnumSymbols.h"

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeEnumSymbols::NativeEnumSymbols(NativeSession &PDBSession,
                                     std::vector<SymIndexId> Symbols)
    : Symbols(std::move(Symbols)), Index(0), Session(PDBSession) {}

uint32_t NativeEnumSymbols::getChildCount() const {
  return static_cast<uint32_t>(Symbols.size());
}

std::unique_ptr<PDBSymbol>
NativeEnumSymbols::getChildAtIndex(uint32_t N) const {
  if (N < Symbols.size()) {
    return Session.getSymbolCache().getSymbolById(Symbols[N]);
  }
  return nullptr;
}

std::unique_ptr<PDBSymbol> NativeEnumSymbols::getNext() {
  return getChildAtIndex(Index++);
}

void NativeEnumSymbols::reset() { Index = 0; }
