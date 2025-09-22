//===- PDBSymDumper.cpp - ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymDumper.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;
using namespace llvm::pdb;

#define PDB_SYMDUMP_UNREACHABLE(Type)                                          \
  if (RequireImpl)                                                             \
    llvm_unreachable("Attempt to dump " #Type " with no dump implementation");

PDBSymDumper::PDBSymDumper(bool ShouldRequireImpl)
    : RequireImpl(ShouldRequireImpl) {}

PDBSymDumper::~PDBSymDumper() = default;

void PDBSymDumper::dump(const PDBSymbolAnnotation &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolAnnotation)
}

void PDBSymDumper::dump(const PDBSymbolBlock &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolBlock)
}

void PDBSymDumper::dump(const PDBSymbolCompiland &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolCompiland)
}

void PDBSymDumper::dump(const PDBSymbolCompilandDetails &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolCompilandDetails)
}

void PDBSymDumper::dump(const PDBSymbolCompilandEnv &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolCompilandEnv)
}

void PDBSymDumper::dump(const PDBSymbolCustom &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolCustom)
}

void PDBSymDumper::dump(const PDBSymbolData &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolData)
}

void PDBSymDumper::dump(const PDBSymbolExe &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolExe)
}

void PDBSymDumper::dump(const PDBSymbolFunc &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolFunc)
}

void PDBSymDumper::dump(const PDBSymbolFuncDebugEnd &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolFuncDebugEnd)
}

void PDBSymDumper::dump(const PDBSymbolFuncDebugStart &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolFuncDebugStart)
}

void PDBSymDumper::dump(const PDBSymbolLabel &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolLabel)
}

void PDBSymDumper::dump(const PDBSymbolPublicSymbol &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolPublicSymbol)
}

void PDBSymDumper::dump(const PDBSymbolThunk &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolThunk)
}

void PDBSymDumper::dump(const PDBSymbolTypeArray &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeArray)
}

void PDBSymDumper::dump(const PDBSymbolTypeBaseClass &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeBaseClass)
}

void PDBSymDumper::dump(const PDBSymbolTypeBuiltin &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeBuiltin)
}

void PDBSymDumper::dump(const PDBSymbolTypeCustom &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeCustom)
}

void PDBSymDumper::dump(const PDBSymbolTypeDimension &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeDimension)
}

void PDBSymDumper::dump(const PDBSymbolTypeEnum &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeEnum)
}

void PDBSymDumper::dump(const PDBSymbolTypeFriend &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeFriend)
}

void PDBSymDumper::dump(const PDBSymbolTypeFunctionArg &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeFunctionArg)
}

void PDBSymDumper::dump(const PDBSymbolTypeFunctionSig &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeFunctionSig)
}

void PDBSymDumper::dump(const PDBSymbolTypeManaged &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeManaged)
}

void PDBSymDumper::dump(const PDBSymbolTypePointer &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypePointer)
}

void PDBSymDumper::dump(const PDBSymbolTypeTypedef &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeTypedef)
}

void PDBSymDumper::dump(const PDBSymbolTypeUDT &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeUDT)
}

void PDBSymDumper::dump(const PDBSymbolTypeVTable &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeVTable)
}

void PDBSymDumper::dump(const PDBSymbolTypeVTableShape &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolTypeVTableShape)
}

void PDBSymDumper::dump(const PDBSymbolUnknown &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolUnknown)
}

void PDBSymDumper::dump(const PDBSymbolUsingNamespace &Symbol) {
  PDB_SYMDUMP_UNREACHABLE(PDBSymbolUsingNamespace)
}
