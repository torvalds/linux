//===- PDBSymbolCustom.cpp - compiler-specific types ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymbolCustom.h"

#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

using namespace llvm;
using namespace llvm::pdb;

void PDBSymbolCustom::getDataBytes(llvm::SmallVector<uint8_t, 32> &bytes) {
  RawSymbol->getDataBytes(bytes);
}

void PDBSymbolCustom::dump(PDBSymDumper &Dumper) const { Dumper.dump(*this); }
