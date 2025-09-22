//===- NativeTypePointer.h - info about pointer type -------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeTypePointer : public NativeRawSymbol {
public:
  // Create a pointer record for a simple type.
  NativeTypePointer(NativeSession &Session, SymIndexId Id,
                    codeview::TypeIndex TI);

  // Create a pointer record for a non-simple type.
  NativeTypePointer(NativeSession &Session, SymIndexId Id,
                    codeview::TypeIndex TI, codeview::PointerRecord PR);
  ~NativeTypePointer() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  SymIndexId getClassParentId() const override;
  bool isConstType() const override;
  uint64_t getLength() const override;
  bool isReference() const override;
  bool isRValueReference() const override;
  bool isPointerToDataMember() const override;
  bool isPointerToMemberFunction() const override;
  SymIndexId getTypeId() const override;
  bool isRestrictedType() const override;
  bool isVolatileType() const override;
  bool isUnalignedType() const override;

  bool isSingleInheritance() const override;
  bool isMultipleInheritance() const override;
  bool isVirtualInheritance() const override;

protected:
  bool isMemberPointer() const;
  codeview::TypeIndex TI;
  std::optional<codeview::PointerRecord> Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEPOINTER_H
