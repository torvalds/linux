//===- PDBSymbolFuncDebugStart.h - function start bounds info ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLFUNCDEBUGSTART_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLFUNCDEBUGSTART_H

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

class PDBSymbolFuncDebugStart : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::FuncDebugStart)
public:
  void dump(PDBSymDumper &Dumper) const override;

  FORWARD_SYMBOL_METHOD(getAddressOffset)
  FORWARD_SYMBOL_METHOD(getAddressSection)
  FORWARD_SYMBOL_METHOD(hasCustomCallingConvention)
  FORWARD_SYMBOL_METHOD(hasFarReturn)
  FORWARD_SYMBOL_METHOD(hasInterruptReturn)
  FORWARD_SYMBOL_METHOD(isStatic)
  FORWARD_SYMBOL_ID_METHOD(getLexicalParent)
  FORWARD_SYMBOL_METHOD(getLocationType)
  FORWARD_SYMBOL_METHOD(hasNoInlineAttribute)
  FORWARD_SYMBOL_METHOD(hasNoReturnAttribute)
  FORWARD_SYMBOL_METHOD(isUnreached)
  FORWARD_SYMBOL_METHOD(getOffset)
  FORWARD_SYMBOL_METHOD(hasOptimizedCodeDebugInfo)
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLFUNCDEBUGSTART_H
