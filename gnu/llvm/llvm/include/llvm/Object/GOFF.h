//===- GOFF.h - GOFF object file implementation -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the GOFFObjectFile class.
// Record classes and derivatives are also declared and implemented.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_GOFF_H
#define LLVM_OBJECT_GOFF_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace object {

/// \brief Represents a GOFF physical record.
///
/// Specifies protected member functions to manipulate the record. These should
/// be called from deriving classes to change values as that record specifies.
class Record {
public:
  static Error getContinuousData(const uint8_t *Record, uint16_t DataLength,
                                 int DataIndex, SmallString<256> &CompleteData);

  static bool isContinued(const uint8_t *Record) {
    uint8_t IsContinued;
    getBits(Record, 1, 7, 1, IsContinued);
    return IsContinued;
  }

  static bool isContinuation(const uint8_t *Record) {
    uint8_t IsContinuation;
    getBits(Record, 1, 6, 1, IsContinuation);
    return IsContinuation;
  }

protected:
  /// \brief Get bit field of specified byte.
  ///
  /// Used to pack bit fields into one byte. Fields are packed left to right.
  /// Bit index zero is the most significant bit of the byte.
  ///
  /// \param ByteIndex index of byte the field is in.
  /// \param BitIndex index of first bit of field.
  /// \param Length length of bit field.
  /// \param Value value of bit field.
  static void getBits(const uint8_t *Bytes, uint8_t ByteIndex, uint8_t BitIndex,
                      uint8_t Length, uint8_t &Value) {
    assert(ByteIndex < GOFF::RecordLength && "Byte index out of bounds!");
    assert(BitIndex < 8 && "Bit index out of bounds!");
    assert(Length + BitIndex <= 8 && "Bit length too long!");

    get<uint8_t>(Bytes, ByteIndex, Value);
    Value = (Value >> (8 - BitIndex - Length)) & ((1 << Length) - 1);
  }

  template <class T>
  static void get(const uint8_t *Bytes, uint8_t ByteIndex, T &Value) {
    assert(ByteIndex + sizeof(T) <= GOFF::RecordLength &&
           "Byte index out of bounds!");
    Value = support::endian::read<T, llvm::endianness::big>(&Bytes[ByteIndex]);
  }
};

class TXTRecord : public Record {
public:
  /// \brief Maximum length of data; any more must go in continuation.
  static const uint8_t TXTMaxDataLength = 56;

  static Error getData(const uint8_t *Record, SmallString<256> &CompleteData);

  static void getElementEsdId(const uint8_t *Record, uint32_t &EsdId) {
    get<uint32_t>(Record, 4, EsdId);
  }

  static void getOffset(const uint8_t *Record, uint32_t &Offset) {
    get<uint32_t>(Record, 12, Offset);
  }

  static void getDataLength(const uint8_t *Record, uint16_t &Length) {
    get<uint16_t>(Record, 22, Length);
  }
};

class HDRRecord : public Record {
public:
  static Error getData(const uint8_t *Record, SmallString<256> &CompleteData);

  static uint16_t getPropertyModuleLength(const uint8_t *Record) {
    uint16_t Length;
    get<uint16_t>(Record, 52, Length);
    return Length;
  }
};

class ESDRecord : public Record {
public:
  /// \brief Number of bytes for name; any more must go in continuation.
  /// This is the number of bytes that can fit into the data field of an ESD
  /// record.
  static const uint8_t ESDMaxUncontinuedNameLength = 8;

  /// \brief Maximum name length for ESD records and continuations.
  /// This is the number of bytes that can fit into the data field of an ESD
  /// record AND following continuations. This is limited fundamentally by the
  /// 16 bit SIGNED length field.
  static const uint16_t MaxNameLength = 32 * 1024;

public:
  static Error getData(const uint8_t *Record, SmallString<256> &CompleteData);

  // ESD Get routines.
  static void getSymbolType(const uint8_t *Record,
                            GOFF::ESDSymbolType &SymbolType) {
    uint8_t Value;
    get<uint8_t>(Record, 3, Value);
    SymbolType = (GOFF::ESDSymbolType)Value;
  }

  static void getEsdId(const uint8_t *Record, uint32_t &EsdId) {
    get<uint32_t>(Record, 4, EsdId);
  }

  static void getParentEsdId(const uint8_t *Record, uint32_t &EsdId) {
    get<uint32_t>(Record, 8, EsdId);
  }

  static void getOffset(const uint8_t *Record, uint32_t &Offset) {
    get<uint32_t>(Record, 16, Offset);
  }

  static void getLength(const uint8_t *Record, uint32_t &Length) {
    get<uint32_t>(Record, 24, Length);
  }

  static void getNameSpaceId(const uint8_t *Record, GOFF::ESDNameSpaceId &Id) {
    uint8_t Value;
    get<uint8_t>(Record, 40, Value);
    Id = (GOFF::ESDNameSpaceId)Value;
  }

  static void getFillBytePresent(const uint8_t *Record, bool &Present) {
    uint8_t Value;
    getBits(Record, 41, 0, 1, Value);
    Present = (bool)Value;
  }

