//===- YAML.cpp - YAMLIO utilities for object files -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines utility classes for handling the YAML representation of
// object files.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/YAML.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cctype>
#include <cstdint>

using namespace llvm;

void yaml::ScalarTraits<yaml::BinaryRef>::output(
    const yaml::BinaryRef &Val, void *, raw_ostream &Out) {
  Val.writeAsHex(Out);
}

StringRef yaml::ScalarTraits<yaml::BinaryRef>::input(StringRef Scalar, void *,
                                                     yaml::BinaryRef &Val) {
  if (Scalar.size() % 2 != 0)
    return "BinaryRef hex string must contain an even number of nybbles.";
  // TODO: Can we improve YAMLIO to permit a more accurate diagnostic here?
  // (e.g. a caret pointing to the offending character).
  if (!llvm::all_of(Scalar, llvm::isHexDigit))
    return "BinaryRef hex string must contain only hex digits.";
  Val = yaml::BinaryRef(Scalar);
  return {};
}

void yaml::BinaryRef::writeAsBinary(raw_ostream &OS, uint64_t N) const {
  if (!DataIsHexString) {
    OS.write((const char *)Data.data(), std::min<uint64_t>(N, Data.size()));
    return;
  }

  for (uint64_t I = 0, E = std::min<uint64_t>(N, Data.size() / 2); I != E;
       ++I) {
    uint8_t Byte = llvm::hexDigitValue(Data[I * 2]);
    Byte <<= 4;
    Byte |= llvm::hexDigitValue(Data[I * 2 + 1]);
    OS.write(Byte);
  }
}

void yaml::BinaryRef::writeAsHex(raw_ostream &OS) const {
  if (binary_size() == 0)
    return;
  if (DataIsHexString) {
    OS.write((const char *)Data.data(), Data.size());
    return;
  }
  for (uint8_t Byte : Data)
    OS << hexdigit(Byte >> 4) << hexdigit(Byte & 0xf);
}
