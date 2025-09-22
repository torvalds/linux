//===- NativeExeSymbol.h - native impl for PDBSymbolExe ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEEXESYMBOL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEEXESYMBOL_H

#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeSession;

class DbiStream;

class NativeExeSymbol : public NativeRawSymbol {
  // EXE symbol is the authority on the various symbol types.
  DbiStream *Dbi = nullptr;

public:
  NativeExeSymbol(NativeSession &Session, SymIndexId Id);

  std::unique_ptr<IPDBEnumSymbols>
  findChildren(PDB_SymType Type) const override;

  uint32_t getAge() const override;
  std::string getSymbolsFileName() const override;
  codeview::GUID getGuid() const override;
  bool hasCTypes() const override;
  bool hasPrivateSymbols() const override;
};

} // namespace pdb
} // namespace llvm

#endif
