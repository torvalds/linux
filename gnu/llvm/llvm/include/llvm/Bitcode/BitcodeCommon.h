//===- BitcodeCommon.h - Common code for encode/decode   --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header defines common code to be used by BitcodeWriter and
// BitcodeReader.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_BITCODE_BITCODECOMMON_H
#define LLVM_BITCODE_BITCODECOMMON_H

#include "llvm/ADT/Bitfields.h"

namespace llvm {

struct AllocaPackedValues {
  // We increased the number of bits needed to represent alignment to be more
  // than 5, but to preserve backward compatibility we store the upper bits
  // separately.
  using AlignLower = Bitfield::Element<unsigned, 0, 5>;
  using UsedWithInAlloca = Bitfield::Element<bool, AlignLower::NextBit, 1>;
  using ExplicitType = Bitfield::Element<bool, UsedWithInAlloca::NextBit, 1>;
  using SwiftError = Bitfield::Element<bool, ExplicitType::NextBit, 1>;
  using AlignUpper = Bitfield::Element<unsigned, SwiftError::NextBit, 3>;
};

} // namespace llvm

#endif // LLVM_BITCODE_BITCODECOMMON_H
