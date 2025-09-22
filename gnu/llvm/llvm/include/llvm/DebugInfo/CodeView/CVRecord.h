//===- CVRecord.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CVRECORD_H
#define LLVM_DEBUGINFO_CODEVIEW_CVRECORD_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

namespace codeview {

/// CVRecord is a fat pointer (base + size pair) to a symbol or type record.
/// Carrying the size separately instead of trusting the size stored in the
/// record prefix provides some extra safety and flexibility.
template <typename Kind> class CVRecord {
public:
  CVRecord() = default;

  CVRecord(ArrayRef<uint8_t> Data) : RecordData(Data) {}

  CVRecord(const RecordPrefix *P, size_t Size)
      : RecordData(reinterpret_cast<const uint8_t *>(P), Size) {}

  bool valid() const { return kind() != Kind(0); }

  uint32_t length() const { return RecordData.size(); }

  Kind kind() const {
    if (RecordData.size() < sizeof(RecordPrefix))
      return Kind(0);
    return static_cast<Kind>(static_cast<uint16_t>(
        reinterpret_cast<const RecordPrefix *>(RecordData.data())->RecordKind));
  }

  ArrayRef<uint8_t> data() const { return RecordData; }

  StringRef str_data() const {
    return StringRef(reinterpret_cast<const char *>(RecordData.data()),
                     RecordData.size());
  }

  ArrayRef<uint8_t> content() const {
    return RecordData.drop_front(sizeof(RecordPrefix));
  }

  ArrayRef<uint8_t> RecordData;
};

// There are two kinds of codeview records: type and symbol records.
using CVType = CVRecord<TypeLeafKind>;
using CVSymbol = CVRecord<SymbolKind>;

template <typename Record, typename Func>
Error forEachCodeViewRecord(ArrayRef<uint8_t> StreamBuffer, Func F) {
  while (!StreamBuffer.empty()) {
    if (StreamBuffer.size() < sizeof(RecordPrefix))
      return make_error<CodeViewError>(cv_error_code::corrupt_record);

    const RecordPrefix *Prefix =
        reinterpret_cast<const RecordPrefix *>(StreamBuffer.data());

    size_t RealLen = Prefix->RecordLen + 2;
    if (StreamBuffer.size() < RealLen)
      return make_error<CodeViewError>(cv_error_code::corrupt_record);

    ArrayRef<uint8_t> Data = StreamBuffer.take_front(RealLen);
    StreamBuffer = StreamBuffer.drop_front(RealLen);

    Record R(Data);
    if (auto EC = F(R))
      return EC;
  }
  return Error::success();
}

/// Read a complete record from a stream at a random offset.
template <typename Kind>
inline Expected<CVRecord<Kind>> readCVRecordFromStream(BinaryStreamRef Stream,
                                                       uint32_t Offset) {
  const RecordPrefix *Prefix = nullptr;
  BinaryStreamReader Reader(Stream);
  Reader.setOffset(Offset);

  if (auto EC = Reader.readObject(Prefix))
    return std::move(EC);
  if (Prefix->RecordLen < 2)
    return make_error<CodeViewError>(cv_error_code::corrupt_record);

  Reader.setOffset(Offset);
  ArrayRef<uint8_t> RawData;
  if (auto EC = Reader.readBytes(RawData, Prefix->RecordLen + sizeof(uint16_t)))
    return std::move(EC);
  return codeview::CVRecord<Kind>(RawData);
}

} // end namespace codeview

template <typename Kind>
struct VarStreamArrayExtractor<codeview::CVRecord<Kind>> {
  Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                   codeview::CVRecord<Kind> &Item) {
    auto ExpectedRec = codeview::readCVRecordFromStream<Kind>(Stream, 0);
    if (!ExpectedRec)
      return ExpectedRec.takeError();
    Item = *ExpectedRec;
    Len = ExpectedRec->length();
    return Error::success();
  }
};

namespace codeview {
using CVSymbolArray = VarStreamArray<CVSymbol>;
using CVTypeArray = VarStreamArray<CVType>;
using CVTypeRange = iterator_range<CVTypeArray::Iterator>;
} // namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_CVRECORD_H
