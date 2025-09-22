//===- CodeViewRecordIO.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/CodeView/CodeViewRecordIO.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/GUID.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"

using namespace llvm;
using namespace llvm::codeview;

Error CodeViewRecordIO::beginRecord(std::optional<uint32_t> MaxLength) {
  RecordLimit Limit;
  Limit.MaxLength = MaxLength;
  Limit.BeginOffset = getCurrentOffset();
  Limits.push_back(Limit);
  return Error::success();
}

Error CodeViewRecordIO::endRecord() {
  assert(!Limits.empty() && "Not in a record!");
  Limits.pop_back();
  // We would like to assert that we actually read / wrote all the bytes that we
  // expected to for this record, but unfortunately we can't do this.  Some
  // producers such as MASM over-allocate for certain types of records and
  // commit the extraneous data, so when reading we can't be sure every byte
  // will have been read.  And when writing we over-allocate temporarily since
  // we don't know how big the record is until we're finished writing it, so
  // even though we don't commit the extraneous data, we still can't guarantee
  // we're at the end of the allocated data.

  if (isStreaming()) {
    // For streaming mode, add padding to align with 4 byte boundaries for each
    // record
    uint32_t Align = getStreamedLen() % 4;
    if (Align == 0)
      return Error::success();

    int PaddingBytes = 4 - Align;
    while (PaddingBytes > 0) {
      char Pad = static_cast<uint8_t>(LF_PAD0 + PaddingBytes);
      StringRef BytesSR = StringRef(&Pad, sizeof(Pad));
      Streamer->emitBytes(BytesSR);
      --PaddingBytes;
    }
    resetStreamedLen();
  }
  return Error::success();
}

uint32_t CodeViewRecordIO::maxFieldLength() const {
  if (isStreaming())
    return 0;

  assert(!Limits.empty() && "Not in a record!");

  // The max length of the next field is the minimum of all lengths that would
  // be allowed by any of the sub-records we're in.  In practice, we can only
  // ever be at most 1 sub-record deep (in a FieldList), but this works for
  // the general case.
  uint32_t Offset = getCurrentOffset();
  std::optional<uint32_t> Min = Limits.front().bytesRemaining(Offset);
  for (auto X : ArrayRef(Limits).drop_front()) {
    std::optional<uint32_t> ThisMin = X.bytesRemaining(Offset);
    if (ThisMin)
      Min = Min ? std::min(*Min, *ThisMin) : *ThisMin;
  }
  assert(Min && "Every field must have a maximum length!");

  return *Min;
}

Error CodeViewRecordIO::padToAlignment(uint32_t Align) {
  if (isReading())
    return Reader->padToAlignment(Align);
  return Writer->padToAlignment(Align);
}

Error CodeViewRecordIO::skipPadding() {
  assert(!isWriting() && "Cannot skip padding while writing!");

  if (Reader->bytesRemaining() == 0)
    return Error::success();

  uint8_t Leaf = Reader->peek();
  if (Leaf < LF_PAD0)
    return Error::success();
  // Leaf is greater than 0xf0. We should advance by the number of bytes in
  // the low 4 bits.
  unsigned BytesToAdvance = Leaf & 0x0F;
  return Reader->skip(BytesToAdvance);
}

Error CodeViewRecordIO::mapByteVectorTail(ArrayRef<uint8_t> &Bytes,
                                          const Twine &Comment) {
  if (isStreaming()) {
    emitComment(Comment);
    Streamer->emitBinaryData(toStringRef(Bytes));
    incrStreamedLen(Bytes.size());
  } else if (isWriting()) {
    if (auto EC = Writer->writeBytes(Bytes))
      return EC;
  } else {
    if (auto EC = Reader->readBytes(Bytes, Reader->bytesRemaining()))
      return EC;
  }
  return Error::success();
}

Error CodeViewRecordIO::mapByteVectorTail(std::vector<uint8_t> &Bytes,
                                          const Twine &Comment) {
  ArrayRef<uint8_t> BytesRef(Bytes);
  if (auto EC = mapByteVectorTail(BytesRef, Comment))
    return EC;
  if (!isWriting())
    Bytes.assign(BytesRef.begin(), BytesRef.end());

  return Error::success();
}

Error CodeViewRecordIO::mapInteger(TypeIndex &TypeInd, const Twine &Comment) {
  if (isStreaming()) {
    std::string TypeNameStr = Streamer->getTypeName(TypeInd);
    if (!TypeNameStr.empty())
      emitComment(Comment + ": " + TypeNameStr);
    else
      emitComment(Comment);
    Streamer->emitIntValue(TypeInd.getIndex(), sizeof(TypeInd.getIndex()));
    incrStreamedLen(sizeof(TypeInd.getIndex()));
  } else if (isWriting()) {
    if (auto EC = Writer->writeInteger(TypeInd.getIndex()))
      return EC;
  } else {
    uint32_t I;
    if (auto EC = Reader->readInteger(I))
      return EC;
    TypeInd.setIndex(I);
  }
  return Error::success();
}

