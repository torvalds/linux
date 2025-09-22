//===- DebugSubsectionRecord.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>

using namespace llvm;
using namespace llvm::codeview;

DebugSubsectionRecord::DebugSubsectionRecord() = default;

DebugSubsectionRecord::DebugSubsectionRecord(DebugSubsectionKind Kind,
                                             BinaryStreamRef Data)
    : Kind(Kind), Data(Data) {}

Error DebugSubsectionRecord::initialize(BinaryStreamRef Stream,
                                        DebugSubsectionRecord &Info) {
  const DebugSubsectionHeader *Header;
  BinaryStreamReader Reader(Stream);
  if (auto EC = Reader.readObject(Header))
    return EC;

  DebugSubsectionKind Kind =
      static_cast<DebugSubsectionKind>(uint32_t(Header->Kind));
  if (auto EC = Reader.readStreamRef(Info.Data, Header->Length))
    return EC;
  Info.Kind = Kind;
  return Error::success();
}

uint32_t DebugSubsectionRecord::getRecordLength() const {
  return sizeof(DebugSubsectionHeader) + Data.getLength();
}

DebugSubsectionKind DebugSubsectionRecord::kind() const { return Kind; }

BinaryStreamRef DebugSubsectionRecord::getRecordData() const { return Data; }

DebugSubsectionRecordBuilder::DebugSubsectionRecordBuilder(
    std::shared_ptr<DebugSubsection> Subsection)
    : Subsection(std::move(Subsection)) {}

DebugSubsectionRecordBuilder::DebugSubsectionRecordBuilder(
    const DebugSubsectionRecord &Contents)
    : Contents(Contents) {}

uint32_t DebugSubsectionRecordBuilder::calculateSerializedLength() const {
  uint32_t DataSize = Subsection ? Subsection->calculateSerializedSize()
                                 : Contents.getRecordData().getLength();
  // The length of the entire subsection is always padded to 4 bytes,
  // regardless of the container kind.
  return sizeof(DebugSubsectionHeader) + alignTo(DataSize, 4);
}

Error DebugSubsectionRecordBuilder::commit(BinaryStreamWriter &Writer,
                                           CodeViewContainer Container) const {
  assert(Writer.getOffset() % alignOf(Container) == 0 &&
         "Debug Subsection not properly aligned");

  DebugSubsectionHeader Header;
  Header.Kind = uint32_t(Subsection ? Subsection->kind() : Contents.kind());
  // The value written into the Header's Length field is only padded to the
  // container's alignment
  uint32_t DataSize = Subsection ? Subsection->calculateSerializedSize()
                                 : Contents.getRecordData().getLength();
  Header.Length = alignTo(DataSize, alignOf(Container));

  if (auto EC = Writer.writeObject(Header))
    return EC;
  if (Subsection) {
    if (auto EC = Subsection->commit(Writer))
      return EC;
  } else {
    if (auto EC = Writer.writeStreamRef(Contents.getRecordData()))
      return EC;
  }
  if (auto EC = Writer.padToAlignment(4))
    return EC;

  return Error::success();
}
