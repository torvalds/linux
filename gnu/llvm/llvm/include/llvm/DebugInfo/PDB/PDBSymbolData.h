//===- PDBSymbolData.h - PDB data (e.g. variable) accessors -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLDATA_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLDATA_H

#include "PDBSymbol.h"
#include "PDBTypes.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"

namespace llvm {

namespace pdb {

class PDBSymDumper;

class PDBSymbolData : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::Data)
public:
  void dump(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_METHOD(getAccess)
  FORWARD_SYMBOL_METHOD(getAddressOffset)
  FORWARD_SYMBOL_METHOD(getAddressSection)
  FORWARD_SYMBOL_METHOD(getAddressTaken)
  FORWARD_SYMBOL_METHOD(getBitPosition)
  FORWARD_SYMBOL_ID_METHOD(getClassParent)
  FORWARD_SYMBOL_METHOD(isCompilerGenerated)
  FORWARD_SYMBOL_METHOD(isConstType)
  FORWARD_SYMBOL_METHOD(getDataKind)
  FORWARD_SYMBOL_METHOD(isAggregated)
  FORWARD_SYMBOL_METHOD(isSplitted)
  FORWARD_SYMBOL_METHOD(getLength)
  FORWARD_SYMBOL_ID_METHOD(getLexicalParent)
  FORWARD_SYMBOL_METHOD(getLocationType)
  FORWARD_SYMBOL_METHOD(getName)
  FORWARD_SYMBOL_METHOD(getOffset)
  FORWARD_SYMBOL_METHOD(getRegisterId)
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  FORWARD_SYMBOL_METHOD(getSlot)
  FORWARD_SYMBOL_METHOD(getToken)
  FORWARD_SYMBOL_ID_METHOD(getType)
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  FORWARD_SYMBOL_METHOD(getValue)
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
  FORWARD_SYMBOL_METHOD(isVolatileType)

  std::unique_ptr<IPDBEnumLineNumbers> getLineNumbers() const;
  uint32_t getCompilandId() const;
};
} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLDATA_H
