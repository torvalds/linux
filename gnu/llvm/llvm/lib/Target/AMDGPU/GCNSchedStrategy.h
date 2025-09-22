//===-- GCNSchedStrategy.h - GCN Scheduler Strategy -*- C++ -*-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_GCNSCHEDSTRATEGY_H
#define LLVM_LIB_TARGET_AMDGPU_GCNSCHEDSTRATEGY_H

#include "GCNRegPressure.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/CodeGen/MachineScheduler.h"

namespace llvm {

class SIMachineFunctionInfo;
class SIRegisterInfo;
class GCNSubtarget;
class GCNSchedStage;

enum class GCNSchedStageID : unsigned {
  OccInitialSchedule = 0,
  UnclusteredHighRPReschedule = 1,
  ClusteredLowOccupancyReschedule = 2,
  PreRARematerialize = 3,
  ILPInitialSchedule = 4
};

#ifndef NDEBUG
raw_ostream &operator<<(raw_ostream &OS, const GCNSchedStageID &StageID);
#endif

/// This is a minimal scheduler strategy.  The main difference between this
/// and the GenericScheduler is that GCNSchedStrategy uses different
/// heuristics to determine excess/critical pressure sets.
class GCNSchedStrategy : public GenericScheduler {
protected:
  SUnit *pickNodeBidirectional(bool &IsTopNode);

  void pickNodeFromQueue(SchedBoundary &Zone, const CandPolicy &ZonePolicy,
                         const RegPressureTracker &RPTracker,
                         SchedCandidate &Cand, bool IsBottomUp);

  void initCandidate(SchedCandidate &Cand, SUnit *SU, bool AtTop,
                     const RegPressureTracker &RPTracker,
                     const SIRegisterInfo *SRI, unsigned SGPRPressure,
                     unsigned VGPRPressure, bool IsBottomUp);

  std::vector<unsigned> Pressure;

  std::vector<unsigned> MaxPressure;

  unsigned SGPRExcessLimit;

  unsigned VGPRExcessLimit;

  unsigned TargetOccupancy;

  MachineFunction *MF;

  // Scheduling stages for this strategy.
  SmallVector<GCNSchedStageID, 4> SchedStages;

  // Pointer to the current SchedStageID.
  SmallVectorImpl<GCNSchedStageID>::iterator CurrentStage = nullptr;

public:
  // schedule() have seen register pressure over the critical limits and had to
  // track register pressure for actual scheduling heuristics.
  bool HasHighPressure;

  // Schedule known to have excess register pressure. Be more conservative in
  // increasing ILP and preserving VGPRs.
  bool KnownExcessRP = false;

  // An error margin is necessary because of poor performance of the generic RP
  // tracker and can be adjusted up for tuning heuristics to try and more
  // aggressively reduce register pressure.
  unsigned ErrorMargin = 3;

  // Bias for SGPR limits under a high register pressure.
  const unsigned HighRPSGPRBias = 7;

  // Bias for VGPR limits under a high register pressure.
  const unsigned HighRPVGPRBias = 7;

  unsigned SGPRCriticalLimit;

  unsigned VGPRCriticalLimit;

  unsigned SGPRLimitBias = 0;

  unsigned VGPRLimitBias = 0;

  GCNSchedStrategy(const MachineSchedContext *C);

  SUnit *pickNode(bool &IsTopNode) override;

  void initialize(ScheduleDAGMI *DAG) override;

  unsigned getTargetOccupancy() { return TargetOccupancy; }

  void setTargetOccupancy(unsigned Occ) { TargetOccupancy = Occ; }

  GCNSchedStageID getCurrentStage();

  // Advances stage. Returns true if there are remaining stages.
  bool advanceStage();

  bool hasNextStage() const;

  GCNSchedStageID getNextStage() const;
};

/// The goal of this scheduling strategy is to maximize kernel occupancy (i.e.
/// maximum number of waves per simd).
class GCNMaxOccupancySchedStrategy final : public GCNSchedStrategy {
public:
  GCNMaxOccupancySchedStrategy(const MachineSchedContext *C);
};

/// The goal of this scheduling strategy is to maximize ILP for a single wave
/// (i.e. latency hiding).
class GCNMaxILPSchedStrategy final : public GCNSchedStrategy {
protected:
  bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand,
                    SchedBoundary *Zone) const override;

public:
  GCNMaxILPSchedStrategy(const MachineSchedContext *C);
};

class ScheduleMetrics {
  unsigned ScheduleLength;
  unsigned BubbleCycles;

public:
  ScheduleMetrics() {}
  ScheduleMetrics(unsigned L, unsigned BC)
      : ScheduleLength(L), BubbleCycles(BC) {}
  unsigned getLength() const { return ScheduleLength; }
  unsigned getBubbles() const { return BubbleCycles; }
  unsigned getMetric() const {
    unsigned Metric = (BubbleCycles * ScaleFactor) / ScheduleLength;
    // Metric is zero if the amount of bubbles is less than 1% which is too
    // small. So, return 1.
    return Metric ? Metric : 1;
  }
  static const unsigned ScaleFactor;
};

