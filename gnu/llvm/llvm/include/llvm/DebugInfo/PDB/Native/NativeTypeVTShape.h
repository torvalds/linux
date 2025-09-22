//===- NativeTypeVTShape.h - info about virtual table shape ------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEVTSHAPE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEVTSHAPE_H

#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {
class NativeSession;

class NativeTypeVTShape : public NativeRawSymbol {
public:
  // Create a pointer record for a non-simple type.
  NativeTypeVTShape(NativeSession &Session, SymIndexId Id,
                    codeview::TypeIndex TI, codeview::VFTableShapeRecord SR);

  ~NativeTypeVTShape() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  bool isConstType() const override;
  bool isVolatileType() const override;
  bool isUnalignedType() const override;
  uint32_t getCount() const override;

protected:
  codeview::TypeIndex TI;
  codeview::VFTableShapeRecord Record;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVETYPEVTSHAPE_H
