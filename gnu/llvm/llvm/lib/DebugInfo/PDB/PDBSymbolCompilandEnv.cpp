//===- PDBSymbolCompilandEnv.cpp - compiland env variables ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymbolCompilandEnv.h"

#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymDumper.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

using namespace llvm;
using namespace llvm::pdb;

std::string PDBSymbolCompilandEnv::getValue() const {
  Variant Value = RawSymbol->getValue();
  if (Value.Type != PDB_VariantType::String)
    return std::string();
  return std::string(Value.Value.String);
}

void PDBSymbolCompilandEnv::dump(PDBSymDumper &Dumper) const {
  Dumper.dump(*this);
}
