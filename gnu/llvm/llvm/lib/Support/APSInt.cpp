//===-- llvm/ADT/APSInt.cpp - Arbitrary Precision Signed Int ---*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the APSInt class, which is a simple class that
// represents an arbitrary sized integer that knows its signedness.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/StringRef.h"
#include <cassert>

using namespace llvm;

APSInt::APSInt(StringRef Str) {
  assert(!Str.empty() && "Invalid string length");

  // (Over-)estimate the required number of bits.
  unsigned NumBits = ((Str.size() * 64) / 19) + 2;
  APInt Tmp(NumBits, Str, /*radix=*/10);
  if (Str[0] == '-') {
    unsigned MinBits = Tmp.getSignificantBits();
    if (MinBits < NumBits)
      Tmp = Tmp.trunc(std::max<unsigned>(1, MinBits));
    *this = APSInt(Tmp, /*isUnsigned=*/false);
    return;
  }
  unsigned ActiveBits = Tmp.getActiveBits();
  if (ActiveBits < NumBits)
    Tmp = Tmp.trunc(std::max<unsigned>(1, ActiveBits));
  *this = APSInt(Tmp, /*isUnsigned=*/true);
}

void APSInt::Profile(FoldingSetNodeID& ID) const {
  ID.AddInteger((unsigned) (IsUnsigned ? 1 : 0));
  APInt::Profile(ID);
}
