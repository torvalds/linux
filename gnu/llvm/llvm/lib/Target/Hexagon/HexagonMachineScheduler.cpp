//===- HexagonMachineScheduler.cpp - MI Scheduler for Hexagon -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MachineScheduler schedules machine instructions after phi elimination. It
// preserves LiveIntervals so it can be invoked before register allocation.
//
//===----------------------------------------------------------------------===//

#include "HexagonMachineScheduler.h"
#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/VLIWMachineScheduler.h"

using namespace llvm;

#define DEBUG_TYPE "machine-scheduler"

/// Return true if there is a dependence between SUd and SUu.
bool HexagonVLIWResourceModel::hasDependence(const SUnit *SUd,
                                             const SUnit *SUu) {
  const auto *QII = static_cast<const HexagonInstrInfo *>(TII);

  // Enable .cur formation.
  if (QII->mayBeCurLoad(*SUd->getInstr()))
    return false;

  if (QII->canExecuteInBundle(*SUd->getInstr(), *SUu->getInstr()))
    return false;

  return VLIWResourceModel::hasDependence(SUd, SUu);
}

VLIWResourceModel *HexagonConvergingVLIWScheduler::createVLIWResourceModel(
    const TargetSubtargetInfo &STI, const TargetSchedModel *SchedModel) const {
  return new HexagonVLIWResourceModel(STI, SchedModel);
}

int HexagonConvergingVLIWScheduler::SchedulingCost(ReadyQueue &Q, SUnit *SU,
                                                   SchedCandidate &Candidate,
                                                   RegPressureDelta &Delta,
                                                   bool verbose) {
  int ResCount =
      ConvergingVLIWScheduler::SchedulingCost(Q, SU, Candidate, Delta, verbose);

  if (!SU || SU->isScheduled)
    return ResCount;

  auto &QST = DAG->MF.getSubtarget<HexagonSubtarget>();
  auto &QII = *QST.getInstrInfo();
  if (SU->isInstr() && QII.mayBeCurLoad(*SU->getInstr())) {
    if (Q.getID() == TopQID &&
        Top.ResourceModel->isResourceAvailable(SU, true)) {
      ResCount += PriorityTwo;
      LLVM_DEBUG(if (verbose) dbgs() << "C|");
    } else if (Q.getID() == BotQID &&
               Bot.ResourceModel->isResourceAvailable(SU, false)) {
      ResCount += PriorityTwo;
      LLVM_DEBUG(if (verbose) dbgs() << "C|");
    }
  }

  return ResCount;
}