Error CodeViewRecordIO::mapEncodedInteger(int64_t &Value,
                                          const Twine &Comment) {
  if (isStreaming()) {
    if (Value >= 0)
      emitEncodedUnsignedInteger(static_cast<uint64_t>(Value), Comment);
    else
      emitEncodedSignedInteger(Value, Comment);
  } else if (isWriting()) {
    if (Value >= 0) {
      if (auto EC = writeEncodedUnsignedInteger(static_cast<uint64_t>(Value)))
        return EC;
    } else {
      if (auto EC = writeEncodedSignedInteger(Value))
        return EC;
    }
  } else {
    APSInt N;
    if (auto EC = consume(*Reader, N))
      return EC;
    Value = N.getExtValue();
  }

  return Error::success();
}

Error CodeViewRecordIO::mapEncodedInteger(uint64_t &Value,
                                          const Twine &Comment) {
  if (isStreaming())
    emitEncodedUnsignedInteger(Value, Comment);
  else if (isWriting()) {
    if (auto EC = writeEncodedUnsignedInteger(Value))
      return EC;
  } else {
    APSInt N;
    if (auto EC = consume(*Reader, N))
      return EC;
    Value = N.getZExtValue();
  }
  return Error::success();
}

Error CodeViewRecordIO::mapEncodedInteger(APSInt &Value, const Twine &Comment) {
  if (isStreaming()) {
    // FIXME: We also need to handle big values here, but it's
    //        not clear how we can excercise this code path yet.
    if (Value.isSigned())
      emitEncodedSignedInteger(Value.getSExtValue(), Comment);
    else
      emitEncodedUnsignedInteger(Value.getZExtValue(), Comment);
  } else if (isWriting()) {
    if (Value.isSigned())
      return writeEncodedSignedInteger(
          Value.isSingleWord() ? Value.getSExtValue() : INT64_MIN);
    return writeEncodedUnsignedInteger(Value.getLimitedValue());
  } else
    return consume(*Reader, Value);
  return Error::success();
}

Error CodeViewRecordIO::mapStringZ(StringRef &Value, const Twine &Comment) {
  if (isStreaming()) {
    auto NullTerminatedString = StringRef(Value.data(), Value.size() + 1);
    emitComment(Comment);
    Streamer->emitBytes(NullTerminatedString);
    incrStreamedLen(NullTerminatedString.size());
  } else if (isWriting()) {
    // Truncate if we attempt to write too much.
    StringRef S = Value.take_front(maxFieldLength() - 1);
    if (auto EC = Writer->writeCString(S))
      return EC;
  } else {
    if (auto EC = Reader->readCString(Value))
      return EC;
  }
  return Error::success();
}

Error CodeViewRecordIO::mapGuid(GUID &Guid, const Twine &Comment) {
  constexpr uint32_t GuidSize = 16;

  if (isStreaming()) {
    StringRef GuidSR =
        StringRef((reinterpret_cast<const char *>(&Guid)), GuidSize);
    emitComment(Comment);
    Streamer->emitBytes(GuidSR);
    incrStreamedLen(GuidSize);
    return Error::success();
  }

  if (maxFieldLength() < GuidSize)
    return make_error<CodeViewError>(cv_error_code::insufficient_buffer);

  if (isWriting()) {
    if (auto EC = Writer->writeBytes(Guid.Guid))
      return EC;
  } else {
    ArrayRef<uint8_t> GuidBytes;
    if (auto EC = Reader->readBytes(GuidBytes, GuidSize))
      return EC;
    memcpy(Guid.Guid, GuidBytes.data(), GuidSize);
  }
  return Error::success();
}

Error CodeViewRecordIO::mapStringZVectorZ(std::vector<StringRef> &Value,
                                          const Twine &Comment) {

  if (!isReading()) {
    emitComment(Comment);
    for (auto V : Value) {
      if (auto EC = mapStringZ(V))
        return EC;
    }
    uint8_t FinalZero = 0;
    if (auto EC = mapInteger(FinalZero))
      return EC;
  } else {
    StringRef S;
    if (auto EC = mapStringZ(S))
      return EC;
    while (!S.empty()) {
      Value.push_back(S);
      if (auto EC = mapStringZ(S))
        return EC;
    };
  }
  return Error::success();
}

