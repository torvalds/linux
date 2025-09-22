//===- NativeTypeEnum.h - info about enum type ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEENUM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEENUM_H

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
class raw_ostream;
namespace pdb {

class NativeTypeBuiltin;

class NativeTypeEnum : public NativeRawSymbol {
public:
  NativeTypeEnum(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                 codeview::EnumRecord Record);

  NativeTypeEnum(NativeSession &Session, SymIndexId Id,
                 NativeTypeEnum &UnmodifiedType,
                 codeview::ModifierRecord Modifier);
  ~NativeTypeEnum() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type) const override;

  PDB_BuiltinType getBuiltinType() const override;
  PDB_SymType getSymTag() const override;
  SymIndexId getUnmodifiedTypeId() const override;
  bool hasConstructor() const override;
  bool hasAssignmentOperator() const override;
  bool hasCastOperator() const override;
  uint64_t getLength() const override;
  std::string getName() const override;
  bool isConstType() const override;
  bool isVolatileType() const override;
  bool isUnalignedType() const override;
  bool isNested() const override;
  bool hasOverloadedOperator() const override;
  bool hasNestedTypes() const override;
  bool isIntrinsic() const override;
  bool isPacked() const override;
  bool isScoped() const override;
  SymIndexId getTypeId() const override;
  bool isRefUdt() const override;
  bool isValueUdt() const override;
  bool isInterfaceUdt() const override;

  const NativeTypeBuiltin &getUnderlyingBuiltinType() const;
  const codeview::EnumRecord &getEnumRecord() const { return *Record; }

protected:
  codeview::TypeIndex Index;
  std::optional<codeview::EnumRecord> Record;
  NativeTypeEnum *UnmodifiedType = nullptr;
  std::optional<codeview::ModifierRecord> Modifiers;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEENUM_H
