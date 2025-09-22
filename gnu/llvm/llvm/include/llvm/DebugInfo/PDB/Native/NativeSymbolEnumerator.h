//===- NativeSymbolEnumerator.h - info about enumerator values --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVESYMBOLENUMERATOR_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVESYMBOLENUMERATOR_H

#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {

class raw_ostream;
namespace pdb {
class NativeSession;
class NativeTypeEnum;

class NativeSymbolEnumerator : public NativeRawSymbol {
public:
  NativeSymbolEnumerator(NativeSession &Session, SymIndexId Id,
                         const NativeTypeEnum &Parent,
                         codeview::EnumeratorRecord Record);

  ~NativeSymbolEnumerator() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  SymIndexId getClassParentId() const override;
  SymIndexId getLexicalParentId() const override;
  std::string getName() const override;
  SymIndexId getTypeId() const override;
  PDB_DataKind getDataKind() const override;
  PDB_LocType getLocationType() const override;
  bool isConstType() const override;
  bool isVolatileType() const override;
  bool isUnalignedType() const override;
  Variant getValue() const override;

protected:
  const NativeTypeEnum &Parent;
  codeview::EnumeratorRecord Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVESYMBOLENUMERATOR_H
