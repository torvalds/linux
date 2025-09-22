//===- PDBSymbolTypeUDT.h - UDT type info -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEUDT_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEUDT_H

#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

class PDBSymDumper;

class PDBSymbolTypeUDT : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::UDT)
public:
  void dump(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_ID_METHOD(getClassParent)
  FORWARD_SYMBOL_ID_METHOD(getUnmodifiedType)
  FORWARD_SYMBOL_METHOD(hasConstructor)
  FORWARD_SYMBOL_METHOD(isConstType)
  FORWARD_SYMBOL_METHOD(hasAssignmentOperator)
  FORWARD_SYMBOL_METHOD(hasCastOperator)
  FORWARD_SYMBOL_METHOD(hasNestedTypes)
  FORWARD_SYMBOL_METHOD(getLength)
  FORWARD_SYMBOL_ID_METHOD(getLexicalParent)
  FORWARD_SYMBOL_METHOD(getName)
  FORWARD_SYMBOL_METHOD(getSrcLineOnTypeDefn)
  FORWARD_SYMBOL_METHOD(isNested)
  FORWARD_SYMBOL_METHOD(hasOverloadedOperator)
  FORWARD_SYMBOL_METHOD(isPacked)
  FORWARD_SYMBOL_METHOD(isScoped)
  FORWARD_SYMBOL_METHOD(getUdtKind)
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  FORWARD_SYMBOL_ID_METHOD(getVirtualTableShape)
  FORWARD_SYMBOL_METHOD(isVolatileType)
  FORWARD_SYMBOL_METHOD(getAccess)
};
}
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEUDT_H
