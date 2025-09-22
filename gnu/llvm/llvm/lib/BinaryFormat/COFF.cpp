//===- llvm/BinaryFormat/COFF.cpp - The COFF format -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"

// Maximum offsets for different string table entry encodings.
enum : unsigned { Max7DecimalOffset = 9999999U };
enum : uint64_t { MaxBase64Offset = 0xFFFFFFFFFULL }; // 64^6, including 0

// Encode a string table entry offset in base 64, padded to 6 chars, and
// prefixed with a double slash: '//AAAAAA', '//AAAAAB', ...
// Buffer must be at least 8 bytes large. No terminating null appended.
static void encodeBase64StringEntry(char *Buffer, uint64_t Value) {
  assert(Value > Max7DecimalOffset && Value <= MaxBase64Offset &&
         "Illegal section name encoding for value");

  static const char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                 "abcdefghijklmnopqrstuvwxyz"
                                 "0123456789+/";

  Buffer[0] = '/';
  Buffer[1] = '/';

  char *Ptr = Buffer + 7;
  for (unsigned i = 0; i < 6; ++i) {
    unsigned Rem = Value % 64;
    Value /= 64;
    *(Ptr--) = Alphabet[Rem];
  }
}

bool llvm::COFF::encodeSectionName(char *Out, uint64_t Offset) {
  if (Offset <= Max7DecimalOffset) {
    // Offsets of 7 digits or less are encoded in ASCII.
    SmallVector<char, COFF::NameSize> Buffer;
    Twine('/').concat(Twine(Offset)).toVector(Buffer);
    assert(Buffer.size() <= COFF::NameSize && Buffer.size() >= 2);
    std::memcpy(Out, Buffer.data(), Buffer.size());
    return true;
  }

  if (Offset <= MaxBase64Offset) {
    // Starting with 10,000,000, offsets are encoded as base64.
    encodeBase64StringEntry(Out, Offset);
    return true;
  }

  // The offset is too large to be encoded.
  return false;
}
