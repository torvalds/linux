//====--------------- lib/Support/BlockFrequency.cpp -----------*- C++ -*-====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Block Frequency class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ScaledNumber.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

BlockFrequency &BlockFrequency::operator*=(BranchProbability Prob) {
  Frequency = Prob.scale(Frequency);
  return *this;
}

BlockFrequency BlockFrequency::operator*(BranchProbability Prob) const {
  BlockFrequency Freq(Frequency);
  Freq *= Prob;
  return Freq;
}

BlockFrequency &BlockFrequency::operator/=(BranchProbability Prob) {
  Frequency = Prob.scaleByInverse(Frequency);
  return *this;
}

BlockFrequency BlockFrequency::operator/(BranchProbability Prob) const {
  BlockFrequency Freq(Frequency);
  Freq /= Prob;
  return Freq;
}

std::optional<BlockFrequency> BlockFrequency::mul(uint64_t Factor) const {
  bool Overflow;
  uint64_t ResultFrequency = SaturatingMultiply(Frequency, Factor, &Overflow);
  if (Overflow)
    return {};
  return BlockFrequency(ResultFrequency);
}

void llvm::printRelativeBlockFreq(raw_ostream &OS, BlockFrequency EntryFreq,
                                  BlockFrequency Freq) {
  if (Freq == BlockFrequency(0)) {
    OS << "0";
    return;
  }
  if (EntryFreq == BlockFrequency(0)) {
    OS << "<invalid BFI>";
    return;
  }
  ScaledNumber<uint64_t> Block(Freq.getFrequency(), 0);
  ScaledNumber<uint64_t> Entry(EntryFreq.getFrequency(), 0);
  OS << Block / Entry;
}
