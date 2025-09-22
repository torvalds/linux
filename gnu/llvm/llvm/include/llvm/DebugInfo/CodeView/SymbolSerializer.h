//===- SymbolSerializer.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_SYMBOLSERIALIZER_H
#define LLVM_DEBUGINFO_CODEVIEW_SYMBOLSERIALIZER_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/SymbolRecordMapping.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbacks.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <array>
#include <cstdint>

namespace llvm {
namespace codeview {

class SymbolSerializer : public SymbolVisitorCallbacks {
  BumpPtrAllocator &Storage;
  // Since this is a fixed size buffer, use a stack allocated buffer.  This
  // yields measurable performance increase over the repeated heap allocations
  // when serializing many independent records via writeOneSymbol.
  std::array<uint8_t, MaxRecordLength> RecordBuffer;
  MutableBinaryByteStream Stream;
  BinaryStreamWriter Writer;
  SymbolRecordMapping Mapping;
  std::optional<SymbolKind> CurrentSymbol;

  Error writeRecordPrefix(SymbolKind Kind) {
    RecordPrefix Prefix;
    Prefix.RecordKind = Kind;
    Prefix.RecordLen = 0;
    if (auto EC = Writer.writeObject(Prefix))
      return EC;
    return Error::success();
  }

public:
  SymbolSerializer(BumpPtrAllocator &Storage, CodeViewContainer Container);

  template <typename SymType>
  static CVSymbol writeOneSymbol(SymType &Sym, BumpPtrAllocator &Storage,
                                 CodeViewContainer Container) {
    RecordPrefix Prefix{uint16_t(Sym.Kind)};
    CVSymbol Result(&Prefix, sizeof(Prefix));
    SymbolSerializer Serializer(Storage, Container);
    consumeError(Serializer.visitSymbolBegin(Result));
    consumeError(Serializer.visitKnownRecord(Result, Sym));
    consumeError(Serializer.visitSymbolEnd(Result));
    return Result;
  }

  Error visitSymbolBegin(CVSymbol &Record) override;
  Error visitSymbolEnd(CVSymbol &Record) override;

#define SYMBOL_RECORD(EnumName, EnumVal, Name)                                 \
  Error visitKnownRecord(CVSymbol &CVR, Name &Record) override {               \
    return visitKnownRecordImpl(CVR, Record);                                  \
  }
#define SYMBOL_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"

private:
  template <typename RecordKind>
  Error visitKnownRecordImpl(CVSymbol &CVR, RecordKind &Record) {
    return Mapping.visitKnownRecord(CVR, Record);
  }
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_SYMBOLSERIALIZER_H