  static void getNameMangled(const uint8_t *Record, bool &Mangled) {
    uint8_t Value;
    getBits(Record, 41, 1, 1, Value);
    Mangled = (bool)Value;
  }

  static void getRenamable(const uint8_t *Record, bool &Renamable) {
    uint8_t Value;
    getBits(Record, 41, 2, 1, Value);
    Renamable = (bool)Value;
  }

  static void getRemovable(const uint8_t *Record, bool &Removable) {
    uint8_t Value;
    getBits(Record, 41, 3, 1, Value);
    Removable = (bool)Value;
  }

  static void getFillByteValue(const uint8_t *Record, uint8_t &Fill) {
    get<uint8_t>(Record, 42, Fill);
  }

  static void getAdaEsdId(const uint8_t *Record, uint32_t &EsdId) {
    get<uint32_t>(Record, 44, EsdId);
  }

  static void getSortPriority(const uint8_t *Record, uint32_t &Priority) {
    get<uint32_t>(Record, 48, Priority);
  }

  static void getAmode(const uint8_t *Record, GOFF::ESDAmode &Amode) {
    uint8_t Value;
    get<uint8_t>(Record, 60, Value);
    Amode = (GOFF::ESDAmode)Value;
  }

  static void getRmode(const uint8_t *Record, GOFF::ESDRmode &Rmode) {
    uint8_t Value;
    get<uint8_t>(Record, 61, Value);
    Rmode = (GOFF::ESDRmode)Value;
  }

  static void getTextStyle(const uint8_t *Record, GOFF::ESDTextStyle &Style) {
    uint8_t Value;
    getBits(Record, 62, 0, 4, Value);
    Style = (GOFF::ESDTextStyle)Value;
  }

  static void getBindingAlgorithm(const uint8_t *Record,
                                  GOFF::ESDBindingAlgorithm &Algorithm) {
    uint8_t Value;
    getBits(Record, 62, 4, 4, Value);
    Algorithm = (GOFF::ESDBindingAlgorithm)Value;
  }

  static void getTaskingBehavior(const uint8_t *Record,
                                 GOFF::ESDTaskingBehavior &TaskingBehavior) {
    uint8_t Value;
    getBits(Record, 63, 0, 3, Value);
    TaskingBehavior = (GOFF::ESDTaskingBehavior)Value;
  }

  static void getReadOnly(const uint8_t *Record, bool &ReadOnly) {
    uint8_t Value;
    getBits(Record, 63, 4, 1, Value);
    ReadOnly = (bool)Value;
  }

  static void getExecutable(const uint8_t *Record,
                            GOFF::ESDExecutable &Executable) {
    uint8_t Value;
    getBits(Record, 63, 5, 3, Value);
    Executable = (GOFF::ESDExecutable)Value;
  }

  static void getDuplicateSeverity(const uint8_t *Record,
                                   GOFF::ESDDuplicateSymbolSeverity &DSS) {
    uint8_t Value;
    getBits(Record, 64, 2, 2, Value);
    DSS = (GOFF::ESDDuplicateSymbolSeverity)Value;
  }

  static void getBindingStrength(const uint8_t *Record,
                                 GOFF::ESDBindingStrength &Strength) {
    uint8_t Value;
    getBits(Record, 64, 4, 4, Value);
    Strength = (GOFF::ESDBindingStrength)Value;
  }

  static void getLoadingBehavior(const uint8_t *Record,
                                 GOFF::ESDLoadingBehavior &Behavior) {
    uint8_t Value;
    getBits(Record, 65, 0, 2, Value);
    Behavior = (GOFF::ESDLoadingBehavior)Value;
  }

  static void getIndirectReference(const uint8_t *Record, bool &Indirect) {
    uint8_t Value;
    getBits(Record, 65, 3, 1, Value);
    Indirect = (bool)Value;
  }

  static void getBindingScope(const uint8_t *Record,
                              GOFF::ESDBindingScope &Scope) {
    uint8_t Value;
    getBits(Record, 65, 4, 4, Value);
    Scope = (GOFF::ESDBindingScope)Value;
  }

  static void getLinkageType(const uint8_t *Record,
                             GOFF::ESDLinkageType &Type) {
    uint8_t Value;
    getBits(Record, 66, 2, 1, Value);
    Type = (GOFF::ESDLinkageType)Value;
  }

  static void getAlignment(const uint8_t *Record,
                           GOFF::ESDAlignment &Alignment) {
    uint8_t Value;
    getBits(Record, 66, 3, 5, Value);
    Alignment = (GOFF::ESDAlignment)Value;
  }

  static uint16_t getNameLength(const uint8_t *Record) {
    uint16_t Length;
    get<uint16_t>(Record, 70, Length);
    return Length;
  }
};

class ENDRecord : public Record {
public:
  static Error getData(const uint8_t *Record, SmallString<256> &CompleteData);

  static uint16_t getNameLength(const uint8_t *Record) {
    uint16_t Length;
    get<uint16_t>(Record, 24, Length);
    return Length;
  }
};

} // end namespace object
} // end namespace llvm

#endif
