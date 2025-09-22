//===- CodeViewRecordIO.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CODEVIEWRECORDIO_H
#define LLVM_DEBUGINFO_CODEVIEW_CODEVIEWRECORDIO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace llvm {

template <typename T> class ArrayRef;
class APSInt;

namespace codeview {
class TypeIndex;
struct GUID;

class CodeViewRecordStreamer {
public:
  virtual void emitBytes(StringRef Data) = 0;
  virtual void emitIntValue(uint64_t Value, unsigned Size) = 0;
  virtual void emitBinaryData(StringRef Data) = 0;
  virtual void AddComment(const Twine &T) = 0;
  virtual void AddRawComment(const Twine &T) = 0;
  virtual bool isVerboseAsm() = 0;
  virtual std::string getTypeName(TypeIndex TI) = 0;
  virtual ~CodeViewRecordStreamer() = default;
};

class CodeViewRecordIO {
  uint32_t getCurrentOffset() const {
    if (isWriting())
      return Writer->getOffset();
    else if (isReading())
      return Reader->getOffset();
    else
      return 0;
  }

public:
  // deserializes records to structures
  explicit CodeViewRecordIO(BinaryStreamReader &Reader) : Reader(&Reader) {}

  // serializes records to buffer
  explicit CodeViewRecordIO(BinaryStreamWriter &Writer) : Writer(&Writer) {}

  // writes records to assembly file using MC library interface
  explicit CodeViewRecordIO(CodeViewRecordStreamer &Streamer)
      : Streamer(&Streamer) {}

  Error beginRecord(std::optional<uint32_t> MaxLength);
  Error endRecord();

  Error mapInteger(TypeIndex &TypeInd, const Twine &Comment = "");

  bool isStreaming() const {
    return (Streamer != nullptr) && (Reader == nullptr) && (Writer == nullptr);
  }
  bool isReading() const {
    return (Reader != nullptr) && (Streamer == nullptr) && (Writer == nullptr);
  }
  bool isWriting() const {
    return (Writer != nullptr) && (Streamer == nullptr) && (Reader == nullptr);
  }

  uint32_t maxFieldLength() const;

  template <typename T> Error mapObject(T &Value) {
    if (isStreaming()) {
      StringRef BytesSR =
          StringRef((reinterpret_cast<const char *>(&Value)), sizeof(Value));
      Streamer->emitBytes(BytesSR);
      incrStreamedLen(sizeof(T));
      return Error::success();
    }

    if (isWriting())
      return Writer->writeObject(Value);

    const T *ValuePtr;
    if (auto EC = Reader->readObject(ValuePtr))
      return EC;
    Value = *ValuePtr;
    return Error::success();
  }

  template <typename T> Error mapInteger(T &Value, const Twine &Comment = "") {
    if (isStreaming()) {
      emitComment(Comment);
      Streamer->emitIntValue((int)Value, sizeof(T));
      incrStreamedLen(sizeof(T));
      return Error::success();
    }

    if (isWriting())
      return Writer->writeInteger(Value);

    return Reader->readInteger(Value);
  }

  template <typename T> Error mapEnum(T &Value, const Twine &Comment = "") {
    if (!isStreaming() && sizeof(Value) > maxFieldLength())
      return make_error<CodeViewError>(cv_error_code::insufficient_buffer);

    using U = std::underlying_type_t<T>;
    U X;

    if (isWriting() || isStreaming())
      X = static_cast<U>(Value);

    if (auto EC = mapInteger(X, Comment))
      return EC;

    if (isReading())
      Value = static_cast<T>(X);

    return Error::success();
  }

  Error mapEncodedInteger(int64_t &Value, const Twine &Comment = "");
  Error mapEncodedInteger(uint64_t &Value, const Twine &Comment = "");
  Error mapEncodedInteger(APSInt &Value, const Twine &Comment = "");
  Error mapStringZ(StringRef &Value, const Twine &Comment = "");
  Error mapGuid(GUID &Guid, const Twine &Comment = "");

  Error mapStringZVectorZ(std::vector<StringRef> &Value,
                          const Twine &Comment = "");

