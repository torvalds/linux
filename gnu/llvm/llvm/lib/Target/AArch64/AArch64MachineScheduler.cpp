//===- AArch64MachineScheduler.cpp - MI Scheduler for AArch64 -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AArch64MachineScheduler.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "MCTargetDesc/AArch64MCTargetDesc.h"

using namespace llvm;

static bool needReorderStoreMI(const MachineInstr *MI) {
  if (!MI)
    return false;

  switch (MI->getOpcode()) {
  default:
    return false;
  case AArch64::STURQi:
  case AArch64::STRQui:
    if (!MI->getMF()->getSubtarget<AArch64Subtarget>().isStoreAddressAscend())
      return false;
    [[fallthrough]];
  case AArch64::STPQi:
    return AArch64InstrInfo::getLdStOffsetOp(*MI).isImm();
  }

  return false;
}

// Return true if two stores with same base address may overlap writes
static bool mayOverlapWrite(const MachineInstr &MI0, const MachineInstr &MI1,
                            int64_t &Off0, int64_t &Off1) {
  const MachineOperand &Base0 = AArch64InstrInfo::getLdStBaseOp(MI0);
  const MachineOperand &Base1 = AArch64InstrInfo::getLdStBaseOp(MI1);

  // May overlapping writes if two store instructions without same base
  if (!Base0.isIdenticalTo(Base1))
    return true;

  int StoreSize0 = AArch64InstrInfo::getMemScale(MI0);
  int StoreSize1 = AArch64InstrInfo::getMemScale(MI1);
  Off0 = AArch64InstrInfo::hasUnscaledLdStOffset(MI0.getOpcode())
             ? AArch64InstrInfo::getLdStOffsetOp(MI0).getImm()
             : AArch64InstrInfo::getLdStOffsetOp(MI0).getImm() * StoreSize0;
  Off1 = AArch64InstrInfo::hasUnscaledLdStOffset(MI1.getOpcode())
             ? AArch64InstrInfo::getLdStOffsetOp(MI1).getImm()
             : AArch64InstrInfo::getLdStOffsetOp(MI1).getImm() * StoreSize1;

  const MachineInstr &MI = (Off0 < Off1) ? MI0 : MI1;
  int Multiples = AArch64InstrInfo::isPairedLdSt(MI) ? 2 : 1;
  int StoreSize = AArch64InstrInfo::getMemScale(MI) * Multiples;

  return llabs(Off0 - Off1) < StoreSize;
}

bool AArch64PostRASchedStrategy::tryCandidate(SchedCandidate &Cand,
                                              SchedCandidate &TryCand) {
  bool OriginalResult = PostGenericScheduler::tryCandidate(Cand, TryCand);

  if (Cand.isValid()) {
    MachineInstr *Instr0 = TryCand.SU->getInstr();
    MachineInstr *Instr1 = Cand.SU->getInstr();

    if (!needReorderStoreMI(Instr0) || !needReorderStoreMI(Instr1))
      return OriginalResult;

    int64_t Off0, Off1;
    // With the same base address and non-overlapping writes.
    if (!mayOverlapWrite(*Instr0, *Instr1, Off0, Off1)) {
      TryCand.Reason = NodeOrder;
      // Order them by ascending offsets.
      return Off0 < Off1;
    }
  }

  return OriginalResult;
}
