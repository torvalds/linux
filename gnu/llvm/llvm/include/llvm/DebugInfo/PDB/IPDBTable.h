//===- IPDBTable.h - Base Interface for a PDB Symbol Context ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_IPDBTABLE_H
#define LLVM_DEBUGINFO_PDB_IPDBTABLE_H

#include "PDBTypes.h"

namespace llvm {
namespace pdb {
class IPDBTable {
public:
  virtual ~IPDBTable();

  virtual std::string getName() const = 0;
  virtual uint32_t getItemCount() const = 0;
  virtual PDB_TableType getTableType() const = 0;
};
}
}

#endif // LLVM_DEBUGINFO_PDB_IPDBTABLE_H
