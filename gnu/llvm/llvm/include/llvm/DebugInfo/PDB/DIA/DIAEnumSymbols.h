//==- DIAEnumSymbols.h - DIA Symbol Enumerator impl --------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAENUMSYMBOLS_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAENUMSYMBOLS_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"

namespace llvm {
namespace pdb {
class DIASession;

class DIAEnumSymbols : public IPDBEnumChildren<PDBSymbol> {
public:
  explicit DIAEnumSymbols(const DIASession &Session,
                          CComPtr<IDiaEnumSymbols> DiaEnumerator);

  uint32_t getChildCount() const override;
  std::unique_ptr<PDBSymbol> getChildAtIndex(uint32_t Index) const override;
  std::unique_ptr<PDBSymbol> getNext() override;
  void reset() override;

private:
  const DIASession &Session;
  CComPtr<IDiaEnumSymbols> Enumerator;
};
}
}

#endif
