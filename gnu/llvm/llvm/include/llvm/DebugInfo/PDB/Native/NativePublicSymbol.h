//===- NativePublicSymbol.h - info about public symbols ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEPUBLICSYMBOL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEPUBLICSYMBOL_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"

namespace llvm {

class raw_ostream;
namespace pdb {
class NativeSession;

class NativePublicSymbol : public NativeRawSymbol {
public:
  NativePublicSymbol(NativeSession &Session, SymIndexId Id,
                     const codeview::PublicSym32 &Sym);

  ~NativePublicSymbol() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  uint32_t getAddressOffset() const override;
  uint32_t getAddressSection() const override;
  std::string getName() const override;
  uint32_t getRelativeVirtualAddress() const override;
  uint64_t getVirtualAddress() const override;

protected:
  const codeview::PublicSym32 Sym;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVEPUBLICSYMBOL_H
