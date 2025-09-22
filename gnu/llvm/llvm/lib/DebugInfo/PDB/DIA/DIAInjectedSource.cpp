//===- DIAInjectedSource.cpp - DIA impl for IPDBInjectedSource --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIAInjectedSource.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/DIA/DIASession.h"
#include "llvm/DebugInfo/PDB/DIA/DIAUtils.h"

using namespace llvm;
using namespace llvm::pdb;

DIAInjectedSource::DIAInjectedSource(CComPtr<IDiaInjectedSource> DiaSourceFile)
    : SourceFile(DiaSourceFile) {}

uint32_t DIAInjectedSource::getCrc32() const {
  DWORD Crc;
  return (S_OK == SourceFile->get_crc(&Crc)) ? Crc : 0;
}

uint64_t DIAInjectedSource::getCodeByteSize() const {
  ULONGLONG Size;
  return (S_OK == SourceFile->get_length(&Size)) ? Size : 0;
}

std::string DIAInjectedSource::getFileName() const {
  return invokeBstrMethod(*SourceFile, &IDiaInjectedSource::get_filename);
}

std::string DIAInjectedSource::getObjectFileName() const {
  return invokeBstrMethod(*SourceFile, &IDiaInjectedSource::get_objectFilename);
}

std::string DIAInjectedSource::getVirtualFileName() const {
  return invokeBstrMethod(*SourceFile,
                          &IDiaInjectedSource::get_virtualFilename);
}

uint32_t DIAInjectedSource::getCompression() const {
  DWORD Compression = 0;
  if (S_OK != SourceFile->get_sourceCompression(&Compression))
    return PDB_SourceCompression::None;
  return static_cast<uint32_t>(Compression);
}

std::string DIAInjectedSource::getCode() const {
  DWORD DataSize;
  if (S_OK != SourceFile->get_source(0, &DataSize, nullptr))
    return "";

  std::vector<uint8_t> Buffer(DataSize);
  if (S_OK != SourceFile->get_source(DataSize, &DataSize, Buffer.data()))
    return "";
  assert(Buffer.size() == DataSize);
  return std::string(reinterpret_cast<const char *>(Buffer.data()),
                     Buffer.size());
}
