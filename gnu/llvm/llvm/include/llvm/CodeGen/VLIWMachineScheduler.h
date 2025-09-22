//===- VLIWMachineScheduler.h - VLIW-Focused Scheduling Pass ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//                                                                            //
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_VLIWMACHINESCHEDULER_H
#define LLVM_CODEGEN_VLIWMACHINESCHEDULER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include <limits>
#include <memory>
#include <utility>

namespace llvm {

class DFAPacketizer;
class RegisterClassInfo;
class ScheduleHazardRecognizer;
class SUnit;
class TargetInstrInfo;
class TargetSubtargetInfo;

class VLIWResourceModel {
protected:
  const TargetInstrInfo *TII;

  /// ResourcesModel - Represents VLIW state.
  /// Not limited to VLIW targets per se, but assumes definition of resource
  /// model by a target.
  DFAPacketizer *ResourcesModel;

  const TargetSchedModel *SchedModel;

  /// Local packet/bundle model. Purely
  /// internal to the MI scheduler at the time.
  SmallVector<SUnit *> Packet;

  /// Total packets created.
  unsigned TotalPackets = 0;

public:
  VLIWResourceModel(const TargetSubtargetInfo &STI, const TargetSchedModel *SM);
  VLIWResourceModel &operator=(const VLIWResourceModel &other) = delete;
  VLIWResourceModel(const VLIWResourceModel &other) = delete;
  virtual ~VLIWResourceModel();

  virtual void reset();

  virtual bool hasDependence(const SUnit *SUd, const SUnit *SUu);
  virtual bool isResourceAvailable(SUnit *SU, bool IsTop);
  virtual bool reserveResources(SUnit *SU, bool IsTop);
  unsigned getTotalPackets() const { return TotalPackets; }
  size_t getPacketInstCount() const { return Packet.size(); }
  bool isInPacket(SUnit *SU) const { return is_contained(Packet, SU); }

protected:
  virtual DFAPacketizer *createPacketizer(const TargetSubtargetInfo &STI) const;
};

/// Extend the standard ScheduleDAGMILive to provide more context and override
/// the top-level schedule() driver.
class VLIWMachineScheduler : public ScheduleDAGMILive {
public:
  VLIWMachineScheduler(MachineSchedContext *C,
                       std::unique_ptr<MachineSchedStrategy> S)
      : ScheduleDAGMILive(C, std::move(S)) {}

  /// Schedule - This is called back from ScheduleDAGInstrs::Run() when it's
  /// time to do some work.
  void schedule() override;

  RegisterClassInfo *getRegClassInfo() { return RegClassInfo; }
  int getBBSize() { return BB->size(); }
};

//===----------------------------------------------------------------------===//
// ConvergingVLIWScheduler - Implementation of a VLIW-aware
// MachineSchedStrategy.
//===----------------------------------------------------------------------===//

class ConvergingVLIWScheduler : public MachineSchedStrategy {
protected:
  /// Store the state used by ConvergingVLIWScheduler heuristics, required
  ///  for the lifetime of one invocation of pickNode().
  struct SchedCandidate {
    // The best SUnit candidate.
    SUnit *SU = nullptr;

    // Register pressure values for the best candidate.
    RegPressureDelta RPDelta;

    // Best scheduling cost.
    int SCost = 0;

    SchedCandidate() = default;
  };
  /// Represent the type of SchedCandidate found within a single queue.
  enum CandResult {
    NoCand,
    NodeOrder,
    SingleExcess,
    SingleCritical,
    SingleMax,
    MultiPressure,
    BestCost,
    Weak
  };

  // Constants used to denote relative importance of
  // heuristic components for cost computation.
  static constexpr unsigned PriorityOne = 200;
  static constexpr unsigned PriorityTwo = 50;
  static constexpr unsigned PriorityThree = 75;
  static constexpr unsigned ScaleTwo = 10;

  /// Each Scheduling boundary is associated with ready queues. It tracks the
  /// current cycle in whichever direction at has moved, and maintains the state
  /// of "hazards" and other interlocks at the current cycle.
  struct VLIWSchedBoundary {
    VLIWMachineScheduler *DAG = nullptr;
    const TargetSchedModel *SchedModel = nullptr;

    ReadyQueue Available;
    ReadyQueue Pending;
    bool CheckPending = false;

    ScheduleHazardRecognizer *HazardRec = nullptr;
    VLIWResourceModel *ResourceModel = nullptr;

    unsigned CurrCycle = 0;
    unsigned IssueCount = 0;
    unsigned CriticalPathLength = 0;

    /// MinReadyCycle - Cycle of the soonest available instruction.
    unsigned MinReadyCycle = std::numeric_limits<unsigned>::max();

    // Remember the greatest min operand latency.
    unsigned MaxMinLatency = 0;

    /// Pending queues extend the ready queues with the same ID and the
    /// PendingFlag set.
    VLIWSchedBoundary(unsigned ID, const Twine &Name)
        : Available(ID, Name + ".A"),
          Pending(ID << ConvergingVLIWScheduler::LogMaxQID, Name + ".P") {}

    ~VLIWSchedBoundary();
    VLIWSchedBoundary &operator=(const VLIWSchedBoundary &other) = delete;
    VLIWSchedBoundary(const VLIWSchedBoundary &other) = delete;

    void init(VLIWMachineScheduler *dag, const TargetSchedModel *smodel) {
      DAG = dag;
      SchedModel = smodel;
      CurrCycle = 0;
      IssueCount = 0;
      // Initialize the critical path length limit, which used by the scheduling
      // cost model to determine the value for scheduling an instruction. We use
      // a slightly different heuristic for small and large functions. For small
      // functions, it's important to use the height/depth of the instruction.
      // For large functions, prioritizing by height or depth increases spills.
      const auto BBSize = DAG->getBBSize();
      CriticalPathLength = BBSize / SchedModel->getIssueWidth();
      if (BBSize < 50)
        // We divide by two as a cheap and simple heuristic to reduce the
        // critcal path length, which increases the priority of using the graph
        // height/depth in the scheduler's cost computation.
        CriticalPathLength >>= 1;
      else {
        // For large basic blocks, we prefer a larger critical path length to
        // decrease the priority of using the graph height/depth.
        unsigned MaxPath = 0;
        for (auto &SU : DAG->SUnits)
          MaxPath = std::max(MaxPath, isTop() ? SU.getHeight() : SU.getDepth());
        CriticalPathLength = std::max(CriticalPathLength, MaxPath) + 1;
      }
    }

    bool isTop() const {
      return Available.getID() == ConvergingVLIWScheduler::TopQID;
    }

    bool checkHazard(SUnit *SU);

    void releaseNode(SUnit *SU, unsigned ReadyCycle);

    void bumpCycle();

    void bumpNode(SUnit *SU);

    void releasePending();

    void removeReady(SUnit *SU);

    SUnit *pickOnlyChoice();

    bool isLatencyBound(SUnit *SU) {
      if (CurrCycle >= CriticalPathLength)
        return true;
      unsigned PathLength = isTop() ? SU->getHeight() : SU->getDepth();
      return CriticalPathLength - CurrCycle <= PathLength;
    }
  };

  VLIWMachineScheduler *DAG = nullptr;
  const TargetSchedModel *SchedModel = nullptr;

  // State of the top and bottom scheduled instruction boundaries.
  VLIWSchedBoundary Top;
  VLIWSchedBoundary Bot;

  /// List of pressure sets that have a high pressure level in the region.
  SmallVector<bool> HighPressureSets;

public:
  /// SUnit::NodeQueueId: 0 (none), 1 (top), 2 (bot), 3 (both)
  enum { TopQID = 1, BotQID = 2, LogMaxQID = 2 };

  ConvergingVLIWScheduler() : Top(TopQID, "TopQ"), Bot(BotQID, "BotQ") {}
  virtual ~ConvergingVLIWScheduler() = default;

  void initialize(ScheduleDAGMI *dag) override;

  SUnit *pickNode(bool &IsTopNode) override;

  void schedNode(SUnit *SU, bool IsTopNode) override;

  void releaseTopNode(SUnit *SU) override;

  void releaseBottomNode(SUnit *SU) override;

  unsigned reportPackets() {
    return Top.ResourceModel->getTotalPackets() +
           Bot.ResourceModel->getTotalPackets();
  }

protected:
  virtual VLIWResourceModel *
  createVLIWResourceModel(const TargetSubtargetInfo &STI,
                          const TargetSchedModel *SchedModel) const;

  SUnit *pickNodeBidrectional(bool &IsTopNode);

  int pressureChange(const SUnit *SU, bool isBotUp);

  virtual int SchedulingCost(ReadyQueue &Q, SUnit *SU,
                             SchedCandidate &Candidate, RegPressureDelta &Delta,
                             bool verbose);

  CandResult pickNodeFromQueue(VLIWSchedBoundary &Zone,
                               const RegPressureTracker &RPTracker,
                               SchedCandidate &Candidate);
#ifndef NDEBUG
  void traceCandidate(const char *Label, const ReadyQueue &Q, SUnit *SU,
                      int Cost, PressureChange P = PressureChange());

  void readyQueueVerboseDump(const RegPressureTracker &RPTracker,
                             SchedCandidate &Candidate, ReadyQueue &Q);
#endif
};

} // end namespace llvm

#endif // LLVM_CODEGEN_VLIWMACHINESCHEDULER_H
