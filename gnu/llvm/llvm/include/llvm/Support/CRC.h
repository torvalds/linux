//===-- llvm/Support/CRC.h - Cyclic Redundancy Check-------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains implementations of CRC functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CRC_H
#define LLVM_SUPPORT_CRC_H

#include "llvm/Support/DataTypes.h"

namespace llvm {
template <typename T> class ArrayRef;

// Compute the CRC-32 of Data.
uint32_t crc32(ArrayRef<uint8_t> Data);

// Compute the running CRC-32 of Data, with CRC being the previous value of the
// checksum.
uint32_t crc32(uint32_t CRC, ArrayRef<uint8_t> Data);

// Class for computing the JamCRC.
//
// We will use the "Rocksoft^tm Model CRC Algorithm" to describe the properties
// of this CRC:
//   Width  : 32
//   Poly   : 04C11DB7
//   Init   : FFFFFFFF
//   RefIn  : True
//   RefOut : True
//   XorOut : 00000000
//   Check  : 340BC6D9 (result of CRC for "123456789")
//
// In other words, this is the same as CRC-32, except that XorOut is 0 instead
// of FFFFFFFF.
//
// N.B.  We permit flexibility of the "Init" value.  Some consumers of this need
//       it to be zero.
class JamCRC {
public:
  JamCRC(uint32_t Init = 0xFFFFFFFFU) : CRC(Init) {}

  // Update the CRC calculation with Data.
  void update(ArrayRef<uint8_t> Data);

  uint32_t getCRC() const { return CRC; }

private:
  uint32_t CRC;
};

} // end namespace llvm

#endif
