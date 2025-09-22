//===- GCNIterativeScheduler.h - GCN Scheduler ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the class GCNIterativeScheduler, which uses an iterative
/// approach to find a best schedule for GCN architecture. It basically makes
/// use of various lightweight schedules, scores them, chooses best one based on
/// their scores, and finally implements the chosen one.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_GCNITERATIVESCHEDULER_H
#define LLVM_LIB_TARGET_AMDGPU_GCNITERATIVESCHEDULER_H

#include "GCNRegPressure.h"
#include "llvm/CodeGen/MachineScheduler.h"

namespace llvm {

class MachineInstr;
class SUnit;
class raw_ostream;

class GCNIterativeScheduler : public ScheduleDAGMILive {
  using BaseClass = ScheduleDAGMILive;

public:
  enum StrategyKind {
    SCHEDULE_MINREGONLY,
    SCHEDULE_MINREGFORCED,
    SCHEDULE_LEGACYMAXOCCUPANCY,
    SCHEDULE_ILP
  };

  GCNIterativeScheduler(MachineSchedContext *C,
                        StrategyKind S);

  void schedule() override;

  void enterRegion(MachineBasicBlock *BB,
                   MachineBasicBlock::iterator Begin,
                   MachineBasicBlock::iterator End,
                   unsigned RegionInstrs) override;

  void finalizeSchedule() override;

protected:
  using ScheduleRef = ArrayRef<const SUnit *>;

  struct TentativeSchedule {
    std::vector<MachineInstr *> Schedule;
    GCNRegPressure MaxPressure;
  };

  struct Region {
    // Fields except for BestSchedule are supposed to reflect current IR state
    // `const` fields are to emphasize they shouldn't change for any schedule.
    MachineBasicBlock::iterator Begin;
    // End is either a boundary instruction or end of basic block
    const MachineBasicBlock::iterator End;
    const unsigned NumRegionInstrs;
    GCNRegPressure MaxPressure;

    // best schedule for the region so far (not scheduled yet)
    std::unique_ptr<TentativeSchedule> BestSchedule;
  };

  SpecificBumpPtrAllocator<Region> Alloc;
  std::vector<Region*> Regions;

  MachineSchedContext *Context;
  const StrategyKind Strategy;
  mutable GCNUpwardRPTracker UPTracker;

  class BuildDAG;
  class OverrideLegacyStrategy;

  template <typename Range>
  GCNRegPressure getSchedulePressure(const Region &R,
                                     Range &&Schedule) const;

  GCNRegPressure getRegionPressure(MachineBasicBlock::iterator Begin,
                                   MachineBasicBlock::iterator End) const;

  GCNRegPressure getRegionPressure(const Region &R) const {
    return getRegionPressure(R.Begin, R.End);
  }

  void setBestSchedule(Region &R,
                       ScheduleRef Schedule,
                       const GCNRegPressure &MaxRP = GCNRegPressure());

  void scheduleBest(Region &R);

  std::vector<MachineInstr*> detachSchedule(ScheduleRef Schedule) const;

  void sortRegionsByPressure(unsigned TargetOcc);

  template <typename Range>
  void scheduleRegion(Region &R, Range &&Schedule,
                      const GCNRegPressure &MaxRP = GCNRegPressure());

  unsigned tryMaximizeOccupancy(unsigned TargetOcc =
                                std::numeric_limits<unsigned>::max());

  void scheduleLegacyMaxOccupancy(bool TryMaximizeOccupancy = true);
  void scheduleMinReg(bool force = false);
  void scheduleILP(bool TryMaximizeOccupancy = true);

  void printRegions(raw_ostream &OS) const;
  void printSchedResult(raw_ostream &OS,
                        const Region *R,
                        const GCNRegPressure &RP) const;
  void printSchedRP(raw_ostream &OS,
                    const GCNRegPressure &Before,
                    const GCNRegPressure &After) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_GCNITERATIVESCHEDULER_H
