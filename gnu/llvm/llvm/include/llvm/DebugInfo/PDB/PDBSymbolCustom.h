//===- PDBSymbolCustom.h - compiler-specific types --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLCUSTOM_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLCUSTOM_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

namespace pdb {
/// PDBSymbolCustom represents symbols that are compiler-specific and do not
/// fit anywhere else in the lexical hierarchy.
/// https://msdn.microsoft.com/en-us/library/d88sf09h.aspx
class PDBSymbolCustom : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::Custom)
public:
  void dump(PDBSymDumper &Dumper) const override;

  void getDataBytes(llvm::SmallVector<uint8_t, 32> &bytes);
};

} // namespace llvm
}

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLCUSTOM_H
