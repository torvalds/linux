//===- PDBSymbolAnnotation.h - Accessors for querying PDB annotations ---*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLANNOTATION_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLANNOTATION_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

class PDBSymbolAnnotation : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::Annotation)

public:
  void dump(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_METHOD(getAddressOffset)
  FORWARD_SYMBOL_METHOD(getAddressSection)
  FORWARD_SYMBOL_METHOD(getDataKind)
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  // FORWARD_SYMBOL_METHOD(getValue)
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
};
}
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLANNOTATION_H
