//==- RegAllocScore.h - evaluate regalloc policy quality  ----------*-C++-*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// Calculate a measure of the register allocation policy quality. This is used
/// to construct a reward for the training of the ML-driven allocation policy.
/// Currently, the score is the sum of the machine basic block frequency-weighed
/// number of loads, stores, copies, and remat instructions, each factored with
/// a relative weight.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCSCORE_H_
#define LLVM_CODEGEN_REGALLOCSCORE_H_

#include "llvm/ADT/STLFunctionalExtras.h"

namespace llvm {

class MachineBasicBlock;
class MachineBlockFrequencyInfo;
class MachineFunction;
class MachineInstr;

/// Regalloc score.
class RegAllocScore final {
  double CopyCounts = 0.0;
  double LoadCounts = 0.0;
  double StoreCounts = 0.0;
  double CheapRematCounts = 0.0;
  double LoadStoreCounts = 0.0;
  double ExpensiveRematCounts = 0.0;

public:
  RegAllocScore() = default;
  RegAllocScore(const RegAllocScore &) = default;

  double copyCounts() const { return CopyCounts; }
  double loadCounts() const { return LoadCounts; }
  double storeCounts() const { return StoreCounts; }
  double loadStoreCounts() const { return LoadStoreCounts; }
  double expensiveRematCounts() const { return ExpensiveRematCounts; }
  double cheapRematCounts() const { return CheapRematCounts; }

  void onCopy(double Freq) { CopyCounts += Freq; }
  void onLoad(double Freq) { LoadCounts += Freq; }
  void onStore(double Freq) { StoreCounts += Freq; }
  void onLoadStore(double Freq) { LoadStoreCounts += Freq; }
  void onExpensiveRemat(double Freq) { ExpensiveRematCounts += Freq; }
  void onCheapRemat(double Freq) { CheapRematCounts += Freq; }

  RegAllocScore &operator+=(const RegAllocScore &Other);
  bool operator==(const RegAllocScore &Other) const;
  bool operator!=(const RegAllocScore &Other) const;
  double getScore() const;
};

/// Calculate a score. When comparing 2 scores for the same function but
/// different policies, the better policy would have a smaller score.
/// The implementation is the overload below (which is also easily unittestable)
RegAllocScore calculateRegAllocScore(const MachineFunction &MF,
                                     const MachineBlockFrequencyInfo &MBFI);

/// Implementation of the above, which is also more easily unittestable.
RegAllocScore calculateRegAllocScore(
    const MachineFunction &MF,
    llvm::function_ref<double(const MachineBasicBlock &)> GetBBFreq,
    llvm::function_ref<bool(const MachineInstr &)> IsTriviallyRematerializable);
} // end namespace llvm

#endif // LLVM_CODEGEN_REGALLOCSCORE_H_
