//===- IPDBSourceFile.cpp - base interface for a PDB source file ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"
#include "llvm/DebugInfo/PDB/PDBExtras.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <string>

using namespace llvm;
using namespace llvm::pdb;

IPDBSourceFile::~IPDBSourceFile() = default;

void IPDBSourceFile::dump(raw_ostream &OS, int Indent) const {
  OS.indent(Indent);
  PDB_Checksum ChecksumType = getChecksumType();
  OS << "[";
  if (ChecksumType != PDB_Checksum::None) {
    OS << ChecksumType << ": ";
    std::string Checksum = getChecksum();
    for (uint8_t c : Checksum)
      OS << format_hex_no_prefix(c, 2, true);
  } else
    OS << "No checksum";
  OS << "] " << getFileName() << "\n";
}