void CodeViewRecordIO::emitEncodedSignedInteger(const int64_t &Value,
                                                const Twine &Comment) {
  // FIXME: There are no test cases covering this function.
  // This may be because we always consider enumerators to be unsigned.
  // See FIXME at CodeViewDebug.cpp : CodeViewDebug::lowerTypeEnum.
  if (Value < LF_NUMERIC && Value >= 0) {
    emitComment(Comment);
    Streamer->emitIntValue(Value, 2);
    incrStreamedLen(2);
  } else if (Value >= std::numeric_limits<int8_t>::min() &&
             Value <= std::numeric_limits<int8_t>::max()) {
    Streamer->emitIntValue(LF_CHAR, 2);
    emitComment(Comment);
    Streamer->emitIntValue(Value, 1);
    incrStreamedLen(3);
  } else if (Value >= std::numeric_limits<int16_t>::min() &&
             Value <= std::numeric_limits<int16_t>::max()) {
    Streamer->emitIntValue(LF_SHORT, 2);
    emitComment(Comment);
    Streamer->emitIntValue(Value, 2);
    incrStreamedLen(4);
  } else if (Value >= std::numeric_limits<int32_t>::min() &&
             Value <= std::numeric_limits<int32_t>::max()) {
    Streamer->emitIntValue(LF_LONG, 2);
    emitComment(Comment);
    Streamer->emitIntValue(Value, 4);
    incrStreamedLen(6);
  } else {
    Streamer->emitIntValue(LF_QUADWORD, 2);
    emitComment(Comment);
    Streamer->emitIntValue(Value, 4); // FIXME: Why not 8 (size of quadword)?
    incrStreamedLen(6);               // FIXME: Why not 10 (8 + 2)?
  }
}

void CodeViewRecordIO::emitEncodedUnsignedInteger(const uint64_t &Value,
                                                  const Twine &Comment) {
  if (Value < LF_NUMERIC) {
    emitComment(Comment);
    Streamer->emitIntValue(Value, 2);
    incrStreamedLen(2);
  } else if (Value <= std::numeric_limits<uint16_t>::max()) {
    Streamer->emitIntValue(LF_USHORT, 2);
    emitComment(Comment);
    Streamer->emitIntValue(Value, 2);
    incrStreamedLen(4);
  } else if (Value <= std::numeric_limits<uint32_t>::max()) {
    Streamer->emitIntValue(LF_ULONG, 2);
    emitComment(Comment);
    Streamer->emitIntValue(Value, 4);
    incrStreamedLen(6);
  } else {
    // FIXME: There are no test cases covering this block.
    Streamer->emitIntValue(LF_UQUADWORD, 2);
    emitComment(Comment);
    Streamer->emitIntValue(Value, 8);
    incrStreamedLen(6); // FIXME: Why not 10 (8 + 2)?
  }
}

Error CodeViewRecordIO::writeEncodedSignedInteger(const int64_t &Value) {
  if (Value < LF_NUMERIC && Value >= 0) {
    if (auto EC = Writer->writeInteger<int16_t>(Value))
      return EC;
  } else if (Value >= std::numeric_limits<int8_t>::min() &&
             Value <= std::numeric_limits<int8_t>::max()) {
    if (auto EC = Writer->writeInteger<uint16_t>(LF_CHAR))
      return EC;
    if (auto EC = Writer->writeInteger<int8_t>(Value))
      return EC;
  } else if (Value >= std::numeric_limits<int16_t>::min() &&
             Value <= std::numeric_limits<int16_t>::max()) {
    if (auto EC = Writer->writeInteger<uint16_t>(LF_SHORT))
      return EC;
    if (auto EC = Writer->writeInteger<int16_t>(Value))
      return EC;
  } else if (Value >= std::numeric_limits<int32_t>::min() &&
             Value <= std::numeric_limits<int32_t>::max()) {
    if (auto EC = Writer->writeInteger<uint16_t>(LF_LONG))
      return EC;
    if (auto EC = Writer->writeInteger<int32_t>(Value))
      return EC;
  } else {
    if (auto EC = Writer->writeInteger<uint16_t>(LF_QUADWORD))
      return EC;
    if (auto EC = Writer->writeInteger(Value))
      return EC;
  }
  return Error::success();
}

Error CodeViewRecordIO::writeEncodedUnsignedInteger(const uint64_t &Value) {
  if (Value < LF_NUMERIC) {
    if (auto EC = Writer->writeInteger<uint16_t>(Value))
      return EC;
  } else if (Value <= std::numeric_limits<uint16_t>::max()) {
    if (auto EC = Writer->writeInteger<uint16_t>(LF_USHORT))
      return EC;
    if (auto EC = Writer->writeInteger<uint16_t>(Value))
      return EC;
  } else if (Value <= std::numeric_limits<uint32_t>::max()) {
    if (auto EC = Writer->writeInteger<uint16_t>(LF_ULONG))
      return EC;
    if (auto EC = Writer->writeInteger<uint32_t>(Value))
      return EC;
  } else {
    if (auto EC = Writer->writeInteger<uint16_t>(LF_UQUADWORD))
      return EC;
    if (auto EC = Writer->writeInteger(Value))
      return EC;
  }

  return Error::success();
}
