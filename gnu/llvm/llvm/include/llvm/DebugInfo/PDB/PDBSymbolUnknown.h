//===- PDBSymbolUnknown.h - unknown symbol type -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLUNKNOWN_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLUNKNOWN_H

#include "PDBSymbol.h"

namespace llvm {

namespace pdb {

class PDBSymbolUnknown : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CUSTOM_TYPE(S->getSymTag() == PDB_SymType::None ||
                                 S->getSymTag() >= PDB_SymType::Max)

public:
  void dump(PDBSymDumper &Dumper) const override;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLUNKNOWN_H