  template <typename SizeType, typename T, typename ElementMapper>
  Error mapVectorN(T &Items, const ElementMapper &Mapper,
                   const Twine &Comment = "") {
    SizeType Size;
    if (isStreaming()) {
      Size = static_cast<SizeType>(Items.size());
      emitComment(Comment);
      Streamer->emitIntValue(Size, sizeof(Size));
      incrStreamedLen(sizeof(Size)); // add 1 for the delimiter

      for (auto &X : Items) {
        if (auto EC = Mapper(*this, X))
          return EC;
      }
    } else if (isWriting()) {
      Size = static_cast<SizeType>(Items.size());
      if (auto EC = Writer->writeInteger(Size))
        return EC;

      for (auto &X : Items) {
        if (auto EC = Mapper(*this, X))
          return EC;
      }
    } else {
      if (auto EC = Reader->readInteger(Size))
        return EC;
      for (SizeType I = 0; I < Size; ++I) {
        typename T::value_type Item;
        if (auto EC = Mapper(*this, Item))
          return EC;
        Items.push_back(Item);
      }
    }

    return Error::success();
  }

  template <typename T, typename ElementMapper>
  Error mapVectorTail(T &Items, const ElementMapper &Mapper,
                      const Twine &Comment = "") {
    emitComment(Comment);
    if (isStreaming() || isWriting()) {
      for (auto &Item : Items) {
        if (auto EC = Mapper(*this, Item))
          return EC;
      }
    } else {
      typename T::value_type Field;
      // Stop when we run out of bytes or we hit record padding bytes.
      while (!Reader->empty() && Reader->peek() < 0xf0 /* LF_PAD0 */) {
        if (auto EC = Mapper(*this, Field))
          return EC;
        Items.push_back(Field);
      }
    }
    return Error::success();
  }

  Error mapByteVectorTail(ArrayRef<uint8_t> &Bytes, const Twine &Comment = "");
  Error mapByteVectorTail(std::vector<uint8_t> &Bytes,
                          const Twine &Comment = "");

  Error padToAlignment(uint32_t Align);
  Error skipPadding();

  uint64_t getStreamedLen() {
    if (isStreaming())
      return StreamedLen;
    return 0;
  }

  void emitRawComment(const Twine &T) {
    if (isStreaming() && Streamer->isVerboseAsm())
      Streamer->AddRawComment(T);
  }

private:
  void emitEncodedSignedInteger(const int64_t &Value,
                                const Twine &Comment = "");
  void emitEncodedUnsignedInteger(const uint64_t &Value,
                                  const Twine &Comment = "");
  Error writeEncodedSignedInteger(const int64_t &Value);
  Error writeEncodedUnsignedInteger(const uint64_t &Value);

  void incrStreamedLen(const uint64_t &Len) {
    if (isStreaming())
      StreamedLen += Len;
  }

  void resetStreamedLen() {
    if (isStreaming())
      StreamedLen = 4; // The record prefix is 4 bytes long
  }

  void emitComment(const Twine &Comment) {
    if (isStreaming() && Streamer->isVerboseAsm()) {
      Twine TComment(Comment);
      if (!TComment.isTriviallyEmpty())
        Streamer->AddComment(TComment);
    }
  }

  struct RecordLimit {
    uint32_t BeginOffset;
    std::optional<uint32_t> MaxLength;

    std::optional<uint32_t> bytesRemaining(uint32_t CurrentOffset) const {
      if (!MaxLength)
        return std::nullopt;
      assert(CurrentOffset >= BeginOffset);

      uint32_t BytesUsed = CurrentOffset - BeginOffset;
      if (BytesUsed >= *MaxLength)
        return 0;
      return *MaxLength - BytesUsed;
    }
  };

  SmallVector<RecordLimit, 2> Limits;

  BinaryStreamReader *Reader = nullptr;
  BinaryStreamWriter *Writer = nullptr;
  CodeViewRecordStreamer *Streamer = nullptr;
  uint64_t StreamedLen = 0;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_CODEVIEWRECORDIO_H
