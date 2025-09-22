//===- SymbolStream.cpp - PDB Symbol Stream Access ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"

#include "llvm/DebugInfo/MSF/MappedBlockStream.h"

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::support;
using namespace llvm::pdb;

SymbolStream::SymbolStream(std::unique_ptr<MappedBlockStream> Stream)
    : Stream(std::move(Stream)) {}

SymbolStream::~SymbolStream() = default;

Error SymbolStream::reload() {
  BinaryStreamReader Reader(*Stream);

  if (auto EC = Reader.readArray(SymbolRecords, Stream->getLength()))
    return EC;

  return Error::success();
}

iterator_range<codeview::CVSymbolArray::Iterator>
SymbolStream::getSymbols(bool *HadError) const {
  return llvm::make_range(SymbolRecords.begin(HadError), SymbolRecords.end());
}

Error SymbolStream::commit() { return Error::success(); }

codeview::CVSymbol SymbolStream::readRecord(uint32_t Offset) const {
  return *SymbolRecords.at(Offset);
}
