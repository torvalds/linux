//===- PDBSymbolCompiland.h - Accessors for querying PDB compilands -----*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILAND_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILAND_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include <string>

namespace llvm {

class raw_ostream;

namespace pdb {

class PDBSymbolCompiland : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::Compiland)
public:
  void dump(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_METHOD(isEditAndContinueEnabled)
  FORWARD_SYMBOL_ID_METHOD(getLexicalParent)
  FORWARD_SYMBOL_METHOD(getLibraryName)
  FORWARD_SYMBOL_METHOD(getName)

  std::string getSourceFileName() const;
  std::string getSourceFileFullPath() const;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLCOMPILAND_H
