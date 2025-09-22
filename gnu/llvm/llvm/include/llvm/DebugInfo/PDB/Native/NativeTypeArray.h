//===- NativeTypeArray.h ------------------------------------------ C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEARRAY_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEARRAY_H

#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"

#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeSession;

class NativeTypeArray : public NativeRawSymbol {
public:
  NativeTypeArray(NativeSession &Session, SymIndexId Id, codeview::TypeIndex TI,
                  codeview::ArrayRecord Record);
  ~NativeTypeArray() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  SymIndexId getArrayIndexTypeId() const override;

  bool isConstType() const override;
  bool isUnalignedType() const override;
  bool isVolatileType() const override;

  uint32_t getCount() const override;
  SymIndexId getTypeId() const override;
  uint64_t getLength() const override;

protected:
  codeview::ArrayRecord Record;
  codeview::TypeIndex Index;
};

} // namespace pdb
} // namespace llvm

#endif
