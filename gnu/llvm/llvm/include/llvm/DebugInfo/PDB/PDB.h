//===- PDB.h - base header file for creating a PDB reader -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_PDB_H
#define LLVM_DEBUGINFO_PDB_PDB_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Error.h"
#include <memory>

namespace llvm {
namespace pdb {

class IPDBSession;

Error loadDataForPDB(PDB_ReaderType Type, StringRef Path,
                     std::unique_ptr<IPDBSession> &Session);

Error loadDataForEXE(PDB_ReaderType Type, StringRef Path,
                     std::unique_ptr<IPDBSession> &Session);

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_PDB_H
