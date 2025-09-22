//===- NativeTypeUDT.h - info about class/struct type ------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEUDT_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEUDT_H

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {

class raw_ostream;
namespace pdb {
class NativeSession;

class NativeTypeUDT : public NativeRawSymbol {
public:
  NativeTypeUDT(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                codeview::ClassRecord Class);

  NativeTypeUDT(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                codeview::UnionRecord Union);

  NativeTypeUDT(NativeSession &Session, SymIndexId Id,
                NativeTypeUDT &UnmodifiedType,
                codeview::ModifierRecord Modifier);

  ~NativeTypeUDT() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  std::string getName() const override;
  SymIndexId getLexicalParentId() const override;
  SymIndexId getUnmodifiedTypeId() const override;
  SymIndexId getVirtualTableShapeId() const override;
  uint64_t getLength() const override;
  PDB_UdtType getUdtKind() const override;
  bool hasConstructor() const override;
  bool isConstType() const override;
  bool hasAssignmentOperator() const override;
  bool hasCastOperator() const override;
  bool hasNestedTypes() const override;
  bool hasOverloadedOperator() const override;
  bool isInterfaceUdt() const override;
  bool isIntrinsic() const override;
  bool isNested() const override;
  bool isPacked() const override;
  bool isRefUdt() const override;
  bool isScoped() const override;
  bool isValueUdt() const override;
  bool isUnalignedType() const override;
  bool isVolatileType() const override;

protected:
  codeview::TypeIndex Index;

  std::optional<codeview::ClassRecord> Class;
  std::optional<codeview::UnionRecord> Union;
  NativeTypeUDT *UnmodifiedType = nullptr;
  codeview::TagRecord *Tag = nullptr;
  std::optional<codeview::ModifierRecord> Modifiers;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEUDT_H
