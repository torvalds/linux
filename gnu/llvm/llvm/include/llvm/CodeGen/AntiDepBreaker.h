//===- llvm/CodeGen/AntiDepBreaker.h - Anti-Dependence Breaking -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the AntiDepBreaker class, which implements
// anti-dependence breaking heuristics for post-register-allocation scheduling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ANTIDEPBREAKER_H
#define LLVM_CODEGEN_ANTIDEPBREAKER_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/Compiler.h"
#include <utility>
#include <vector>

namespace llvm {

class RegisterClassInfo;

/// This class works in conjunction with the post-RA scheduler to rename
/// registers to break register anti-dependencies (WAR hazards).
class AntiDepBreaker {
public:
  using DbgValueVector =
      std::vector<std::pair<MachineInstr *, MachineInstr *>>;

  virtual ~AntiDepBreaker();

  /// Initialize anti-dep breaking for a new basic block.
  virtual void StartBlock(MachineBasicBlock *BB) = 0;

  /// Identifiy anti-dependencies within a basic-block region and break them by
  /// renaming registers. Return the number of anti-dependencies broken.
  virtual unsigned BreakAntiDependencies(const std::vector<SUnit> &SUnits,
                                         MachineBasicBlock::iterator Begin,
                                         MachineBasicBlock::iterator End,
                                         unsigned InsertPosIndex,
                                         DbgValueVector &DbgValues) = 0;

  /// Update liveness information to account for the current
  /// instruction, which will not be scheduled.
  virtual void Observe(MachineInstr &MI, unsigned Count,
                       unsigned InsertPosIndex) = 0;

  /// Finish anti-dep breaking for a basic block.
  virtual void FinishBlock() = 0;

  /// Update DBG_VALUE or DBG_PHI if dependency breaker is updating
  /// other machine instruction to use NewReg.
  void UpdateDbgValue(MachineInstr &MI, unsigned OldReg, unsigned NewReg) {
    if (MI.isDebugValue()) {
      if (MI.getDebugOperand(0).isReg() &&
          MI.getDebugOperand(0).getReg() == OldReg)
        MI.getDebugOperand(0).setReg(NewReg);
    } else if (MI.isDebugPHI()) {
      if (MI.getOperand(0).isReg() &&
          MI.getOperand(0).getReg() == OldReg)
        MI.getOperand(0).setReg(NewReg);
    } else {
      llvm_unreachable("MI is not DBG_VALUE / DBG_PHI!");
    }
  }

  /// Update all DBG_VALUE instructions that may be affected by the dependency
  /// breaker's update of ParentMI to use NewReg.
  void UpdateDbgValues(const DbgValueVector &DbgValues, MachineInstr *ParentMI,
                       unsigned OldReg, unsigned NewReg) {
    // The following code is dependent on the order in which the DbgValues are
    // constructed in ScheduleDAGInstrs::buildSchedGraph.
    MachineInstr *PrevDbgMI = nullptr;
    for (const auto &DV : make_range(DbgValues.crbegin(), DbgValues.crend())) {
      MachineInstr *PrevMI = DV.second;
      if ((PrevMI == ParentMI) || (PrevMI == PrevDbgMI)) {
        MachineInstr *DbgMI = DV.first;
        UpdateDbgValue(*DbgMI, OldReg, NewReg);
        PrevDbgMI = DbgMI;
      } else if (PrevDbgMI) {
        break; // If no match and already found a DBG_VALUE, we're done.
      }
    }
  }
};

AntiDepBreaker *createAggressiveAntiDepBreaker(
    MachineFunction &MFi, const RegisterClassInfo &RCI,
    TargetSubtargetInfo::RegClassVector &CriticalPathRCs);

AntiDepBreaker *createCriticalAntiDepBreaker(MachineFunction &MFi,
                                             const RegisterClassInfo &RCI);

} // end namespace llvm

#endif // LLVM_CODEGEN_ANTIDEPBREAKER_H
