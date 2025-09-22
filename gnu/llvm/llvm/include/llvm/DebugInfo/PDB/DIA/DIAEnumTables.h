//===- DIAEnumTables.h - DIA Tables Enumerator Impl -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIAENUMTABLES_H
#define LLVM_DEBUGINFO_PDB_DIA_DIAENUMTABLES_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBTable.h"

namespace llvm {
namespace pdb {
class IPDBTable;

class DIAEnumTables : public IPDBEnumChildren<IPDBTable> {
public:
  explicit DIAEnumTables(CComPtr<IDiaEnumTables> DiaEnumerator);

  uint32_t getChildCount() const override;
  std::unique_ptr<IPDBTable> getChildAtIndex(uint32_t Index) const override;
  std::unique_ptr<IPDBTable> getNext() override;
  void reset() override;

private:
  CComPtr<IDiaEnumTables> Enumerator;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_DIA_DIAENUMTABLES_H
