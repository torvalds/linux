//===- yaml2goff - Convert YAML to a GOFF object file ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The GOFF component of yaml2obj.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/IndexedMap.h"
#include "llvm/ObjectYAML/ObjectYAML.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/ConvertEBCDIC.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

// Common flag values on records.
enum {
  // Flag: This record is continued.
  Rec_Continued = 1,

  // Flag: This record is a continuation.
  Rec_Continuation = 1 << (8 - 6 - 1),
};

template <typename ValueType> struct BinaryBeImpl {
  ValueType Value;
  BinaryBeImpl(ValueType V) : Value(V) {}
};

template <typename ValueType>
raw_ostream &operator<<(raw_ostream &OS, const BinaryBeImpl<ValueType> &BBE) {
  char Buffer[sizeof(BBE.Value)];
  support::endian::write<ValueType, llvm::endianness::big, support::unaligned>(
      Buffer, BBE.Value);
  OS.write(Buffer, sizeof(BBE.Value));
  return OS;
}

template <typename ValueType> BinaryBeImpl<ValueType> binaryBe(ValueType V) {
  return BinaryBeImpl<ValueType>(V);
}

struct ZerosImpl {
  size_t NumBytes;
};

raw_ostream &operator<<(raw_ostream &OS, const ZerosImpl &Z) {
  OS.write_zeros(Z.NumBytes);
  return OS;
}

ZerosImpl zeros(const size_t NumBytes) { return ZerosImpl{NumBytes}; }

// The GOFFOstream is responsible to write the data into the fixed physical
// records of the format. A user of this class announces the start of a new
// logical record and the size of its payload. While writing the payload, the
// physical records are created for the data. Possible fill bytes at the end of
// a physical record are written automatically.
class GOFFOstream : public raw_ostream {
public:
  explicit GOFFOstream(raw_ostream &OS)
      : OS(OS), LogicalRecords(0), RemainingSize(0), NewLogicalRecord(false) {
    SetBufferSize(GOFF::PayloadLength);
  }

  ~GOFFOstream() { finalize(); }

  void makeNewRecord(GOFF::RecordType Type, size_t Size) {
    fillRecord();
    CurrentType = Type;
    RemainingSize = Size;
    if (size_t Gap = (RemainingSize % GOFF::PayloadLength))
      RemainingSize += GOFF::PayloadLength - Gap;
    NewLogicalRecord = true;
    ++LogicalRecords;
  }

  void finalize() { fillRecord(); }

  uint32_t logicalRecords() { return LogicalRecords; }

private:
  // The underlying raw_ostream.
  raw_ostream &OS;

  // The number of logical records emitted so far.
  uint32_t LogicalRecords;

  // The remaining size of this logical record, including fill bytes.
  size_t RemainingSize;

  // The type of the current (logical) record.
  GOFF::RecordType CurrentType;

  // Signals start of new record.
  bool NewLogicalRecord;

  // Return the number of bytes left to write until next physical record.
  // Please note that we maintain the total number of bytes left, not the
  // written size.
  size_t bytesToNextPhysicalRecord() {
    size_t Bytes = RemainingSize % GOFF::PayloadLength;
    return Bytes ? Bytes : GOFF::PayloadLength;
  }

  // Write the record prefix of a physical record, using the current record
  // type.
  static void writeRecordPrefix(raw_ostream &OS, GOFF::RecordType Type,
                                size_t RemainingSize,
                                uint8_t Flags = Rec_Continuation) {
    uint8_t TypeAndFlags = Flags | (Type << 4);
    if (RemainingSize > GOFF::RecordLength)
      TypeAndFlags |= Rec_Continued;
    OS << binaryBe(static_cast<unsigned char>(GOFF::PTVPrefix))
       << binaryBe(static_cast<unsigned char>(TypeAndFlags))
       << binaryBe(static_cast<unsigned char>(0));
  }

  // Fill the last physical record of a logical record with zero bytes.
  void fillRecord() {
    assert((GetNumBytesInBuffer() <= RemainingSize) &&
           "More bytes in buffer than expected");
    size_t Remains = RemainingSize - GetNumBytesInBuffer();
    if (Remains) {
      assert((Remains < GOFF::RecordLength) &&
             "Attempting to fill more than one physical record");
      raw_ostream::write_zeros(Remains);
    }
    flush();
    assert(RemainingSize == 0 && "Not fully flushed");
    assert(GetNumBytesInBuffer() == 0 && "Buffer not fully empty");
  }

  // See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override {
    assert((RemainingSize >= Size) && "Attempt to write too much data");
    assert(RemainingSize && "Logical record overflow");
    if (!(RemainingSize % GOFF::PayloadLength)) {
      writeRecordPrefix(OS, CurrentType, RemainingSize,
                        NewLogicalRecord ? 0 : Rec_Continuation);
      NewLogicalRecord = false;
    }
    assert(!NewLogicalRecord &&
           "New logical record not on physical record boundary");

