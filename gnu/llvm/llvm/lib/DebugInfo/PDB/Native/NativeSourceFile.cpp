//===- NativeSourceFile.cpp - Native line number implementation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/NativeSourceFile.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTable.h"

using namespace llvm;
using namespace llvm::pdb;

NativeSourceFile::NativeSourceFile(NativeSession &Session, uint32_t FileId,
                                   const codeview::FileChecksumEntry &Checksum)
    : Session(Session), FileId(FileId), Checksum(Checksum) {}

std::string NativeSourceFile::getFileName() const {
  auto ST = Session.getPDBFile().getStringTable();
  if (!ST) {
    consumeError(ST.takeError());
    return "";
  }
  auto FileName = ST->getStringTable().getString(Checksum.FileNameOffset);
  if (!FileName) {
    consumeError(FileName.takeError());
    return "";
  }

  return std::string(FileName.get());
}

uint32_t NativeSourceFile::getUniqueId() const { return FileId; }

std::string NativeSourceFile::getChecksum() const {
  return toStringRef(Checksum.Checksum).str();
}

PDB_Checksum NativeSourceFile::getChecksumType() const {
  return static_cast<PDB_Checksum>(Checksum.Kind);
}

std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
NativeSourceFile::getCompilands() const {
  return nullptr;
}
