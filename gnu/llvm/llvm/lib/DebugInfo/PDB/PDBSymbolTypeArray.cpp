//===- PDBSymbolTypeArray.cpp - ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymbolTypeArray.h"

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"

using namespace llvm;
using namespace llvm::pdb;

void PDBSymbolTypeArray::dump(PDBSymDumper &Dumper) const {
  Dumper.dump(*this);
}

void PDBSymbolTypeArray::dumpRight(PDBSymDumper &Dumper) const {
  Dumper.dumpRight(*this);
}
