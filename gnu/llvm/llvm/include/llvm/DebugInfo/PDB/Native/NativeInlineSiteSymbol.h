//===- NativeInlineSiteSymbol.h - info about inline sites -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_NATIVEINLINESITESYMBOL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_NATIVEINLINESITESYMBOL_H

#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

namespace llvm {
namespace pdb {

class NativeSession;

class NativeInlineSiteSymbol : public NativeRawSymbol {
public:
  NativeInlineSiteSymbol(NativeSession &Session, SymIndexId Id,
                         const codeview::InlineSiteSym &Sym,
                         uint64_t ParentAddr);

  ~NativeInlineSiteSymbol() override;

  void dump(raw_ostream &OS, int Indent, PdbSymbolIdField ShowIdFields,
            PdbSymbolIdField RecurseIdFields) const override;

  std::string getName() const override;
  std::unique_ptr<IPDBEnumLineNumbers>
  findInlineeLinesByVA(uint64_t VA, uint32_t Length) const override;

private:
  const codeview::InlineSiteSym Sym;
  uint64_t ParentAddr;

  void getLineOffset(uint32_t OffsetInFunc, uint32_t &LineOffset,
                     uint32_t &FileOffset) const;
};

} // namespace pdb
} // namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_NATIVEINLINESITESYMBOL_H
