//===- BitCodes.h - Enum values for the bitstream format --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines bitstream enum values.
//
// The enum values defined in this file should be considered permanent.  If
// new features are added, they should have values added at the end of the
// respective lists.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITSTREAM_BITCODES_H
#define LLVM_BITSTREAM_BITCODES_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitstream/BitCodeEnums.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

namespace llvm {
/// BitCodeAbbrevOp - This describes one or more operands in an abbreviation.
/// This is actually a union of two different things:
///   1. It could be a literal integer value ("the operand is always 17").
///   2. It could be an encoding specification ("this operand encoded like so").
///
class BitCodeAbbrevOp {
  uint64_t Val;           // A literal value or data for an encoding.
  bool IsLiteral : 1;     // Indicate whether this is a literal value or not.
  unsigned Enc   : 3;     // The encoding to use.
public:
  enum Encoding {
    Fixed = 1,  // A fixed width field, Val specifies number of bits.
    VBR   = 2,  // A VBR field where Val specifies the width of each chunk.
    Array = 3,  // A sequence of fields, next field species elt encoding.
    Char6 = 4,  // A 6-bit fixed field which maps to [a-zA-Z0-9._].
    Blob  = 5   // 32-bit aligned array of 8-bit characters.
  };

  static bool isValidEncoding(uint64_t E) {
    return E >= 1 && E <= 5;
  }

  explicit BitCodeAbbrevOp(uint64_t V) :  Val(V), IsLiteral(true) {}
  explicit BitCodeAbbrevOp(Encoding E, uint64_t Data = 0)
    : Val(Data), IsLiteral(false), Enc(E) {}

  bool isLiteral() const  { return IsLiteral; }
  bool isEncoding() const { return !IsLiteral; }

  // Accessors for literals.
  uint64_t getLiteralValue() const { assert(isLiteral()); return Val; }

  // Accessors for encoding info.
  Encoding getEncoding() const { assert(isEncoding()); return (Encoding)Enc; }
  uint64_t getEncodingData() const {
    assert(isEncoding() && hasEncodingData());
    return Val;
  }

  bool hasEncodingData() const { return hasEncodingData(getEncoding()); }
  static bool hasEncodingData(Encoding E) {
    switch (E) {
    case Fixed:
    case VBR:
      return true;
    case Array:
    case Char6:
    case Blob:
      return false;
    }
    report_fatal_error("Invalid encoding");
  }

  /// isChar6 - Return true if this character is legal in the Char6 encoding.
  static bool isChar6(char C) { return isAlnum(C) || C == '.' || C == '_'; }
  static unsigned EncodeChar6(char C) {
    if (C >= 'a' && C <= 'z') return C-'a';
    if (C >= 'A' && C <= 'Z') return C-'A'+26;
    if (C >= '0' && C <= '9') return C-'0'+26+26;
    if (C == '.')             return 62;
    if (C == '_')             return 63;
    llvm_unreachable("Not a value Char6 character!");
  }

  static char DecodeChar6(unsigned V) {
    assert((V & ~63) == 0 && "Not a Char6 encoded character!");
    return "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._"
        [V];
  }

};

/// BitCodeAbbrev - This class represents an abbreviation record.  An
/// abbreviation allows a complex record that has redundancy to be stored in a
/// specialized format instead of the fully-general, fully-vbr, format.
class BitCodeAbbrev {
  SmallVector<BitCodeAbbrevOp, 32> OperandList;

public:
  BitCodeAbbrev() = default;

  explicit BitCodeAbbrev(std::initializer_list<BitCodeAbbrevOp> OperandList)
      : OperandList(OperandList) {}

  unsigned getNumOperandInfos() const {
    return static_cast<unsigned>(OperandList.size());
  }
  const BitCodeAbbrevOp &getOperandInfo(unsigned N) const {
    return OperandList[N];
  }

  void Add(const BitCodeAbbrevOp &OpInfo) {
    OperandList.push_back(OpInfo);
  }
};
} // namespace llvm

#endif
