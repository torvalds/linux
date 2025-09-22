//===- SymbolSerializer.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/SymbolSerializer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>
#include <cstdint>
#include <cstring>

using namespace llvm;
using namespace llvm::codeview;

SymbolSerializer::SymbolSerializer(BumpPtrAllocator &Allocator,
                                   CodeViewContainer Container)
    : Storage(Allocator), Stream(RecordBuffer, llvm::endianness::little),
      Writer(Stream), Mapping(Writer, Container) {}

Error SymbolSerializer::visitSymbolBegin(CVSymbol &Record) {
  assert(!CurrentSymbol && "Already in a symbol mapping!");

  Writer.setOffset(0);

  if (auto EC = writeRecordPrefix(Record.kind()))
    return EC;

  CurrentSymbol = Record.kind();
  if (auto EC = Mapping.visitSymbolBegin(Record))
    return EC;

  return Error::success();
}

Error SymbolSerializer::visitSymbolEnd(CVSymbol &Record) {
  assert(CurrentSymbol && "Not in a symbol mapping!");

  if (auto EC = Mapping.visitSymbolEnd(Record))
    return EC;

  uint32_t RecordEnd = Writer.getOffset();
  uint16_t Length = RecordEnd - 2;
  Writer.setOffset(0);
  if (auto EC = Writer.writeInteger(Length))
    return EC;

  uint8_t *StableStorage = Storage.Allocate<uint8_t>(RecordEnd);
  ::memcpy(StableStorage, &RecordBuffer[0], RecordEnd);
  Record.RecordData = ArrayRef<uint8_t>(StableStorage, RecordEnd);
  CurrentSymbol.reset();

  return Error::success();
}
