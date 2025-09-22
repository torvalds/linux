//==- DIAEnumLineNumbers.h - DIA Line Number Enumerator impl -----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAENUMLINENUMBERS_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAENUMLINENUMBERS_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"

namespace llvm {
namespace pdb {
class IPDBLineNumber;

class DIAEnumLineNumbers : public IPDBEnumChildren<IPDBLineNumber> {
public:
  explicit DIAEnumLineNumbers(CComPtr<IDiaEnumLineNumbers> DiaEnumerator);

  uint32_t getChildCount() const override;
  ChildTypePtr getChildAtIndex(uint32_t Index) const override;
  ChildTypePtr getNext() override;
  void reset() override;

private:
  CComPtr<IDiaEnumLineNumbers> Enumerator;
};
}
}

#endif
