//===-- llvm/MC/MCSymbolGOFF.h - GOFF Machine Code Symbols ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the MCSymbolGOFF class
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLGOFF_H
#define LLVM_MC_MCSYMBOLGOFF_H

#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"

namespace llvm {

class MCSymbolGOFF : public MCSymbol {
public:
  MCSymbolGOFF(const MCSymbolTableEntry *Name, bool IsTemporary)
      : MCSymbol(SymbolKindGOFF, Name, IsTemporary) {}
  static bool classof(const MCSymbol *S) { return S->isGOFF(); }
};
} // end namespace llvm

#endif
