//===- llvm/Support/BCD.h - Binary-Coded Decimal utility functions -*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares some utility functions for encoding/decoding BCD values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BCD_H
#define LLVM_SUPPORT_BCD_H

#include <assert.h>
#include <cstddef>
#include <cstdint>

namespace llvm {

// Decode a packed BCD value.
// Maximum value of int64_t is 9,223,372,036,854,775,807. These are 18 usable
// decimal digits. Thus BCD numbers of up to 9 bytes can be converted.
// Please note that s390 supports BCD numbers up to a length of 16 bytes.
inline int64_t decodePackedBCD(const uint8_t *Ptr, size_t ByteLen,
                               bool IsSigned = true) {
  assert(ByteLen >= 1 && ByteLen <= 9 && "Invalid BCD number");
  int64_t Value = 0;
  size_t RunLen = ByteLen - static_cast<unsigned>(IsSigned);
  for (size_t I = 0; I < RunLen; ++I) {
    uint8_t DecodedByteValue = ((Ptr[I] >> 4) & 0x0f) * 10 + (Ptr[I] & 0x0f);
    Value = (Value * 100) + DecodedByteValue;
  }
  if (IsSigned) {
    uint8_t DecodedByteValue = (Ptr[ByteLen - 1] >> 4) & 0x0f;
    uint8_t Sign = Ptr[ByteLen - 1] & 0x0f;
    Value = (Value * 10) + DecodedByteValue;
    if (Sign == 0x0d || Sign == 0x0b)
      Value *= -1;
  }
  return Value;
}

template <typename ResultT, typename ValT>
inline ResultT decodePackedBCD(const ValT Val, bool IsSigned = true) {
  return static_cast<ResultT>(decodePackedBCD(
      reinterpret_cast<const uint8_t *>(&Val), sizeof(ValT), IsSigned));
}

} // namespace llvm

#endif // LLVM_SUPPORT_BCD_H
