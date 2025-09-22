//===- PDBSymbolThunk.h - Support for querying PDB thunks ---------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTHUNK_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTHUNK_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

class PDBSymbolThunk : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::Thunk)
public:
  void dump(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_METHOD(getAccess)
  FORWARD_SYMBOL_METHOD(getAddressOffset)
  FORWARD_SYMBOL_METHOD(getAddressSection)
  FORWARD_SYMBOL_ID_METHOD(getClassParent)
  FORWARD_SYMBOL_METHOD(isConstType)
  FORWARD_SYMBOL_METHOD(isIntroVirtualFunction)
  FORWARD_SYMBOL_METHOD(isStatic)
  FORWARD_SYMBOL_METHOD(getLength)
  FORWARD_SYMBOL_ID_METHOD(getLexicalParent)
  FORWARD_SYMBOL_METHOD(getName)
  FORWARD_SYMBOL_METHOD(isPureVirtual)
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  FORWARD_SYMBOL_METHOD(getTargetOffset)
  FORWARD_SYMBOL_METHOD(getTargetRelativeVirtualAddress)
  FORWARD_SYMBOL_METHOD(getTargetVirtualAddress)
  FORWARD_SYMBOL_METHOD(getTargetSection)
  FORWARD_SYMBOL_METHOD(getThunkOrdinal)
  FORWARD_SYMBOL_ID_METHOD(getType)
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  FORWARD_SYMBOL_METHOD(isVirtual)
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
  FORWARD_SYMBOL_METHOD(getVirtualBaseOffset)
  FORWARD_SYMBOL_METHOD(isVolatileType)
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTHUNK_H
