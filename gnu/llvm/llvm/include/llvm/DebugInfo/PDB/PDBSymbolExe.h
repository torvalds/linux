//===- PDBSymbolExe.h - Accessors for querying executables in a PDB ----*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLEXE_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLEXE_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

class raw_ostream;

namespace pdb {

class PDBSymbolExe : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::Exe)
public:
  void dump(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_METHOD(getAge)
  FORWARD_SYMBOL_METHOD(getGuid)
  FORWARD_SYMBOL_METHOD(hasCTypes)
  FORWARD_SYMBOL_METHOD(hasPrivateSymbols)
  FORWARD_SYMBOL_METHOD(getMachineType)
  FORWARD_SYMBOL_METHOD(getName)
  FORWARD_SYMBOL_METHOD(getSignature)
  FORWARD_SYMBOL_METHOD(getSymbolsFileName)

  uint32_t getPointerByteSize() const;

private:
  void dumpChildren(raw_ostream &OS, StringRef Label, PDB_SymType ChildType,
                    int Indent) const;
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLEXE_H
