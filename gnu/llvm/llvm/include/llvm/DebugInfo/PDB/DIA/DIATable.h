//===- DIATable.h - DIA implementation of IPDBTable -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_DIA_DIATABLE_H
#define LLVM_DEBUGINFO_PDB_DIA_DIATABLE_H

#include "DIASupport.h"
#include "llvm/DebugInfo/PDB/IPDBTable.h"

namespace llvm {
namespace pdb {
class DIATable : public IPDBTable {
public:
  explicit DIATable(CComPtr<IDiaTable> DiaTable);

  uint32_t getItemCount() const override;
  std::string getName() const override;
  PDB_TableType getTableType() const override;

private:
  CComPtr<IDiaTable> Table;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_DIA_DIATABLE_H
