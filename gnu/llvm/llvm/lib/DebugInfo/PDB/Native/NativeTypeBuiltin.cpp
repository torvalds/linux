//===- NativeTypeBuiltin.cpp -------------------------------------- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeTypeBuiltin.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

NativeTypeBuiltin::NativeTypeBuiltin(NativeSession &PDBSession, SymIndexId Id,
                                     ModifierOptions Mods, PDB_BuiltinType T,
                                     uint64_t L)
    : NativeRawSymbol(PDBSession, PDB_SymType::BuiltinType, Id),
      Session(PDBSession), Mods(Mods), Type(T), Length(L) {}

NativeTypeBuiltin::~NativeTypeBuiltin() = default;

void NativeTypeBuiltin::dump(raw_ostream &OS, int Indent,
                             PdbSymbolIdField ShowIdFields,
                             PdbSymbolIdField RecurseIdFields) const {}

PDB_SymType NativeTypeBuiltin::getSymTag() const {
  return PDB_SymType::BuiltinType;
}

PDB_BuiltinType NativeTypeBuiltin::getBuiltinType() const { return Type; }

bool NativeTypeBuiltin::isConstType() const {
  return (Mods & ModifierOptions::Const) != ModifierOptions::None;
}

uint64_t NativeTypeBuiltin::getLength() const { return Length; }

bool NativeTypeBuiltin::isUnalignedType() const {
  return (Mods & ModifierOptions::Unaligned) != ModifierOptions::None;
}

bool NativeTypeBuiltin::isVolatileType() const {
  return (Mods & ModifierOptions::Volatile) != ModifierOptions::None;
}