    size_t Idx = 0;
    while (Size > 0) {
      size_t BytesToWrite = bytesToNextPhysicalRecord();
      if (BytesToWrite > Size)
        BytesToWrite = Size;
      OS.write(Ptr + Idx, BytesToWrite);
      Idx += BytesToWrite;
      Size -= BytesToWrite;
      RemainingSize -= BytesToWrite;
      if (Size) {
        writeRecordPrefix(OS, CurrentType, RemainingSize);
      }
    }
  }

  // Return the current position within the stream, not counting the bytes
  // currently in the buffer.
  uint64_t current_pos() const override { return OS.tell(); }
};

class GOFFState {
  void writeHeader(GOFFYAML::FileHeader &FileHdr);
  void writeEnd();

  void reportError(const Twine &Msg) {
    ErrHandler(Msg);
    HasError = true;
  }

  GOFFState(raw_ostream &OS, GOFFYAML::Object &Doc,
            yaml::ErrorHandler ErrHandler)
      : GW(OS), Doc(Doc), ErrHandler(ErrHandler), HasError(false) {}

  ~GOFFState() { GW.finalize(); }

  bool writeObject();

public:
  static bool writeGOFF(raw_ostream &OS, GOFFYAML::Object &Doc,
                        yaml::ErrorHandler ErrHandler);

private:
  GOFFOstream GW;
  GOFFYAML::Object &Doc;
  yaml::ErrorHandler ErrHandler;
  bool HasError;
};

void GOFFState::writeHeader(GOFFYAML::FileHeader &FileHdr) {
  SmallString<16> CCSIDName;
  if (std::error_code EC =
          ConverterEBCDIC::convertToEBCDIC(FileHdr.CharacterSetName, CCSIDName))
    reportError("Conversion error on " + FileHdr.CharacterSetName);
  if (CCSIDName.size() > 16) {
    reportError("CharacterSetName too long");
    CCSIDName.resize(16);
  }
  SmallString<16> LangProd;
  if (std::error_code EC = ConverterEBCDIC::convertToEBCDIC(
          FileHdr.LanguageProductIdentifier, LangProd))
    reportError("Conversion error on " + FileHdr.LanguageProductIdentifier);
  if (LangProd.size() > 16) {
    reportError("LanguageProductIdentifier too long");
    LangProd.resize(16);
  }

  GW.makeNewRecord(GOFF::RT_HDR, GOFF::PayloadLength);
  GW << binaryBe(FileHdr.TargetEnvironment)     // TargetEnvironment
     << binaryBe(FileHdr.TargetOperatingSystem) // TargetOperatingSystem
     << zeros(2)                                // Reserved
     << binaryBe(FileHdr.CCSID)                 // CCSID
     << CCSIDName                               // CharacterSetName
     << zeros(16 - CCSIDName.size())            // Fill bytes
     << LangProd                                // LanguageProductIdentifier
     << zeros(16 - LangProd.size())             // Fill bytes
     << binaryBe(FileHdr.ArchitectureLevel);    // ArchitectureLevel
  // The module propties are optional. Figure out if we need to write them.
  uint16_t ModPropLen = 0;
  if (FileHdr.TargetSoftwareEnvironment)
    ModPropLen = 3;
  else if (FileHdr.InternalCCSID)
    ModPropLen = 2;
  if (ModPropLen) {
    GW << binaryBe(ModPropLen) << zeros(6);
    if (ModPropLen >= 2)
      GW << binaryBe(FileHdr.InternalCCSID ? *FileHdr.InternalCCSID : 0);
    if (ModPropLen >= 3)
      GW << binaryBe(FileHdr.TargetSoftwareEnvironment
                         ? *FileHdr.TargetSoftwareEnvironment
                         : 0);
  }
}

void GOFFState::writeEnd() {
  GW.makeNewRecord(GOFF::RT_END, GOFF::PayloadLength);
  GW << binaryBe(uint8_t(0)) // No entry point
     << binaryBe(uint8_t(0)) // No AMODE
     << zeros(3)             // Reserved
     << binaryBe(GW.logicalRecords());
  // No entry point yet. Automatically fill remaining space with zero bytes.
  GW.finalize();
}

bool GOFFState::writeObject() {
  writeHeader(Doc.Header);
  if (HasError)
    return false;
  writeEnd();
  return true;
}

bool GOFFState::writeGOFF(raw_ostream &OS, GOFFYAML::Object &Doc,
                          yaml::ErrorHandler ErrHandler) {
  GOFFState State(OS, Doc, ErrHandler);
  return State.writeObject();
}
} // namespace

namespace llvm {
namespace yaml {

bool yaml2goff(llvm::GOFFYAML::Object &Doc, raw_ostream &Out,
               ErrorHandler ErrHandler) {
  return GOFFState::writeGOFF(Out, Doc, ErrHandler);
}

} // namespace yaml
} // namespace llvm
