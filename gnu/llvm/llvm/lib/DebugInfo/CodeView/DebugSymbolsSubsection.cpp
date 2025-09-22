//===- DebugSymbolsSubsection.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/DebugSymbolsSubsection.h"
#include "llvm/Support/BinaryStreamWriter.h"

using namespace llvm;
using namespace llvm::codeview;

Error DebugSymbolsSubsectionRef::initialize(BinaryStreamReader Reader) {
  return Reader.readArray(Records, Reader.getLength());
}

uint32_t DebugSymbolsSubsection::calculateSerializedSize() const {
  return Length;
}

Error DebugSymbolsSubsection::commit(BinaryStreamWriter &Writer) const {
  for (const auto &Record : Records) {
    if (auto EC = Writer.writeBytes(Record.RecordData))
      return EC;
  }
  return Error::success();
}

void DebugSymbolsSubsection::addSymbol(CVSymbol Symbol) {
  Records.push_back(Symbol);
  Length += Symbol.length();
}
