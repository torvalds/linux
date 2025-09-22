//===- lib/MC/GOFFObjectWriter.cpp - GOFF File Writer ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements GOFF object file writer information.
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCGOFFObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "goff-writer"

namespace {

// The standard System/390 convention is to name the high-order (leftmost) bit
// in a byte as bit zero. The Flags type helps to set bits in a byte according
// to this numeration order.
class Flags {
  uint8_t Val;

  constexpr static uint8_t bits(uint8_t BitIndex, uint8_t Length, uint8_t Value,
                                uint8_t OldValue) {
    assert(BitIndex < 8 && "Bit index out of bounds!");
    assert(Length + BitIndex <= 8 && "Bit length too long!");

    uint8_t Mask = ((1 << Length) - 1) << (8 - BitIndex - Length);
    Value = Value << (8 - BitIndex - Length);
    assert((Value & Mask) == Value && "Bits set outside of range!");

    return (OldValue & ~Mask) | Value;
  }

public:
  constexpr Flags() : Val(0) {}
  constexpr Flags(uint8_t BitIndex, uint8_t Length, uint8_t Value)
      : Val(bits(BitIndex, Length, Value, 0)) {}

  void set(uint8_t BitIndex, uint8_t Length, uint8_t Value) {
    Val = bits(BitIndex, Length, Value, Val);
  }

  constexpr operator uint8_t() const { return Val; }
};

// Common flag values on records.

// Flag: This record is continued.
constexpr uint8_t RecContinued = Flags(7, 1, 1);

// Flag: This record is a continuation.
constexpr uint8_t RecContinuation = Flags(6, 1, 1);

// The GOFFOstream is responsible to write the data into the fixed physical
// records of the format. A user of this class announces the start of a new
// logical record and the size of its content. While writing the content, the
// physical records are created for the data. Possible fill bytes at the end of
// a physical record are written automatically. In principle, the GOFFOstream
// is agnostic of the endianness of the content. However, it also supports
// writing data in big endian byte order.
class GOFFOstream : public raw_ostream {
  /// The underlying raw_pwrite_stream.
  raw_pwrite_stream &OS;

  /// The remaining size of this logical record, including fill bytes.
  size_t RemainingSize;

#ifndef NDEBUG
  /// The number of bytes needed to fill up the last physical record.
  size_t Gap = 0;
#endif

  /// The number of logical records emitted to far.
  uint32_t LogicalRecords;

  /// The type of the current (logical) record.
  GOFF::RecordType CurrentType;

  /// Signals start of new record.
  bool NewLogicalRecord;

  /// Static allocated buffer for the stream, used by the raw_ostream class. The
  /// buffer is sized to hold the content of a physical record.
  char Buffer[GOFF::RecordContentLength];

  // Return the number of bytes left to write until next physical record.
  // Please note that we maintain the total numbers of byte left, not the
  // written size.
  size_t bytesToNextPhysicalRecord() {
    size_t Bytes = RemainingSize % GOFF::RecordContentLength;
    return Bytes ? Bytes : GOFF::RecordContentLength;
  }

  /// Write the record prefix of a physical record, using the given record type.
  static void writeRecordPrefix(raw_ostream &OS, GOFF::RecordType Type,
                                size_t RemainingSize,
                                uint8_t Flags = RecContinuation);

  /// Fill the last physical record of a logical record with zero bytes.
  void fillRecord();

  /// See raw_ostream::write_impl.
  void write_impl(const char *Ptr, size_t Size) override;

  /// Return the current position within the stream, not counting the bytes
  /// currently in the buffer.
  uint64_t current_pos() const override { return OS.tell(); }

public:
  explicit GOFFOstream(raw_pwrite_stream &OS)
      : OS(OS), RemainingSize(0), LogicalRecords(0), NewLogicalRecord(false) {
    SetBuffer(Buffer, sizeof(Buffer));
  }

  ~GOFFOstream() { finalize(); }

  raw_pwrite_stream &getOS() { return OS; }

  void newRecord(GOFF::RecordType Type, size_t Size);

  void finalize() { fillRecord(); }

  uint32_t logicalRecords() { return LogicalRecords; }

