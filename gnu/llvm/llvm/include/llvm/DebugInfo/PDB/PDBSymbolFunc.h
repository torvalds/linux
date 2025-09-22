//===- PDBSymbolFunc.h - class representing a function instance -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDBSYMBOLFUNC_H
#define LLVM_DEBUGINFO_PDB_PDBSYMBOLFUNC_H

#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"

#include "PDBSymbol.h"
#include "PDBTypes.h"

namespace llvm {

namespace pdb {

class PDBSymDumper;
class PDBSymbolData;
class PDBSymbolTypeFunctionSig;
template <typename ChildType> class IPDBEnumChildren;

class PDBSymbolFunc : public PDBSymbol {
  DECLARE_PDB_SYMBOL_CONCRETE_TYPE(PDB_SymType::Function)
public:
  void dump(PDBSymDumper &Dumper) const override;

  bool isDestructor() const;

  std::unique_ptr<IPDBEnumChildren<PDBSymbolData>> getArguments() const;

  FORWARD_SYMBOL_METHOD(getAccess)
  FORWARD_SYMBOL_METHOD(getAddressOffset)
  FORWARD_SYMBOL_METHOD(getAddressSection)
  FORWARD_SYMBOL_ID_METHOD(getClassParent)
  FORWARD_SYMBOL_METHOD(isCompilerGenerated)
  FORWARD_SYMBOL_METHOD(isConstructorVirtualBase)
  FORWARD_SYMBOL_METHOD(isConstType)
  FORWARD_SYMBOL_METHOD(isCxxReturnUdt)
  FORWARD_SYMBOL_METHOD(hasCustomCallingConvention)
  FORWARD_SYMBOL_METHOD(hasFarReturn)
  FORWARD_SYMBOL_METHOD(hasAlloca)
  FORWARD_SYMBOL_METHOD(hasEH)
  FORWARD_SYMBOL_METHOD(hasEHa)
  FORWARD_SYMBOL_METHOD(hasInlAsm)
  FORWARD_SYMBOL_METHOD(hasLongJump)
  FORWARD_SYMBOL_METHOD(hasSEH)
  FORWARD_SYMBOL_METHOD(hasSecurityChecks)
  FORWARD_SYMBOL_METHOD(hasSetJump)
  FORWARD_SYMBOL_METHOD(hasInterruptReturn)
  FORWARD_SYMBOL_METHOD(isIntroVirtualFunction)
  FORWARD_SYMBOL_METHOD(hasInlineAttribute)
  FORWARD_SYMBOL_METHOD(isNaked)
  FORWARD_SYMBOL_METHOD(isStatic)
  FORWARD_SYMBOL_METHOD(getLength)
  FORWARD_SYMBOL_ID_METHOD(getLexicalParent)
  FORWARD_SYMBOL_METHOD(getLocalBasePointerRegisterId)
  FORWARD_SYMBOL_METHOD(getLocationType)
  FORWARD_SYMBOL_METHOD(getName)
  FORWARD_SYMBOL_METHOD(hasFramePointer)
  FORWARD_SYMBOL_METHOD(hasNoInlineAttribute)
  FORWARD_SYMBOL_METHOD(hasNoReturnAttribute)
  FORWARD_SYMBOL_METHOD(isUnreached)
  FORWARD_SYMBOL_METHOD(getNoStackOrdering)
  FORWARD_SYMBOL_METHOD(hasOptimizedCodeDebugInfo)
  FORWARD_SYMBOL_METHOD(isPureVirtual)
  FORWARD_SYMBOL_METHOD(getRelativeVirtualAddress)
  FORWARD_SYMBOL_METHOD(getToken)
  FORWARD_CONCRETE_SYMBOL_ID_METHOD_WITH_NAME(PDBSymbolTypeFunctionSig, getType,
                                              getSignature)
  FORWARD_SYMBOL_METHOD(isUnalignedType)
  FORWARD_SYMBOL_METHOD(getUndecoratedName)
  FORWARD_SYMBOL_METHOD(isVirtual)
  FORWARD_SYMBOL_METHOD(getVirtualAddress)
  FORWARD_SYMBOL_METHOD(getVirtualBaseOffset)
  FORWARD_SYMBOL_METHOD(isVolatileType)

  std::unique_ptr<IPDBEnumLineNumbers> getLineNumbers() const;
  uint32_t getCompilandId() const;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDBSYMBOLFUNC_H
