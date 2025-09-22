//===- PDBSymbolTypeFunctionSig.h - function signature type info *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONSIG_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONSIG_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

class raw_ostream;
namespace pdb {

class PDBSymbolTypeFunctionSig : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::FunctionSig)
public:
  std::unique_ptr<IPDBEnumSymbols> getArguments() const;

  void dump(PDBSymDumper &Dumper) const override;
  void dumpRight(PDBSymDumper &Dumper) const override;
  void dumpArgList(raw_ostream &OS) const;

  bool isCVarArgs() const;

  FORWARD_SYMBOL_METHOD(getCallingConvention)
  FORWARD_SYMBOL_ID_METHOD(getClassParent)
  FORWARD_SYMBOL_ID_METHOD(getUnmodifiedType)
  FORWARD_SYMBOL_METHOD(isConstType)
  FORWARD_SYMBOL_METHOD(getCount)
  FORWARD_SYMBOL_ID_METHOD(getLexicalParent)
  // FORWARD_SYMBOL_METHOD(getObjectPointerType)
  FORWARD_SYMBOL_METHOD(getThisAdjust)
  FORWARD_SYMBOL_ID_METHOD_WITH_NAME(getType, getReturnType)
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  FORWARD_SYMBOL_METHOD(isVolatileType)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLTYPEFUNCTIONSIG_H
