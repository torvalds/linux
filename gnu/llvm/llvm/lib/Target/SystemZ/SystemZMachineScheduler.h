//==- SystemZMachineScheduler.h - SystemZ Scheduler Interface ----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// -------------------------- Post RA scheduling ---------------------------- //
// SystemZPostRASchedStrategy is a scheduling strategy which is plugged into
// the MachineScheduler. It has a sorted Available set of SUs and a pickNode()
// implementation that looks to optimize decoder grouping and balance the
// usage of processor resources. Scheduler states are saved for the end
// region of each MBB, so that a successor block can learn from it.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZMACHINESCHEDULER_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZMACHINESCHEDULER_H

#include "SystemZHazardRecognizer.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include <set>

using namespace llvm;

namespace llvm {

/// A MachineSchedStrategy implementation for SystemZ post RA scheduling.
class SystemZPostRASchedStrategy : public MachineSchedStrategy {

  const MachineLoopInfo *MLI;
  const SystemZInstrInfo *TII;

  // A SchedModel is needed before any DAG is built while advancing past
  // non-scheduled instructions, so it would not always be possible to call
  // DAG->getSchedClass(SU).
  TargetSchedModel SchedModel;

  /// A candidate during instruction evaluation.
  struct Candidate {
    SUnit *SU = nullptr;

    /// The decoding cost.
    int GroupingCost = 0;

    /// The processor resources cost.
    int ResourcesCost = 0;

    Candidate() = default;
    Candidate(SUnit *SU_, SystemZHazardRecognizer &HazardRec);

    // Compare two candidates.
    bool operator<(const Candidate &other);

    // Check if this node is free of cost ("as good as any").
    bool noCost() const {
      return (GroupingCost <= 0 && !ResourcesCost);
    }

#ifndef NDEBUG
    void dumpCosts() {
      if (GroupingCost != 0)
        dbgs() << "  Grouping cost:" << GroupingCost;
      if (ResourcesCost != 0)
        dbgs() << "  Resource cost:" << ResourcesCost;
    }
#endif
  };

  // A sorter for the Available set that makes sure that SUs are considered
  // in the best order.
  struct SUSorter {
    bool operator() (SUnit *lhs, SUnit *rhs) const {
      if (lhs->isScheduleHigh && !rhs->isScheduleHigh)
        return true;
      if (!lhs->isScheduleHigh && rhs->isScheduleHigh)
        return false;

      if (lhs->getHeight() > rhs->getHeight())
        return true;
      else if (lhs->getHeight() < rhs->getHeight())
        return false;

      return (lhs->NodeNum < rhs->NodeNum);
    }
  };
  // A set of SUs with a sorter and dump method.
  struct SUSet : std::set<SUnit*, SUSorter> {
    #ifndef NDEBUG
    void dump(SystemZHazardRecognizer &HazardRec) const;
    #endif
  };

  /// The set of available SUs to schedule next.
  SUSet Available;

  /// Current MBB
  MachineBasicBlock *MBB;

  /// Maintain hazard recognizers for all blocks, so that the scheduler state
  /// can be maintained past BB boundaries when appropariate.
  typedef std::map<MachineBasicBlock*, SystemZHazardRecognizer*> MBB2HazRec;
  MBB2HazRec SchedStates;

  /// Pointer to the HazardRecognizer that tracks the scheduler state for
  /// the current region.
  SystemZHazardRecognizer *HazardRec;

  /// Update the scheduler state by emitting (non-scheduled) instructions
  /// up to, but not including, NextBegin.
  void advanceTo(MachineBasicBlock::iterator NextBegin);

public:
  SystemZPostRASchedStrategy(const MachineSchedContext *C);
  virtual ~SystemZPostRASchedStrategy();

  /// Called for a region before scheduling.
  void initPolicy(MachineBasicBlock::iterator Begin,
                  MachineBasicBlock::iterator End,
                  unsigned NumRegionInstrs) override;

  /// PostRA scheduling does not track pressure.
  bool shouldTrackPressure() const override { return false; }

  // Process scheduling regions top-down so that scheduler states can be
  // transferrred over scheduling boundaries.
  bool doMBBSchedRegionsTopDown() const override { return true; }

  void initialize(ScheduleDAGMI *dag) override;

  /// Tell the strategy that MBB is about to be processed.
  void enterMBB(MachineBasicBlock *NextMBB) override;

  /// Tell the strategy that current MBB is done.
  void leaveMBB() override;

  /// Pick the next node to schedule, or return NULL.
  SUnit *pickNode(bool &IsTopNode) override;

  /// ScheduleDAGMI has scheduled an instruction - tell HazardRec
  /// about it.
  void schedNode(SUnit *SU, bool IsTopNode) override;

  /// SU has had all predecessor dependencies resolved. Put it into
  /// Available.
  void releaseTopNode(SUnit *SU) override;

  /// Currently only scheduling top-down, so this method is empty.
  void releaseBottomNode(SUnit *SU) override {};
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZMACHINESCHEDULER_H
