//===- SimpleTypeSerializer.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/SimpleTypeSerializer.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/TypeRecordMapping.h"
#include "llvm/Support/BinaryStreamWriter.h"

using namespace llvm;
using namespace llvm::codeview;

static void addPadding(BinaryStreamWriter &Writer) {
  uint32_t Align = Writer.getOffset() % 4;
  if (Align == 0)
    return;

  int PaddingBytes = 4 - Align;
  while (PaddingBytes > 0) {
    uint8_t Pad = static_cast<uint8_t>(LF_PAD0 + PaddingBytes);
    cantFail(Writer.writeInteger(Pad));
    --PaddingBytes;
  }
}

SimpleTypeSerializer::SimpleTypeSerializer() : ScratchBuffer(MaxRecordLength) {}

SimpleTypeSerializer::~SimpleTypeSerializer() = default;

template <typename T>
ArrayRef<uint8_t> SimpleTypeSerializer::serialize(T &Record) {
  BinaryStreamWriter Writer(ScratchBuffer, llvm::endianness::little);
  TypeRecordMapping Mapping(Writer);

  // Write the record prefix first with a dummy length but real kind.
  RecordPrefix DummyPrefix(uint16_t(Record.getKind()));
  cantFail(Writer.writeObject(DummyPrefix));

  RecordPrefix *Prefix = reinterpret_cast<RecordPrefix *>(ScratchBuffer.data());
  CVType CVT(Prefix, sizeof(RecordPrefix));

  cantFail(Mapping.visitTypeBegin(CVT));
  cantFail(Mapping.visitKnownRecord(CVT, Record));
  cantFail(Mapping.visitTypeEnd(CVT));

  addPadding(Writer);

  // Update the size and kind after serialization.
  Prefix->RecordKind = CVT.kind();
  Prefix->RecordLen = Writer.getOffset() - sizeof(uint16_t);

  return {ScratchBuffer.data(), static_cast<size_t>(Writer.getOffset())};
}

// Explicitly instantiate the member function for each known type so that we can
// implement this in the cpp file.
#define TYPE_RECORD(EnumName, EnumVal, Name)                                   \
  template ArrayRef<uint8_t> llvm::codeview::SimpleTypeSerializer::serialize(  \
      Name##Record &Record);
#define TYPE_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#define MEMBER_RECORD(EnumName, EnumVal, Name)
#define MEMBER_RECORD_ALIAS(EnumName, EnumVal, Name, AliasName)
#include "llvm/DebugInfo/CodeView/CodeViewTypes.def"
