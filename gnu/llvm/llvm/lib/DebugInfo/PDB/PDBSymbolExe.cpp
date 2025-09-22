//===- PDBSymbolExe.cpp - ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"

#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/PDBSymDumper.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypePointer.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

using namespace llvm;
using namespace llvm::pdb;

void PDBSymbolExe::dump(PDBSymDumper &Dumper) const { Dumper.dump(*this); }

uint32_t PDBSymbolExe::getPointerByteSize() const {
  auto Pointer = findOneChild<PDBSymbolTypePointer>();
  if (Pointer)
    return Pointer->getLength();

  if (getMachineType() == PDB_Machine::x86)
    return 4;
  return 8;
}