inline raw_ostream &operator<<(raw_ostream &OS, const ScheduleMetrics &Sm) {
  dbgs() << "\n Schedule Metric (scaled by "
         << ScheduleMetrics::ScaleFactor
         << " ) is: " << Sm.getMetric() << " [ " << Sm.getBubbles() << "/"
         << Sm.getLength() << " ]\n";
  return OS;
}

class GCNScheduleDAGMILive final : public ScheduleDAGMILive {
  friend class GCNSchedStage;
  friend class OccInitialScheduleStage;
  friend class UnclusteredHighRPStage;
  friend class ClusteredLowOccStage;
  friend class PreRARematStage;
  friend class ILPInitialScheduleStage;

  const GCNSubtarget &ST;

  SIMachineFunctionInfo &MFI;

  // Occupancy target at the beginning of function scheduling cycle.
  unsigned StartingOccupancy;

  // Minimal real occupancy recorder for the function.
  unsigned MinOccupancy;

  // Vector of regions recorder for later rescheduling
  SmallVector<std::pair<MachineBasicBlock::iterator,
                        MachineBasicBlock::iterator>, 32> Regions;

  // Records if a region is not yet scheduled, or schedule has been reverted,
  // or we generally desire to reschedule it.
  BitVector RescheduleRegions;

  // Record regions with high register pressure.
  BitVector RegionsWithHighRP;

  // Record regions with excess register pressure over the physical register
  // limit. Register pressure in these regions usually will result in spilling.
  BitVector RegionsWithExcessRP;

  // Regions that has the same occupancy as the latest MinOccupancy
  BitVector RegionsWithMinOcc;

  // Regions that have IGLP instructions (SCHED_GROUP_BARRIER or IGLP_OPT).
  BitVector RegionsWithIGLPInstrs;

  // Region live-in cache.
  SmallVector<GCNRPTracker::LiveRegSet, 32> LiveIns;

  // Region pressure cache.
  SmallVector<GCNRegPressure, 32> Pressure;

  // Temporary basic block live-in cache.
  DenseMap<const MachineBasicBlock *, GCNRPTracker::LiveRegSet> MBBLiveIns;

  DenseMap<MachineInstr *, GCNRPTracker::LiveRegSet> BBLiveInMap;

  DenseMap<MachineInstr *, GCNRPTracker::LiveRegSet> getBBLiveInMap() const;

  // Return current region pressure.
  GCNRegPressure getRealRegPressure(unsigned RegionIdx) const;

  // Compute and cache live-ins and pressure for all regions in block.
  void computeBlockPressure(unsigned RegionIdx, const MachineBasicBlock *MBB);

  // Update region boundaries when removing MI or inserting NewMI before MI.
  void updateRegionBoundaries(
      SmallVectorImpl<std::pair<MachineBasicBlock::iterator,
                                MachineBasicBlock::iterator>> &RegionBoundaries,
      MachineBasicBlock::iterator MI, MachineInstr *NewMI,
      bool Removing = false);

  void runSchedStages();

  std::unique_ptr<GCNSchedStage> createSchedStage(GCNSchedStageID SchedStageID);

public:
  GCNScheduleDAGMILive(MachineSchedContext *C,
                       std::unique_ptr<MachineSchedStrategy> S);

  void schedule() override;

  void finalizeSchedule() override;
};

// GCNSchedStrategy applies multiple scheduling stages to a function.
class GCNSchedStage {
protected:
  GCNScheduleDAGMILive &DAG;

  GCNSchedStrategy &S;

  MachineFunction &MF;

  SIMachineFunctionInfo &MFI;

  const GCNSubtarget &ST;

  const GCNSchedStageID StageID;

  // The current block being scheduled.
  MachineBasicBlock *CurrentMBB = nullptr;

  // Current region index.
  unsigned RegionIdx = 0;

  // Record the original order of instructions before scheduling.
  std::vector<MachineInstr *> Unsched;

  // RP before scheduling the current region.
  GCNRegPressure PressureBefore;

  // RP after scheduling the current region.
  GCNRegPressure PressureAfter;

  std::vector<std::unique_ptr<ScheduleDAGMutation>> SavedMutations;

  GCNSchedStage(GCNSchedStageID StageID, GCNScheduleDAGMILive &DAG);

public:
  // Initialize state for a scheduling stage. Returns false if the current stage
  // should be skipped.
  virtual bool initGCNSchedStage();

  // Finalize state after finishing a scheduling pass on the function.
  virtual void finalizeGCNSchedStage();

  // Setup for scheduling a region. Returns false if the current region should
  // be skipped.
  virtual bool initGCNRegion();

  // Track whether a new region is also a new MBB.
  void setupNewBlock();

