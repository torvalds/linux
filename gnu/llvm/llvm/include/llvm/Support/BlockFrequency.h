//===-------- BlockFrequency.h - Block Frequency Wrapper --------*- C++ -*-===//
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

#ifndef LLVM_SUPPORT_BLOCKFREQUENCY_H
#define LLVM_SUPPORT_BLOCKFREQUENCY_H

#include <cassert>
#include <cstdint>
#include <optional>

namespace llvm {

class raw_ostream;
class BranchProbability;

// This class represents Block Frequency as a 64-bit value.
class BlockFrequency {
  uint64_t Frequency;

public:
  BlockFrequency() : Frequency(0) {}
  explicit BlockFrequency(uint64_t Freq) : Frequency(Freq) {}

  /// Returns the maximum possible frequency, the saturation value.
  static BlockFrequency max() { return BlockFrequency(UINT64_MAX); }

  /// Returns the frequency as a fixpoint number scaled by the entry
  /// frequency.
  uint64_t getFrequency() const { return Frequency; }

  /// Multiplies with a branch probability. The computation will never
  /// overflow.
  BlockFrequency &operator*=(BranchProbability Prob);
  BlockFrequency operator*(BranchProbability Prob) const;

  /// Divide by a non-zero branch probability using saturating
  /// arithmetic.
  BlockFrequency &operator/=(BranchProbability Prob);
  BlockFrequency operator/(BranchProbability Prob) const;

  /// Adds another block frequency using saturating arithmetic.
  BlockFrequency &operator+=(BlockFrequency Freq) {
    uint64_t Before = Freq.Frequency;
    Frequency += Freq.Frequency;

    // If overflow, set frequency to the maximum value.
    if (Frequency < Before)
      Frequency = UINT64_MAX;

    return *this;
  }
  BlockFrequency operator+(BlockFrequency Freq) const {
    BlockFrequency NewFreq(Frequency);
    NewFreq += Freq;
    return NewFreq;
  }

  /// Subtracts another block frequency using saturating arithmetic.
  BlockFrequency &operator-=(BlockFrequency Freq) {
    // If underflow, set frequency to 0.
    if (Frequency <= Freq.Frequency)
      Frequency = 0;
    else
      Frequency -= Freq.Frequency;
    return *this;
  }
  BlockFrequency operator-(BlockFrequency Freq) const {
    BlockFrequency NewFreq(Frequency);
    NewFreq -= Freq;
    return NewFreq;
  }

  /// Multiplies frequency with `Factor`. Returns `nullopt` in case of overflow.
  std::optional<BlockFrequency> mul(uint64_t Factor) const;

  /// Shift block frequency to the right by count digits saturating to 1.
  BlockFrequency &operator>>=(const unsigned count) {
    // Frequency can never be 0 by design.
    assert(Frequency != 0);

    // Shift right by count.
    Frequency >>= count;

    // Saturate to 1 if we are 0.
    Frequency |= Frequency == 0;
    return *this;
  }

  bool operator<(BlockFrequency RHS) const {
    return Frequency < RHS.Frequency;
  }

  bool operator<=(BlockFrequency RHS) const {
    return Frequency <= RHS.Frequency;
  }

  bool operator>(BlockFrequency RHS) const {
    return Frequency > RHS.Frequency;
  }

  bool operator>=(BlockFrequency RHS) const {
    return Frequency >= RHS.Frequency;
  }

  bool operator==(BlockFrequency RHS) const {
    return Frequency == RHS.Frequency;
  }

  bool operator!=(BlockFrequency RHS) const {
    return Frequency != RHS.Frequency;
  }
};

void printRelativeBlockFreq(raw_ostream &OS, BlockFrequency EntryFreq,
                            BlockFrequency Freq);

} // namespace llvm

#endif
