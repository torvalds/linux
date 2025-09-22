//===--- APSIntType.cpp - Simple record of the type of APSInts ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/APSIntType.h"

using namespace clang;
using namespace ento;

APSIntType::RangeTestResultKind
APSIntType::testInRange(const llvm::APSInt &Value,
                        bool AllowSignConversions) const {

  // Negative numbers cannot be losslessly converted to unsigned type.
  if (IsUnsigned && !AllowSignConversions &&
      Value.isSigned() && Value.isNegative())
    return RTR_Below;

  unsigned MinBits;
  if (AllowSignConversions) {
    if (Value.isSigned() && !IsUnsigned)
      MinBits = Value.getSignificantBits();
    else
      MinBits = Value.getActiveBits();

  } else {
    // Signed integers can be converted to signed integers of the same width
    // or (if positive) unsigned integers with one fewer bit.
    // Unsigned integers can be converted to unsigned integers of the same width
    // or signed integers with one more bit.
    if (Value.isSigned())
      MinBits = Value.getSignificantBits() - IsUnsigned;
    else
      MinBits = Value.getActiveBits() + !IsUnsigned;
  }

  if (MinBits <= BitWidth)
    return RTR_Within;

  if (Value.isSigned() && Value.isNegative())
    return RTR_Below;
  else
    return RTR_Above;
}
