//===- DIASourceFile.cpp - DIA implementation of IPDBSourceFile -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIASourceFile.h"
#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/DIA/DIAEnumSymbols.h"
#include "llvm/DebugInfo/PDB/DIA/DIASession.h"
#include "llvm/DebugInfo/PDB/DIA/DIAUtils.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"

using namespace llvm;
using namespace llvm::pdb;

DIASourceFile::DIASourceFile(const DIASession &PDBSession,
                             CComPtr<IDiaSourceFile> DiaSourceFile)
    : Session(PDBSession), SourceFile(DiaSourceFile) {}

std::string DIASourceFile::getFileName() const {
  return invokeBstrMethod(*SourceFile, &IDiaSourceFile::get_fileName);
}

uint32_t DIASourceFile::getUniqueId() const {
  DWORD Id;
  return (S_OK == SourceFile->get_uniqueId(&Id)) ? Id : 0;
}

std::string DIASourceFile::getChecksum() const {
  DWORD ByteSize = 0;
  HRESULT Result = SourceFile->get_checksum(0, &ByteSize, nullptr);
  if (ByteSize == 0)
    return std::string();
  std::vector<BYTE> ChecksumBytes(ByteSize);
  Result = SourceFile->get_checksum(ByteSize, &ByteSize, &ChecksumBytes[0]);
  if (S_OK != Result)
    return std::string();
  return std::string(ChecksumBytes.begin(), ChecksumBytes.end());
}

PDB_Checksum DIASourceFile::getChecksumType() const {
  DWORD Type;
  HRESULT Result = SourceFile->get_checksumType(&Type);
  if (S_OK != Result)
    return PDB_Checksum::None;
  return static_cast<PDB_Checksum>(Type);
}

std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>
DIASourceFile::getCompilands() const {
  CComPtr<IDiaEnumSymbols> DiaEnumerator;
  HRESULT Result = SourceFile->get_compilands(&DiaEnumerator);
  if (S_OK != Result)
    return nullptr;

  auto Enumerator = std::unique_ptr<IPDBEnumSymbols>(
      new DIAEnumSymbols(Session, DiaEnumerator));
  return std::unique_ptr<IPDBEnumChildren<PDBSymbolCompiland>>(
      new ConcreteSymbolEnumerator<PDBSymbolCompiland>(std::move(Enumerator)));
}