  // Support for endian-specific data.
  template <typename value_type> void writebe(value_type Value) {
    Value =
        support::endian::byte_swap<value_type>(Value, llvm::endianness::big);
    write(reinterpret_cast<const char *>(&Value), sizeof(value_type));
  }
};

void GOFFOstream::writeRecordPrefix(raw_ostream &OS, GOFF::RecordType Type,
                                    size_t RemainingSize, uint8_t Flags) {
  uint8_t TypeAndFlags = Flags | (Type << 4);
  if (RemainingSize > GOFF::RecordLength)
    TypeAndFlags |= RecContinued;
  OS << static_cast<unsigned char>(GOFF::PTVPrefix) // Record Type
     << static_cast<unsigned char>(TypeAndFlags)    // Continuation
     << static_cast<unsigned char>(0);              // Version
}

void GOFFOstream::newRecord(GOFF::RecordType Type, size_t Size) {
  fillRecord();
  CurrentType = Type;
  RemainingSize = Size;
#ifdef NDEBUG
  size_t Gap;
#endif
  Gap = (RemainingSize % GOFF::RecordContentLength);
  if (Gap) {
    Gap = GOFF::RecordContentLength - Gap;
    RemainingSize += Gap;
  }
  NewLogicalRecord = true;
  ++LogicalRecords;
}

void GOFFOstream::fillRecord() {
  assert((GetNumBytesInBuffer() <= RemainingSize) &&
         "More bytes in buffer than expected");
  size_t Remains = RemainingSize - GetNumBytesInBuffer();
  if (Remains) {
    assert(Remains == Gap && "Wrong size of fill gap");
    assert((Remains < GOFF::RecordLength) &&
           "Attempt to fill more than one physical record");
    raw_ostream::write_zeros(Remains);
  }
  flush();
  assert(RemainingSize == 0 && "Not fully flushed");
  assert(GetNumBytesInBuffer() == 0 && "Buffer not fully empty");
}

// This function is called from the raw_ostream implementation if:
// - The internal buffer is full. Size is excactly the size of the buffer.
// - Data larger than the internal buffer is written. Size is a multiple of the
//   buffer size.
// - flush() has been called. Size is at most the buffer size.
// The GOFFOstream implementation ensures that flush() is called before a new
// logical record begins. Therefore it is sufficient to check for a new block
// only once.
void GOFFOstream::write_impl(const char *Ptr, size_t Size) {
  assert((RemainingSize >= Size) && "Attempt to write too much data");
  assert(RemainingSize && "Logical record overflow");
  if (!(RemainingSize % GOFF::RecordContentLength)) {
    writeRecordPrefix(OS, CurrentType, RemainingSize,
                      NewLogicalRecord ? 0 : RecContinuation);
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
    if (Size)
      writeRecordPrefix(OS, CurrentType, RemainingSize);
  }
}

class GOFFObjectWriter : public MCObjectWriter {
  // The target specific GOFF writer instance.
  std::unique_ptr<MCGOFFObjectTargetWriter> TargetObjectWriter;

  // The stream used to write the GOFF records.
  GOFFOstream OS;

public:
  GOFFObjectWriter(std::unique_ptr<MCGOFFObjectTargetWriter> MOTW,
                   raw_pwrite_stream &OS)
      : TargetObjectWriter(std::move(MOTW)), OS(OS) {}

  ~GOFFObjectWriter() override {}

  // Write GOFF records.
  void writeHeader();
  void writeEnd();

  // Implementation of the MCObjectWriter interface.
  void recordRelocation(MCAssembler &Asm, const MCFragment *Fragment,
                        const MCFixup &Fixup, MCValue Target,
                        uint64_t &FixedValue) override {}
  uint64_t writeObject(MCAssembler &Asm) override;
};
} // end anonymous namespace

void GOFFObjectWriter::writeHeader() {
  OS.newRecord(GOFF::RT_HDR, /*Size=*/57);
  OS.write_zeros(1);       // Reserved
  OS.writebe<uint32_t>(0); // Target Hardware Environment
  OS.writebe<uint32_t>(0); // Target Operating System Environment
  OS.write_zeros(2);       // Reserved
  OS.writebe<uint16_t>(0); // CCSID
  OS.write_zeros(16);      // Character Set name
  OS.write_zeros(16);      // Language Product Identifier
  OS.writebe<uint32_t>(1); // Architecture Level
  OS.writebe<uint16_t>(0); // Module Properties Length
  OS.write_zeros(6);       // Reserved
}

void GOFFObjectWriter::writeEnd() {
  uint8_t F = GOFF::END_EPR_None;
  uint8_t AMODE = 0;
  uint32_t ESDID = 0;

  // TODO Set Flags/AMODE/ESDID for entry point.

  OS.newRecord(GOFF::RT_END, /*Size=*/13);
  OS.writebe<uint8_t>(Flags(6, 2, F)); // Indicator flags
  OS.writebe<uint8_t>(AMODE);          // AMODE
  OS.write_zeros(3);                   // Reserved
  // The record count is the number of logical records. In principle, this value
  // is available as OS.logicalRecords(). However, some tools rely on this field
  // being zero.
  OS.writebe<uint32_t>(0);     // Record Count
  OS.writebe<uint32_t>(ESDID); // ESDID (of entry point)
  OS.finalize();
}

uint64_t GOFFObjectWriter::writeObject(MCAssembler &Asm) {
  uint64_t StartOffset = OS.tell();

  writeHeader();
  writeEnd();

  LLVM_DEBUG(dbgs() << "Wrote " << OS.logicalRecords() << " logical records.");

  return OS.tell() - StartOffset;
}

std::unique_ptr<MCObjectWriter>
llvm::createGOFFObjectWriter(std::unique_ptr<MCGOFFObjectTargetWriter> MOTW,
                             raw_pwrite_stream &OS) {
  return std::make_unique<GOFFObjectWriter>(std::move(MOTW), OS);
}