  // Finalize state after scheudling a region.
  void finalizeGCNRegion();

  // Check result of scheduling.
  void checkScheduling();

  // computes the given schedule virtual execution time in clocks
  ScheduleMetrics getScheduleMetrics(const std::vector<SUnit> &InputSchedule);
  ScheduleMetrics getScheduleMetrics(const GCNScheduleDAGMILive &DAG);
  unsigned computeSUnitReadyCycle(const SUnit &SU, unsigned CurrCycle,
                                  DenseMap<unsigned, unsigned> &ReadyCycles,
                                  const TargetSchedModel &SM);

  // Returns true if scheduling should be reverted.
  virtual bool shouldRevertScheduling(unsigned WavesAfter);

  // Returns true if current region has known excess pressure.
  bool isRegionWithExcessRP() const {
    return DAG.RegionsWithExcessRP[RegionIdx];
  }

  // Returns true if the new schedule may result in more spilling.
  bool mayCauseSpilling(unsigned WavesAfter);

  // Attempt to revert scheduling for this region.
  void revertScheduling();

  void advanceRegion() { RegionIdx++; }

  virtual ~GCNSchedStage() = default;
};

class OccInitialScheduleStage : public GCNSchedStage {
public:
  bool shouldRevertScheduling(unsigned WavesAfter) override;

  OccInitialScheduleStage(GCNSchedStageID StageID, GCNScheduleDAGMILive &DAG)
      : GCNSchedStage(StageID, DAG) {}
};

class UnclusteredHighRPStage : public GCNSchedStage {
private:
  // Save the initial occupancy before starting this stage.
  unsigned InitialOccupancy;

public:
  bool initGCNSchedStage() override;

  void finalizeGCNSchedStage() override;

  bool initGCNRegion() override;

  bool shouldRevertScheduling(unsigned WavesAfter) override;

  UnclusteredHighRPStage(GCNSchedStageID StageID, GCNScheduleDAGMILive &DAG)
      : GCNSchedStage(StageID, DAG) {}
};

// Retry function scheduling if we found resulting occupancy and it is
// lower than used for other scheduling passes. This will give more freedom
// to schedule low register pressure blocks.
class ClusteredLowOccStage : public GCNSchedStage {
public:
  bool initGCNSchedStage() override;

  bool initGCNRegion() override;

  bool shouldRevertScheduling(unsigned WavesAfter) override;

  ClusteredLowOccStage(GCNSchedStageID StageID, GCNScheduleDAGMILive &DAG)
      : GCNSchedStage(StageID, DAG) {}
};

class PreRARematStage : public GCNSchedStage {
private:
  // Each region at MinOccupancy will have their own list of trivially
  // rematerializable instructions we can remat to reduce RP. The list maps an
  // instruction to the position we should remat before, usually the MI using
  // the rematerializable instruction.
  MapVector<unsigned, MapVector<MachineInstr *, MachineInstr *>>
      RematerializableInsts;

  // Map a trivially rematerializable def to a list of regions at MinOccupancy
  // that has the defined reg as a live-in.
  DenseMap<MachineInstr *, SmallVector<unsigned, 4>> RematDefToLiveInRegions;

  // Collect all trivially rematerializable VGPR instructions with a single def
  // and single use outside the defining block into RematerializableInsts.
  void collectRematerializableInstructions();

  bool isTriviallyReMaterializable(const MachineInstr &MI);

  // TODO: Should also attempt to reduce RP of SGPRs and AGPRs
  // Attempt to reduce RP of VGPR by sinking trivially rematerializable
  // instructions. Returns true if we were able to sink instruction(s).
  bool sinkTriviallyRematInsts(const GCNSubtarget &ST,
                               const TargetInstrInfo *TII);

public:
  bool initGCNSchedStage() override;

  bool initGCNRegion() override;

  bool shouldRevertScheduling(unsigned WavesAfter) override;

  PreRARematStage(GCNSchedStageID StageID, GCNScheduleDAGMILive &DAG)
      : GCNSchedStage(StageID, DAG) {}
};

class ILPInitialScheduleStage : public GCNSchedStage {
public:
  bool shouldRevertScheduling(unsigned WavesAfter) override;

  ILPInitialScheduleStage(GCNSchedStageID StageID, GCNScheduleDAGMILive &DAG)
      : GCNSchedStage(StageID, DAG) {}
};

class GCNPostScheduleDAGMILive final : public ScheduleDAGMI {
private:
  std::vector<std::unique_ptr<ScheduleDAGMutation>> SavedMutations;

  bool HasIGLPInstrs = false;

public:
  void schedule() override;

  void finalizeSchedule() override;

  GCNPostScheduleDAGMILive(MachineSchedContext *C,
                           std::unique_ptr<MachineSchedStrategy> S,
                           bool RemoveKillFlags);
};

} // End namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_GCNSCHEDSTRATEGY_H
