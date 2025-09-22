//===- RegAllocScore.cpp - evaluate regalloc policy quality ---------------===//
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

#include "RegAllocScore.h"
#include "llvm/ADT/ilist_iterator.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
cl::opt<double> CopyWeight("regalloc-copy-weight", cl::init(0.2), cl::Hidden);
cl::opt<double> LoadWeight("regalloc-load-weight", cl::init(4.0), cl::Hidden);
cl::opt<double> StoreWeight("regalloc-store-weight", cl::init(1.0), cl::Hidden);
cl::opt<double> CheapRematWeight("regalloc-cheap-remat-weight", cl::init(0.2),
                                 cl::Hidden);
cl::opt<double> ExpensiveRematWeight("regalloc-expensive-remat-weight",
                                     cl::init(1.0), cl::Hidden);
#define DEBUG_TYPE "regalloc-score"

RegAllocScore &RegAllocScore::operator+=(const RegAllocScore &Other) {
  CopyCounts += Other.copyCounts();
  LoadCounts += Other.loadCounts();
  StoreCounts += Other.storeCounts();
  LoadStoreCounts += Other.loadStoreCounts();
  CheapRematCounts += Other.cheapRematCounts();
  ExpensiveRematCounts += Other.expensiveRematCounts();
  return *this;
}

bool RegAllocScore::operator==(const RegAllocScore &Other) const {
  return copyCounts() == Other.copyCounts() &&
         loadCounts() == Other.loadCounts() &&
         storeCounts() == Other.storeCounts() &&
         loadStoreCounts() == Other.loadStoreCounts() &&
         cheapRematCounts() == Other.cheapRematCounts() &&
         expensiveRematCounts() == Other.expensiveRematCounts();
}

bool RegAllocScore::operator!=(const RegAllocScore &Other) const {
  return !(*this == Other);
}

double RegAllocScore::getScore() const {
  double Ret = 0.0;
  Ret += CopyWeight * copyCounts();
  Ret += LoadWeight * loadCounts();
  Ret += StoreWeight * storeCounts();
  Ret += (LoadWeight + StoreWeight) * loadStoreCounts();
  Ret += CheapRematWeight * cheapRematCounts();
  Ret += ExpensiveRematWeight * expensiveRematCounts();

  return Ret;
}

RegAllocScore
llvm::calculateRegAllocScore(const MachineFunction &MF,
                             const MachineBlockFrequencyInfo &MBFI) {
  return calculateRegAllocScore(
      MF,
      [&](const MachineBasicBlock &MBB) {
        return MBFI.getBlockFreqRelativeToEntryBlock(&MBB);
      },
      [&](const MachineInstr &MI) {
        return MF.getSubtarget().getInstrInfo()->isTriviallyReMaterializable(
            MI);
      });
}

RegAllocScore llvm::calculateRegAllocScore(
    const MachineFunction &MF,
    llvm::function_ref<double(const MachineBasicBlock &)> GetBBFreq,
    llvm::function_ref<bool(const MachineInstr &)>
        IsTriviallyRematerializable) {
  RegAllocScore Total;

  for (const MachineBasicBlock &MBB : MF) {
    double BlockFreqRelativeToEntrypoint = GetBBFreq(MBB);
    RegAllocScore MBBScore;

    for (const MachineInstr &MI : MBB) {
      if (MI.isDebugInstr() || MI.isKill() || MI.isInlineAsm()) {
        continue;
      }
      if (MI.isCopy()) {
        MBBScore.onCopy(BlockFreqRelativeToEntrypoint);
      } else if (IsTriviallyRematerializable(MI)) {
        if (MI.getDesc().isAsCheapAsAMove()) {
          MBBScore.onCheapRemat(BlockFreqRelativeToEntrypoint);
        } else {
          MBBScore.onExpensiveRemat(BlockFreqRelativeToEntrypoint);
        }
      } else if (MI.mayLoad() && MI.mayStore()) {
        MBBScore.onLoadStore(BlockFreqRelativeToEntrypoint);
      } else if (MI.mayLoad()) {
        MBBScore.onLoad(BlockFreqRelativeToEntrypoint);
      } else if (MI.mayStore()) {
        MBBScore.onStore(BlockFreqRelativeToEntrypoint);
      }
    }
    Total += MBBScore;
  }
  return Total;
}
