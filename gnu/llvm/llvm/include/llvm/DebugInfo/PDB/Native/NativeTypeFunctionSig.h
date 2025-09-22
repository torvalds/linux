//===- NativeTypeFunctionSig.h - info about function signature ---*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEFUNCTIONSIG_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEFUNCTIONSIG_H

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeTypeFunctionSig : public NativeRawSymbol {
protected:
  void initialize() override;

public:
  NativeTypeFunctionSig(NativeSession &Session, SymIndexId Id,
                        codeview::TypeIndex TI, codeview::ProcedureRecord Proc);

  NativeTypeFunctionSig(NativeSession &Session, SymIndexId Id,
                        codeview::TypeIndex TI,
                        codeview::MemberFunctionRecord MemberFunc);

  ~NativeTypeFunctionSig() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type) const override;

  SymIndexId getClassParentId() const override;
  PDB_CallingConv getCallingConvention() const override;
  uint32_t getCount() const override;
  SymIndexId getTypeId() const override;
  int32_t getThisAdjust() const override;
  bool hasConstructor() const override;
  bool isConstType() const override;
  bool isConstructorVirtualBase() const override;
  bool isCxxReturnUdt() const override;
  bool isUnalignedType() const override;
  bool isVolatileType() const override;

private:
  void initializeArgList(codeview::TypeIndex ArgListTI);

  union {
    codeview::MemberFunctionRecord MemberFunc;
    codeview::ProcedureRecord Proc;
  };

  SymIndexId ClassParentId = 0;
  codeview::TypeIndex Index;
  codeview::ArgListRecord ArgList;
  bool IsMemberFunction = false;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEFUNCTIONSIG_H
