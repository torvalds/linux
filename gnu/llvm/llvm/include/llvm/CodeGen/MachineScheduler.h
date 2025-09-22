//===- MachineScheduler.h - MachineInstr Scheduling Pass --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an interface for customizing the standard MachineScheduler
// pass. Note that the entire pass may be replaced as follows:
//
// <Target>TargetMachine::createPassConfig(PassManagerBase &PM) {
//   PM.substitutePass(&MachineSchedulerID, &CustomSchedulerPassID);
//   ...}
//
// The MachineScheduler pass is only responsible for choosing the regions to be
// scheduled. Targets can override the DAG builder and scheduler without
// replacing the pass as follows:
//
// ScheduleDAGInstrs *<Target>PassConfig::
// createMachineScheduler(MachineSchedContext *C) {
//   return new CustomMachineScheduler(C);
// }
//
// The default scheduler, ScheduleDAGMILive, builds the DAG and drives list
// scheduling while updating the instruction stream, register pressure, and live
// intervals. Most targets don't need to override the DAG builder and list
// scheduler, but subtargets that require custom scheduling heuristics may
// plugin an alternate MachineSchedStrategy. The strategy is responsible for
// selecting the highest priority node from the list:
//
// ScheduleDAGInstrs *<Target>PassConfig::
// createMachineScheduler(MachineSchedContext *C) {
//   return new ScheduleDAGMILive(C, CustomStrategy(C));
// }
//
// The DAG builder can also be customized in a sense by adding DAG mutations
// that will run after DAG building and before list scheduling. DAG mutations
// can adjust dependencies based on target-specific knowledge or add weak edges
// to aid heuristics:
//
// ScheduleDAGInstrs *<Target>PassConfig::
// createMachineScheduler(MachineSchedContext *C) {
//   ScheduleDAGMI *DAG = createGenericSchedLive(C);
//   DAG->addMutation(new CustomDAGMutation(...));
//   return DAG;
// }
//
// A target that supports alternative schedulers can use the
// MachineSchedRegistry to allow command line selection. This can be done by
// implementing the following boilerplate:
//
// static ScheduleDAGInstrs *createCustomMachineSched(MachineSchedContext *C) {
//  return new CustomMachineScheduler(C);
// }
// static MachineSchedRegistry
// SchedCustomRegistry("custom", "Run my target's custom scheduler",
//                     createCustomMachineSched);
//
//
// Finally, subtargets that don't need to implement custom heuristics but would
// like to configure the GenericScheduler's policy for a given scheduler region,
// including scheduling direction and register pressure tracking policy, can do
// this:
//
// void <SubTarget>Subtarget::
// overrideSchedPolicy(MachineSchedPolicy &Policy,
//                     unsigned NumRegionInstrs) const {
//   Policy.<Flag> = true;
// }
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINESCHEDULER_H
#define LLVM_CODEGEN_MACHINESCHEDULER_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachinePassRegistry.h"
#include "llvm/CodeGen/RegisterPressure.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <string>
#include <vector>

namespace llvm {

extern cl::opt<bool> ForceTopDown;
extern cl::opt<bool> ForceBottomUp;
extern cl::opt<bool> VerifyScheduling;
#ifndef NDEBUG
extern cl::opt<bool> ViewMISchedDAGs;
extern cl::opt<bool> PrintDAGs;
#else
extern const bool ViewMISchedDAGs;
extern const bool PrintDAGs;
#endif

class AAResults;
class LiveIntervals;
class MachineDominatorTree;
class MachineFunction;
class MachineInstr;
class MachineLoopInfo;
class RegisterClassInfo;
class SchedDFSResult;
class ScheduleHazardRecognizer;
class TargetInstrInfo;
class TargetPassConfig;
class TargetRegisterInfo;

/// MachineSchedContext provides enough context from the MachineScheduler pass
/// for the target to instantiate a scheduler.
struct MachineSchedContext {
  MachineFunction *MF = nullptr;
  const MachineLoopInfo *MLI = nullptr;
  const MachineDominatorTree *MDT = nullptr;
  const TargetPassConfig *PassConfig = nullptr;
  AAResults *AA = nullptr;
  LiveIntervals *LIS = nullptr;

  RegisterClassInfo *RegClassInfo;

  MachineSchedContext();
  MachineSchedContext &operator=(const MachineSchedContext &other) = delete;
  MachineSchedContext(const MachineSchedContext &other) = delete;
  virtual ~MachineSchedContext();
};

/// MachineSchedRegistry provides a selection of available machine instruction
/// schedulers.
class MachineSchedRegistry
    : public MachinePassRegistryNode<
          ScheduleDAGInstrs *(*)(MachineSchedContext *)> {
public:
  using ScheduleDAGCtor = ScheduleDAGInstrs *(*)(MachineSchedContext *);

  // RegisterPassParser requires a (misnamed) FunctionPassCtor type.
  using FunctionPassCtor = ScheduleDAGCtor;

  static MachinePassRegistry<ScheduleDAGCtor> Registry;

  MachineSchedRegistry(const char *N, const char *D, ScheduleDAGCtor C)
      : MachinePassRegistryNode(N, D, C) {
    Registry.Add(this);
  }

  ~MachineSchedRegistry() { Registry.Remove(this); }

  // Accessors.
  //
  MachineSchedRegistry *getNext() const {
    return (MachineSchedRegistry *)MachinePassRegistryNode::getNext();
  }

  static MachineSchedRegistry *getList() {
    return (MachineSchedRegistry *)Registry.getList();
  }

  static void setListener(MachinePassRegistryListener<FunctionPassCtor> *L) {
    Registry.setListener(L);
  }
};

class ScheduleDAGMI;

/// Define a generic scheduling policy for targets that don't provide their own
/// MachineSchedStrategy. This can be overriden for each scheduling region
/// before building the DAG.
struct MachineSchedPolicy {
  // Allow the scheduler to disable register pressure tracking.
  bool ShouldTrackPressure = false;
  /// Track LaneMasks to allow reordering of independent subregister writes
  /// of the same vreg. \sa MachineSchedStrategy::shouldTrackLaneMasks()
  bool ShouldTrackLaneMasks = false;

  // Allow the scheduler to force top-down or bottom-up scheduling. If neither
  // is true, the scheduler runs in both directions and converges.
  bool OnlyTopDown = false;
  bool OnlyBottomUp = false;

  // Disable heuristic that tries to fetch nodes from long dependency chains
  // first.
  bool DisableLatencyHeuristic = false;

  // Compute DFSResult for use in scheduling heuristics.
  bool ComputeDFSResult = false;

  MachineSchedPolicy() = default;
};

/// MachineSchedStrategy - Interface to the scheduling algorithm used by
/// ScheduleDAGMI.
///
/// Initialization sequence:
///   initPolicy -> shouldTrackPressure -> initialize(DAG) -> registerRoots
class MachineSchedStrategy {
  virtual void anchor();

public:
  virtual ~MachineSchedStrategy() = default;

  /// Optionally override the per-region scheduling policy.
  virtual void initPolicy(MachineBasicBlock::iterator Begin,
                          MachineBasicBlock::iterator End,
                          unsigned NumRegionInstrs) {}

  virtual void dumpPolicy() const {}

  /// Check if pressure tracking is needed before building the DAG and
  /// initializing this strategy. Called after initPolicy.
  virtual bool shouldTrackPressure() const { return true; }

  /// Returns true if lanemasks should be tracked. LaneMask tracking is
  /// necessary to reorder independent subregister defs for the same vreg.
  /// This has to be enabled in combination with shouldTrackPressure().
  virtual bool shouldTrackLaneMasks() const { return false; }

  // If this method returns true, handling of the scheduling regions
  // themselves (in case of a scheduling boundary in MBB) will be done
  // beginning with the topmost region of MBB.
  virtual bool doMBBSchedRegionsTopDown() const { return false; }

  /// Initialize the strategy after building the DAG for a new region.
  virtual void initialize(ScheduleDAGMI *DAG) = 0;

  /// Tell the strategy that MBB is about to be processed.
  virtual void enterMBB(MachineBasicBlock *MBB) {};

  /// Tell the strategy that current MBB is done.
  virtual void leaveMBB() {};

  /// Notify this strategy that all roots have been released (including those
  /// that depend on EntrySU or ExitSU).
  virtual void registerRoots() {}

  /// Pick the next node to schedule, or return NULL. Set IsTopNode to true to
  /// schedule the node at the top of the unscheduled region. Otherwise it will
  /// be scheduled at the bottom.
  virtual SUnit *pickNode(bool &IsTopNode) = 0;

  /// Scheduler callback to notify that a new subtree is scheduled.
  virtual void scheduleTree(unsigned SubtreeID) {}

  /// Notify MachineSchedStrategy that ScheduleDAGMI has scheduled an
  /// instruction and updated scheduled/remaining flags in the DAG nodes.
  virtual void schedNode(SUnit *SU, bool IsTopNode) = 0;

  /// When all predecessor dependencies have been resolved, free this node for
  /// top-down scheduling.
  virtual void releaseTopNode(SUnit *SU) = 0;

  /// When all successor dependencies have been resolved, free this node for
  /// bottom-up scheduling.
  virtual void releaseBottomNode(SUnit *SU) = 0;
};

/// ScheduleDAGMI is an implementation of ScheduleDAGInstrs that simply
/// schedules machine instructions according to the given MachineSchedStrategy
/// without much extra book-keeping. This is the common functionality between
/// PreRA and PostRA MachineScheduler.
class ScheduleDAGMI : public ScheduleDAGInstrs {
protected:
  AAResults *AA;
  LiveIntervals *LIS;
  std::unique_ptr<MachineSchedStrategy> SchedImpl;

  /// Ordered list of DAG postprocessing steps.
  std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;

  /// The top of the unscheduled zone.
  MachineBasicBlock::iterator CurrentTop;

  /// The bottom of the unscheduled zone.
  MachineBasicBlock::iterator CurrentBottom;

  /// Record the next node in a scheduled cluster.
  const SUnit *NextClusterPred = nullptr;
  const SUnit *NextClusterSucc = nullptr;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  /// The number of instructions scheduled so far. Used to cut off the
  /// scheduler at the point determined by misched-cutoff.
  unsigned NumInstrsScheduled = 0;
#endif

public:
  ScheduleDAGMI(MachineSchedContext *C, std::unique_ptr<MachineSchedStrategy> S,
                bool RemoveKillFlags)
      : ScheduleDAGInstrs(*C->MF, C->MLI, RemoveKillFlags), AA(C->AA),
        LIS(C->LIS), SchedImpl(std::move(S)) {}

  // Provide a vtable anchor
  ~ScheduleDAGMI() override;

  /// If this method returns true, handling of the scheduling regions
  /// themselves (in case of a scheduling boundary in MBB) will be done
  /// beginning with the topmost region of MBB.
  bool doMBBSchedRegionsTopDown() const override {
    return SchedImpl->doMBBSchedRegionsTopDown();
  }

  // Returns LiveIntervals instance for use in DAG mutators and such.
  LiveIntervals *getLIS() const { return LIS; }

  /// Return true if this DAG supports VReg liveness and RegPressure.
  virtual bool hasVRegLiveness() const { return false; }

  /// Add a postprocessing step to the DAG builder.
  /// Mutations are applied in the order that they are added after normal DAG
  /// building and before MachineSchedStrategy initialization.
  ///
  /// ScheduleDAGMI takes ownership of the Mutation object.
  void addMutation(std::unique_ptr<ScheduleDAGMutation> Mutation) {
    if (Mutation)
      Mutations.push_back(std::move(Mutation));
  }

  MachineBasicBlock::iterator top() const { return CurrentTop; }
  MachineBasicBlock::iterator bottom() const { return CurrentBottom; }

  /// Implement the ScheduleDAGInstrs interface for handling the next scheduling
  /// region. This covers all instructions in a block, while schedule() may only
  /// cover a subset.
  void enterRegion(MachineBasicBlock *bb,
                   MachineBasicBlock::iterator begin,
                   MachineBasicBlock::iterator end,
                   unsigned regioninstrs) override;

  /// Implement ScheduleDAGInstrs interface for scheduling a sequence of
  /// reorderable instructions.
  void schedule() override;

  void startBlock(MachineBasicBlock *bb) override;
  void finishBlock() override;

  /// Change the position of an instruction within the basic block and update
  /// live ranges and region boundary iterators.
  void moveInstruction(MachineInstr *MI, MachineBasicBlock::iterator InsertPos);

  const SUnit *getNextClusterPred() const { return NextClusterPred; }

  const SUnit *getNextClusterSucc() const { return NextClusterSucc; }

  void viewGraph(const Twine &Name, const Twine &Title) override;
  void viewGraph() override;

protected:
  // Top-Level entry points for the schedule() driver...

  /// Apply each ScheduleDAGMutation step in order. This allows different
  /// instances of ScheduleDAGMI to perform custom DAG postprocessing.
  void postProcessDAG();

  /// Release ExitSU predecessors and setup scheduler queues.
  void initQueues(ArrayRef<SUnit*> TopRoots, ArrayRef<SUnit*> BotRoots);

  /// Update scheduler DAG and queues after scheduling an instruction.
  void updateQueues(SUnit *SU, bool IsTopNode);

  /// Reinsert debug_values recorded in ScheduleDAGInstrs::DbgValues.
  void placeDebugValues();

  /// dump the scheduled Sequence.
  void dumpSchedule() const;
  /// Print execution trace of the schedule top-down or bottom-up.
  void dumpScheduleTraceTopDown() const;
  void dumpScheduleTraceBottomUp() const;

  // Lesser helpers...
  bool checkSchedLimit();

  void findRootsAndBiasEdges(SmallVectorImpl<SUnit*> &TopRoots,
                             SmallVectorImpl<SUnit*> &BotRoots);

  void releaseSucc(SUnit *SU, SDep *SuccEdge);
  void releaseSuccessors(SUnit *SU);
  void releasePred(SUnit *SU, SDep *PredEdge);
  void releasePredecessors(SUnit *SU);
};

/// ScheduleDAGMILive is an implementation of ScheduleDAGInstrs that schedules
/// machine instructions while updating LiveIntervals and tracking regpressure.
class ScheduleDAGMILive : public ScheduleDAGMI {
protected:
  RegisterClassInfo *RegClassInfo;

  /// Information about DAG subtrees. If DFSResult is NULL, then SchedulerTrees
  /// will be empty.
  SchedDFSResult *DFSResult = nullptr;
  BitVector ScheduledTrees;

  MachineBasicBlock::iterator LiveRegionEnd;

  /// Maps vregs to the SUnits of their uses in the current scheduling region.
  VReg2SUnitMultiMap VRegUses;

  // Map each SU to its summary of pressure changes. This array is updated for
  // liveness during bottom-up scheduling. Top-down scheduling may proceed but
  // has no affect on the pressure diffs.
  PressureDiffs SUPressureDiffs;

  /// Register pressure in this region computed by initRegPressure.
  bool ShouldTrackPressure = false;
  bool ShouldTrackLaneMasks = false;
  IntervalPressure RegPressure;
  RegPressureTracker RPTracker;

  /// List of pressure sets that exceed the target's pressure limit before
  /// scheduling, listed in increasing set ID order. Each pressure set is paired
  /// with its max pressure in the currently scheduled regions.
  std::vector<PressureChange> RegionCriticalPSets;

  /// The top of the unscheduled zone.
  IntervalPressure TopPressure;
  RegPressureTracker TopRPTracker;

  /// The bottom of the unscheduled zone.
  IntervalPressure BotPressure;
  RegPressureTracker BotRPTracker;

public:
  ScheduleDAGMILive(MachineSchedContext *C,
                    std::unique_ptr<MachineSchedStrategy> S)
      : ScheduleDAGMI(C, std::move(S), /*RemoveKillFlags=*/false),
        RegClassInfo(C->RegClassInfo), RPTracker(RegPressure),
        TopRPTracker(TopPressure), BotRPTracker(BotPressure) {}

  ~ScheduleDAGMILive() override;

  /// Return true if this DAG supports VReg liveness and RegPressure.
  bool hasVRegLiveness() const override { return true; }

  /// Return true if register pressure tracking is enabled.
  bool isTrackingPressure() const { return ShouldTrackPressure; }

  /// Get current register pressure for the top scheduled instructions.
  const IntervalPressure &getTopPressure() const { return TopPressure; }
  const RegPressureTracker &getTopRPTracker() const { return TopRPTracker; }

  /// Get current register pressure for the bottom scheduled instructions.
  const IntervalPressure &getBotPressure() const { return BotPressure; }
  const RegPressureTracker &getBotRPTracker() const { return BotRPTracker; }

  /// Get register pressure for the entire scheduling region before scheduling.
  const IntervalPressure &getRegPressure() const { return RegPressure; }

  const std::vector<PressureChange> &getRegionCriticalPSets() const {
    return RegionCriticalPSets;
  }

  PressureDiff &getPressureDiff(const SUnit *SU) {
    return SUPressureDiffs[SU->NodeNum];
  }
  const PressureDiff &getPressureDiff(const SUnit *SU) const {
    return SUPressureDiffs[SU->NodeNum];
  }

  /// Compute a DFSResult after DAG building is complete, and before any
  /// queue comparisons.
  void computeDFSResult();

  /// Return a non-null DFS result if the scheduling strategy initialized it.
  const SchedDFSResult *getDFSResult() const { return DFSResult; }

  BitVector &getScheduledTrees() { return ScheduledTrees; }

  /// Implement the ScheduleDAGInstrs interface for handling the next scheduling
  /// region. This covers all instructions in a block, while schedule() may only
  /// cover a subset.
  void enterRegion(MachineBasicBlock *bb,
                   MachineBasicBlock::iterator begin,
                   MachineBasicBlock::iterator end,
                   unsigned regioninstrs) override;

  /// Implement ScheduleDAGInstrs interface for scheduling a sequence of
  /// reorderable instructions.
  void schedule() override;

  /// Compute the cyclic critical path through the DAG.
  unsigned computeCyclicCriticalPath();

  void dump() const override;

protected:
  // Top-Level entry points for the schedule() driver...

  /// Call ScheduleDAGInstrs::buildSchedGraph with register pressure tracking
  /// enabled. This sets up three trackers. RPTracker will cover the entire DAG
  /// region, TopTracker and BottomTracker will be initialized to the top and
  /// bottom of the DAG region without covereing any unscheduled instruction.
  void buildDAGWithRegPressure();

  /// Release ExitSU predecessors and setup scheduler queues. Re-position
  /// the Top RP tracker in case the region beginning has changed.
  void initQueues(ArrayRef<SUnit*> TopRoots, ArrayRef<SUnit*> BotRoots);

  /// Move an instruction and update register pressure.
  void scheduleMI(SUnit *SU, bool IsTopNode);

  // Lesser helpers...

  void initRegPressure();

  void updatePressureDiffs(ArrayRef<RegisterMaskPair> LiveUses);

  void updateScheduledPressure(const SUnit *SU,
                               const std::vector<unsigned> &NewMaxPressure);

  void collectVRegUses(SUnit &SU);
};

//===----------------------------------------------------------------------===//
///
/// Helpers for implementing custom MachineSchedStrategy classes. These take
/// care of the book-keeping associated with list scheduling heuristics.
///
//===----------------------------------------------------------------------===//

/// ReadyQueue encapsulates vector of "ready" SUnits with basic convenience
/// methods for pushing and removing nodes. ReadyQueue's are uniquely identified
/// by an ID. SUnit::NodeQueueId is a mask of the ReadyQueues the SUnit is in.
///
/// This is a convenience class that may be used by implementations of
/// MachineSchedStrategy.
class ReadyQueue {
  unsigned ID;
  std::string Name;
  std::vector<SUnit*> Queue;

public:
  ReadyQueue(unsigned id, const Twine &name): ID(id), Name(name.str()) {}

  unsigned getID() const { return ID; }

  StringRef getName() const { return Name; }

  // SU is in this queue if it's NodeQueueID is a superset of this ID.
  bool isInQueue(SUnit *SU) const { return (SU->NodeQueueId & ID); }

  bool empty() const { return Queue.empty(); }

  void clear() { Queue.clear(); }

  unsigned size() const { return Queue.size(); }

  using iterator = std::vector<SUnit*>::iterator;

  iterator begin() { return Queue.begin(); }

  iterator end() { return Queue.end(); }

  ArrayRef<SUnit*> elements() { return Queue; }

  iterator find(SUnit *SU) { return llvm::find(Queue, SU); }

  void push(SUnit *SU) {
    Queue.push_back(SU);
    SU->NodeQueueId |= ID;
  }

  iterator remove(iterator I) {
    (*I)->NodeQueueId &= ~ID;
    *I = Queue.back();
    unsigned idx = I - Queue.begin();
    Queue.pop_back();
    return Queue.begin() + idx;
  }

  void dump() const;
};

/// Summarize the unscheduled region.
struct SchedRemainder {
  // Critical path through the DAG in expected latency.
  unsigned CriticalPath;
  unsigned CyclicCritPath;

  // Scaled count of micro-ops left to schedule.
  unsigned RemIssueCount;

  bool IsAcyclicLatencyLimited;

  // Unscheduled resources
  SmallVector<unsigned, 16> RemainingCounts;

  SchedRemainder() { reset(); }

  void reset() {
    CriticalPath = 0;
    CyclicCritPath = 0;
    RemIssueCount = 0;
    IsAcyclicLatencyLimited = false;
    RemainingCounts.clear();
  }

  void init(ScheduleDAGMI *DAG, const TargetSchedModel *SchedModel);
};

/// ResourceSegments are a collection of intervals closed on the
/// left and opened on the right:
///
///     list{ [a1, b1), [a2, b2), ..., [a_N, b_N) }
///
/// The collection has the following properties:
///
/// 1. The list is ordered: a_i < b_i and b_i < a_(i+1)
///
/// 2. The intervals in the collection do not intersect each other.
///
/// A \ref ResourceSegments instance represents the cycle
/// reservation history of the instance of and individual resource.
class ResourceSegments {
public:
  /// Represents an interval of discrete integer values closed on
  /// the left and open on the right: [a, b).
  typedef std::pair<int64_t, int64_t> IntervalTy;

  /// Adds an interval [a, b) to the collection of the instance.
  ///
  /// When adding [a, b[ to the collection, the operation merges the
  /// adjacent intervals. For example
  ///
  ///       0  1  2  3  4  5  6  7  8  9  10
  ///       [-----)  [--)     [--)
  ///     +       [--)
  ///     = [-----------)     [--)
  ///
  /// To be able to debug duplicate resource usage, the function has
  /// assertion that checks that no interval should be added if it
  /// overlaps any of the intervals in the collection. We can
  /// require this because by definition a \ref ResourceSegments is
  /// attached only to an individual resource instance.
  void add(IntervalTy A, const unsigned CutOff = 10);

public:
  /// Checks whether intervals intersect.
  static bool intersects(IntervalTy A, IntervalTy B);

  /// These function return the interval used by a resource in bottom and top
  /// scheduling.
  ///
  /// Consider an instruction that uses resources X0, X1 and X2 as follows:
  ///
  /// X0 X1 X1 X2    +--------+-------------+--------------+
  ///                |Resource|AcquireAtCycle|ReleaseAtCycle|
  ///                +--------+-------------+--------------+
  ///                |   X0   |     0       |       1      |
  ///                +--------+-------------+--------------+
  ///                |   X1   |     1       |       3      |
  ///                +--------+-------------+--------------+
  ///                |   X2   |     3       |       4      |
  ///                +--------+-------------+--------------+
  ///
  /// If we can schedule the instruction at cycle C, we need to
  /// compute the interval of the resource as follows:
  ///
  /// # TOP DOWN SCHEDULING
  ///
  /// Cycles scheduling flows to the _right_, in the same direction
  /// of time.
  ///
  ///       C      1      2      3      4      5  ...
  /// ------|------|------|------|------|------|----->
  ///       X0     X1     X1     X2   ---> direction of time
  /// X0    [C, C+1)
  /// X1           [C+1,      C+3)
  /// X2                         [C+3, C+4)
  ///
  /// Therefore, the formula to compute the interval for a resource
  /// of an instruction that can be scheduled at cycle C in top-down
  /// scheduling is:
  ///
  ///       [C+AcquireAtCycle, C+ReleaseAtCycle)
  ///
  ///
  /// # BOTTOM UP SCHEDULING
  ///
  /// Cycles scheduling flows to the _left_, in opposite direction
  /// of time.
  ///
  /// In bottom up scheduling, the scheduling happens in opposite
  /// direction to the execution of the cycles of the
  /// instruction. When the instruction is scheduled at cycle `C`,
  /// the resources are allocated in the past relative to `C`:
  ///
  ///       2      1      C     -1     -2     -3     -4     -5  ...
  /// <-----|------|------|------|------|------|------|------|---
  ///                     X0     X1     X1     X2   ---> direction of time
  /// X0           (C+1, C]
  /// X1                  (C,        C-2]
  /// X2                              (C-2, C-3]
  ///
  /// Therefore, the formula to compute the interval for a resource
  /// of an instruction that can be scheduled at cycle C in bottom-up
  /// scheduling is:
  ///
  ///       [C-ReleaseAtCycle+1, C-AcquireAtCycle+1)
  ///
  ///
  /// NOTE: In both cases, the number of cycles booked by a
  /// resources is the value (ReleaseAtCycle - AcquireAtCycle).
  static IntervalTy getResourceIntervalBottom(unsigned C, unsigned AcquireAtCycle,
                                              unsigned ReleaseAtCycle) {
    return std::make_pair<long, long>((long)C - (long)ReleaseAtCycle + 1L,
                                      (long)C - (long)AcquireAtCycle + 1L);
  }
  static IntervalTy getResourceIntervalTop(unsigned C, unsigned AcquireAtCycle,
                                           unsigned ReleaseAtCycle) {
    return std::make_pair<long, long>((long)C + (long)AcquireAtCycle,
                                      (long)C + (long)ReleaseAtCycle);
  }

private:
  /// Finds the first cycle in which a resource can be allocated.
  ///
  /// The function uses the \param IntervalBuider [*] to build a
  /// resource interval [a, b[ out of the input parameters \param
  /// CurrCycle, \param AcquireAtCycle and \param ReleaseAtCycle.
  ///
  /// The function then loops through the intervals in the ResourceSegments
  /// and shifts the interval [a, b[ and the ReturnCycle to the
  /// right until there is no intersection between the intervals of
  /// the \ref ResourceSegments instance and the new shifted [a, b[. When
  /// this condition is met, the ReturnCycle  (which
  /// correspond to the cycle in which the resource can be
  /// allocated) is returned.
  ///
  ///               c = CurrCycle in input
  ///               c   1   2   3   4   5   6   7   8   9   10 ... ---> (time
  ///               flow)
  ///  ResourceSegments...  [---)   [-------)           [-----------)
  ///               c   [1     3[  -> AcquireAtCycle=1, ReleaseAtCycle=3
  ///                 ++c   [1     3)
  ///                     ++c   [1     3)
  ///                         ++c   [1     3)
  ///                             ++c   [1     3)
  ///                                 ++c   [1     3)    ---> returns c
  ///                                 incremented by 5 (c+5)
  ///
  ///
  /// Notice that for bottom-up scheduling the diagram is slightly
  /// different because the current cycle c is always on the right
  /// of the interval [a, b) (see \ref
  /// `getResourceIntervalBottom`). This is because the cycle
  /// increments for bottom-up scheduling moved in the direction
  /// opposite to the direction of time:
  ///
  ///     --------> direction of time.
  ///     XXYZZZ    (resource usage)
  ///     --------> direction of top-down execution cycles.
  ///     <-------- direction of bottom-up execution cycles.
  ///
  /// Even though bottom-up scheduling moves against the flow of
  /// time, the algorithm used to find the first free slot in between
  /// intervals is the same as for top-down scheduling.
  ///
  /// [*] See \ref `getResourceIntervalTop` and
  /// \ref `getResourceIntervalBottom` to see how such resource intervals
  /// are built.
  unsigned getFirstAvailableAt(
      unsigned CurrCycle, unsigned AcquireAtCycle, unsigned ReleaseAtCycle,
      std::function<IntervalTy(unsigned, unsigned, unsigned)> IntervalBuilder)
      const;

public:
  /// getFirstAvailableAtFromBottom and getFirstAvailableAtFromTop
  /// should be merged in a single function in which a function that
  /// creates the `NewInterval` is passed as a parameter.
  unsigned getFirstAvailableAtFromBottom(unsigned CurrCycle,
                                         unsigned AcquireAtCycle,
                                         unsigned ReleaseAtCycle) const {
    return getFirstAvailableAt(CurrCycle, AcquireAtCycle, ReleaseAtCycle,
                               getResourceIntervalBottom);
  }
  unsigned getFirstAvailableAtFromTop(unsigned CurrCycle,
                                      unsigned AcquireAtCycle,
                                      unsigned ReleaseAtCycle) const {
    return getFirstAvailableAt(CurrCycle, AcquireAtCycle, ReleaseAtCycle,
                               getResourceIntervalTop);
  }

private:
  std::list<IntervalTy> _Intervals;
  /// Merge all adjacent intervals in the collection. For all pairs
  /// of adjacient intervals, it performs [a, b) + [b, c) -> [a, c).
  ///
  /// Before performing the merge operation, the intervals are
  /// sorted with \ref sort_predicate.
  void sortAndMerge();

public:
  // constructor for empty set
  explicit ResourceSegments(){};
  bool empty() const { return _Intervals.empty(); }
  explicit ResourceSegments(const std::list<IntervalTy> &Intervals)
      : _Intervals(Intervals) {
    sortAndMerge();
  }

  friend bool operator==(const ResourceSegments &c1,
                         const ResourceSegments &c2) {
    return c1._Intervals == c2._Intervals;
  }
  friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                       const ResourceSegments &Segments) {
    os << "{ ";
    for (auto p : Segments._Intervals)
      os << "[" << p.first << ", " << p.second << "), ";
    os << "}\n";
    return os;
  }
};

/// Each Scheduling boundary is associated with ready queues. It tracks the
/// current cycle in the direction of movement, and maintains the state
/// of "hazards" and other interlocks at the current cycle.
class SchedBoundary {
public:
  /// SUnit::NodeQueueId: 0 (none), 1 (top), 2 (bot), 3 (both)
  enum {
    TopQID = 1,
    BotQID = 2,
    LogMaxQID = 2
  };

  ScheduleDAGMI *DAG = nullptr;
  const TargetSchedModel *SchedModel = nullptr;
  SchedRemainder *Rem = nullptr;

  ReadyQueue Available;
  ReadyQueue Pending;

  ScheduleHazardRecognizer *HazardRec = nullptr;

private:
  /// True if the pending Q should be checked/updated before scheduling another
  /// instruction.
  bool CheckPending;

  /// Number of cycles it takes to issue the instructions scheduled in this
  /// zone. It is defined as: scheduled-micro-ops / issue-width + stalls.
  /// See getStalls().
  unsigned CurrCycle;

  /// Micro-ops issued in the current cycle
  unsigned CurrMOps;

  /// MinReadyCycle - Cycle of the soonest available instruction.
  unsigned MinReadyCycle;

  // The expected latency of the critical path in this scheduled zone.
  unsigned ExpectedLatency;

  // The latency of dependence chains leading into this zone.
  // For each node scheduled bottom-up: DLat = max DLat, N.Depth.
  // For each cycle scheduled: DLat -= 1.
  unsigned DependentLatency;

  /// Count the scheduled (issued) micro-ops that can be retired by
  /// time=CurrCycle assuming the first scheduled instr is retired at time=0.
  unsigned RetiredMOps;

  // Count scheduled resources that have been executed. Resources are
  // considered executed if they become ready in the time that it takes to
  // saturate any resource including the one in question. Counts are scaled
  // for direct comparison with other resources. Counts can be compared with
  // MOps * getMicroOpFactor and Latency * getLatencyFactor.
  SmallVector<unsigned, 16> ExecutedResCounts;

  /// Cache the max count for a single resource.
  unsigned MaxExecutedResCount;

  // Cache the critical resources ID in this scheduled zone.
  unsigned ZoneCritResIdx;

  // Is the scheduled region resource limited vs. latency limited.
  bool IsResourceLimited;

public:
private:
  /// Record how resources have been allocated across the cycles of
  /// the execution.
  std::map<unsigned, ResourceSegments> ReservedResourceSegments;
  std::vector<unsigned> ReservedCycles;
  /// For each PIdx, stores first index into ReservedResourceSegments that
  /// corresponds to it.
  ///
  /// For example, consider the following 3 resources (ResourceCount =
  /// 3):
  ///
  ///   +------------+--------+
  ///   |ResourceName|NumUnits|
  ///   +------------+--------+
  ///   |     X      |    2   |
  ///   +------------+--------+
  ///   |     Y      |    3   |
  ///   +------------+--------+
  ///   |     Z      |    1   |
  ///   +------------+--------+
  ///
  /// In this case, the total number of resource instances is 6. The
  /// vector \ref ReservedResourceSegments will have a slot for each instance.
  /// The vector \ref ReservedCyclesIndex will track at what index the first
  /// instance of the resource is found in the vector of \ref
  /// ReservedResourceSegments:
  ///
  ///                              Indexes of instances in
  ///                              ReservedResourceSegments
  ///
  ///                              0   1   2   3   4  5
  /// ReservedCyclesIndex[0] = 0; [X0, X1,
  /// ReservedCyclesIndex[1] = 2;          Y0, Y1, Y2
  /// ReservedCyclesIndex[2] = 5;                     Z
  SmallVector<unsigned, 16> ReservedCyclesIndex;

  // For each PIdx, stores the resource group IDs of its subunits
  SmallVector<APInt, 16> ResourceGroupSubUnitMasks;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  // Remember the greatest possible stall as an upper bound on the number of
  // times we should retry the pending queue because of a hazard.
  unsigned MaxObservedStall;
#endif

public:
  /// Pending queues extend the ready queues with the same ID and the
  /// PendingFlag set.
  SchedBoundary(unsigned ID, const Twine &Name):
    Available(ID, Name+".A"), Pending(ID << LogMaxQID, Name+".P") {
    reset();
  }
  SchedBoundary &operator=(const SchedBoundary &other) = delete;
  SchedBoundary(const SchedBoundary &other) = delete;
  ~SchedBoundary();

  void reset();

  void init(ScheduleDAGMI *dag, const TargetSchedModel *smodel,
            SchedRemainder *rem);

  bool isTop() const {
    return Available.getID() == TopQID;
  }

  /// Number of cycles to issue the instructions scheduled in this zone.
  unsigned getCurrCycle() const { return CurrCycle; }

  /// Micro-ops issued in the current cycle
  unsigned getCurrMOps() const { return CurrMOps; }

  // The latency of dependence chains leading into this zone.
  unsigned getDependentLatency() const { return DependentLatency; }

  /// Get the number of latency cycles "covered" by the scheduled
  /// instructions. This is the larger of the critical path within the zone
  /// and the number of cycles required to issue the instructions.
  unsigned getScheduledLatency() const {
    return std::max(ExpectedLatency, CurrCycle);
  }

  unsigned getUnscheduledLatency(SUnit *SU) const {
    return isTop() ? SU->getHeight() : SU->getDepth();
  }

  unsigned getResourceCount(unsigned ResIdx) const {
    return ExecutedResCounts[ResIdx];
  }

  /// Get the scaled count of scheduled micro-ops and resources, including
  /// executed resources.
  unsigned getCriticalCount() const {
    if (!ZoneCritResIdx)
      return RetiredMOps * SchedModel->getMicroOpFactor();
    return getResourceCount(ZoneCritResIdx);
  }

  /// Get a scaled count for the minimum execution time of the scheduled
  /// micro-ops that are ready to execute by getExecutedCount. Notice the
  /// feedback loop.
  unsigned getExecutedCount() const {
    return std::max(CurrCycle * SchedModel->getLatencyFactor(),
                    MaxExecutedResCount);
  }

  unsigned getZoneCritResIdx() const { return ZoneCritResIdx; }

  // Is the scheduled region resource limited vs. latency limited.
  bool isResourceLimited() const { return IsResourceLimited; }

  /// Get the difference between the given SUnit's ready time and the current
  /// cycle.
  unsigned getLatencyStallCycles(SUnit *SU);

  unsigned getNextResourceCycleByInstance(unsigned InstanceIndex,
                                          unsigned ReleaseAtCycle,
                                          unsigned AcquireAtCycle);

  std::pair<unsigned, unsigned> getNextResourceCycle(const MCSchedClassDesc *SC,
                                                     unsigned PIdx,
                                                     unsigned ReleaseAtCycle,
                                                     unsigned AcquireAtCycle);

  bool isUnbufferedGroup(unsigned PIdx) const {
    return SchedModel->getProcResource(PIdx)->SubUnitsIdxBegin &&
           !SchedModel->getProcResource(PIdx)->BufferSize;
  }

  bool checkHazard(SUnit *SU);

  unsigned findMaxLatency(ArrayRef<SUnit*> ReadySUs);

  unsigned getOtherResourceCount(unsigned &OtherCritIdx);

  /// Release SU to make it ready. If it's not in hazard, remove it from
  /// pending queue (if already in) and push into available queue.
  /// Otherwise, push the SU into pending queue.
  ///
  /// @param SU The unit to be released.
  /// @param ReadyCycle Until which cycle the unit is ready.
  /// @param InPQueue Whether SU is already in pending queue.
  /// @param Idx Position offset in pending queue (if in it).
  void releaseNode(SUnit *SU, unsigned ReadyCycle, bool InPQueue,
                   unsigned Idx = 0);

  void bumpCycle(unsigned NextCycle);

  void incExecutedResources(unsigned PIdx, unsigned Count);

  unsigned countResource(const MCSchedClassDesc *SC, unsigned PIdx,
                         unsigned Cycles, unsigned ReadyCycle,
                         unsigned StartAtCycle);

  void bumpNode(SUnit *SU);

  void releasePending();

  void removeReady(SUnit *SU);

  /// Call this before applying any other heuristics to the Available queue.
  /// Updates the Available/Pending Q's if necessary and returns the single
  /// available instruction, or NULL if there are multiple candidates.
  SUnit *pickOnlyChoice();

  /// Dump the state of the information that tracks resource usage.
  void dumpReservedCycles() const;
  void dumpScheduledState() const;
};

/// Base class for GenericScheduler. This class maintains information about
/// scheduling candidates based on TargetSchedModel making it easy to implement
/// heuristics for either preRA or postRA scheduling.
class GenericSchedulerBase : public MachineSchedStrategy {
public:
  /// Represent the type of SchedCandidate found within a single queue.
  /// pickNodeBidirectional depends on these listed by decreasing priority.
  enum CandReason : uint8_t {
    NoCand, Only1, PhysReg, RegExcess, RegCritical, Stall, Cluster, Weak,
    RegMax, ResourceReduce, ResourceDemand, BotHeightReduce, BotPathReduce,
    TopDepthReduce, TopPathReduce, NextDefUse, NodeOrder};

#ifndef NDEBUG
  static const char *getReasonStr(GenericSchedulerBase::CandReason Reason);
#endif

  /// Policy for scheduling the next instruction in the candidate's zone.
  struct CandPolicy {
    bool ReduceLatency = false;
    unsigned ReduceResIdx = 0;
    unsigned DemandResIdx = 0;

    CandPolicy() = default;

    bool operator==(const CandPolicy &RHS) const {
      return ReduceLatency == RHS.ReduceLatency &&
             ReduceResIdx == RHS.ReduceResIdx &&
             DemandResIdx == RHS.DemandResIdx;
    }
    bool operator!=(const CandPolicy &RHS) const {
      return !(*this == RHS);
    }
  };

  /// Status of an instruction's critical resource consumption.
  struct SchedResourceDelta {
    // Count critical resources in the scheduled region required by SU.
    unsigned CritResources = 0;

    // Count critical resources from another region consumed by SU.
    unsigned DemandedResources = 0;

    SchedResourceDelta() = default;

    bool operator==(const SchedResourceDelta &RHS) const {
      return CritResources == RHS.CritResources
        && DemandedResources == RHS.DemandedResources;
    }
    bool operator!=(const SchedResourceDelta &RHS) const {
      return !operator==(RHS);
    }
  };

  /// Store the state used by GenericScheduler heuristics, required for the
  /// lifetime of one invocation of pickNode().
  struct SchedCandidate {
    CandPolicy Policy;

    // The best SUnit candidate.
    SUnit *SU;

    // The reason for this candidate.
    CandReason Reason;

    // Whether this candidate should be scheduled at top/bottom.
    bool AtTop;

    // Register pressure values for the best candidate.
    RegPressureDelta RPDelta;

    // Critical resource consumption of the best candidate.
    SchedResourceDelta ResDelta;

    SchedCandidate() { reset(CandPolicy()); }
    SchedCandidate(const CandPolicy &Policy) { reset(Policy); }

    void reset(const CandPolicy &NewPolicy) {
      Policy = NewPolicy;
      SU = nullptr;
      Reason = NoCand;
      AtTop = false;
      RPDelta = RegPressureDelta();
      ResDelta = SchedResourceDelta();
    }

    bool isValid() const { return SU; }

    // Copy the status of another candidate without changing policy.
    void setBest(SchedCandidate &Best) {
      assert(Best.Reason != NoCand && "uninitialized Sched candidate");
      SU = Best.SU;
      Reason = Best.Reason;
      AtTop = Best.AtTop;
      RPDelta = Best.RPDelta;
      ResDelta = Best.ResDelta;
    }

    void initResourceDelta(const ScheduleDAGMI *DAG,
                           const TargetSchedModel *SchedModel);
  };

protected:
  const MachineSchedContext *Context;
  const TargetSchedModel *SchedModel = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

  SchedRemainder Rem;

  GenericSchedulerBase(const MachineSchedContext *C) : Context(C) {}

  void setPolicy(CandPolicy &Policy, bool IsPostRA, SchedBoundary &CurrZone,
                 SchedBoundary *OtherZone);

#ifndef NDEBUG
  void traceCandidate(const SchedCandidate &Cand);
#endif

private:
  bool shouldReduceLatency(const CandPolicy &Policy, SchedBoundary &CurrZone,
                           bool ComputeRemLatency, unsigned &RemLatency) const;
};

// Utility functions used by heuristics in tryCandidate().
bool tryLess(int TryVal, int CandVal,
             GenericSchedulerBase::SchedCandidate &TryCand,
             GenericSchedulerBase::SchedCandidate &Cand,
             GenericSchedulerBase::CandReason Reason);
bool tryGreater(int TryVal, int CandVal,
                GenericSchedulerBase::SchedCandidate &TryCand,
                GenericSchedulerBase::SchedCandidate &Cand,
                GenericSchedulerBase::CandReason Reason);
bool tryLatency(GenericSchedulerBase::SchedCandidate &TryCand,
                GenericSchedulerBase::SchedCandidate &Cand,
                SchedBoundary &Zone);
bool tryPressure(const PressureChange &TryP,
                 const PressureChange &CandP,
                 GenericSchedulerBase::SchedCandidate &TryCand,
                 GenericSchedulerBase::SchedCandidate &Cand,
                 GenericSchedulerBase::CandReason Reason,
                 const TargetRegisterInfo *TRI,
                 const MachineFunction &MF);
unsigned getWeakLeft(const SUnit *SU, bool isTop);
int biasPhysReg(const SUnit *SU, bool isTop);

/// GenericScheduler shrinks the unscheduled zone using heuristics to balance
/// the schedule.
class GenericScheduler : public GenericSchedulerBase {
public:
  GenericScheduler(const MachineSchedContext *C):
    GenericSchedulerBase(C), Top(SchedBoundary::TopQID, "TopQ"),
    Bot(SchedBoundary::BotQID, "BotQ") {}

  void initPolicy(MachineBasicBlock::iterator Begin,
                  MachineBasicBlock::iterator End,
                  unsigned NumRegionInstrs) override;

  void dumpPolicy() const override;

  bool shouldTrackPressure() const override {
    return RegionPolicy.ShouldTrackPressure;
  }

  bool shouldTrackLaneMasks() const override {
    return RegionPolicy.ShouldTrackLaneMasks;
  }

  void initialize(ScheduleDAGMI *dag) override;

  SUnit *pickNode(bool &IsTopNode) override;

  void schedNode(SUnit *SU, bool IsTopNode) override;

  void releaseTopNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;

    Top.releaseNode(SU, SU->TopReadyCycle, false);
    TopCand.SU = nullptr;
  }

  void releaseBottomNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;

    Bot.releaseNode(SU, SU->BotReadyCycle, false);
    BotCand.SU = nullptr;
  }

  void registerRoots() override;

protected:
  ScheduleDAGMILive *DAG = nullptr;

  MachineSchedPolicy RegionPolicy;

  // State of the top and bottom scheduled instruction boundaries.
  SchedBoundary Top;
  SchedBoundary Bot;

  /// Candidate last picked from Top boundary.
  SchedCandidate TopCand;
  /// Candidate last picked from Bot boundary.
  SchedCandidate BotCand;

  void checkAcyclicLatency();

  void initCandidate(SchedCandidate &Cand, SUnit *SU, bool AtTop,
                     const RegPressureTracker &RPTracker,
                     RegPressureTracker &TempTracker);

  virtual bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand,
                            SchedBoundary *Zone) const;

  SUnit *pickNodeBidirectional(bool &IsTopNode);

  void pickNodeFromQueue(SchedBoundary &Zone,
                         const CandPolicy &ZonePolicy,
                         const RegPressureTracker &RPTracker,
                         SchedCandidate &Candidate);

  void reschedulePhysReg(SUnit *SU, bool isTop);
};

/// PostGenericScheduler - Interface to the scheduling algorithm used by
/// ScheduleDAGMI.
///
/// Callbacks from ScheduleDAGMI:
///   initPolicy -> initialize(DAG) -> registerRoots -> pickNode ...
class PostGenericScheduler : public GenericSchedulerBase {
protected:
  ScheduleDAGMI *DAG = nullptr;
  SchedBoundary Top;
  SchedBoundary Bot;
  MachineSchedPolicy RegionPolicy;

  /// Candidate last picked from Top boundary.
  SchedCandidate TopCand;
  /// Candidate last picked from Bot boundary.
  SchedCandidate BotCand;

public:
  PostGenericScheduler(const MachineSchedContext *C)
      : GenericSchedulerBase(C), Top(SchedBoundary::TopQID, "TopQ"),
        Bot(SchedBoundary::BotQID, "BotQ") {}

  ~PostGenericScheduler() override = default;

  void initPolicy(MachineBasicBlock::iterator Begin,
                  MachineBasicBlock::iterator End,
                  unsigned NumRegionInstrs) override;

  /// PostRA scheduling does not track pressure.
  bool shouldTrackPressure() const override { return false; }

  void initialize(ScheduleDAGMI *Dag) override;

  void registerRoots() override;

  SUnit *pickNode(bool &IsTopNode) override;

  SUnit *pickNodeBidirectional(bool &IsTopNode);

  void scheduleTree(unsigned SubtreeID) override {
    llvm_unreachable("PostRA scheduler does not support subtree analysis.");
  }

  void schedNode(SUnit *SU, bool IsTopNode) override;

  void releaseTopNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;
    Top.releaseNode(SU, SU->TopReadyCycle, false);
    TopCand.SU = nullptr;
  }

  void releaseBottomNode(SUnit *SU) override {
    if (SU->isScheduled)
      return;
    Bot.releaseNode(SU, SU->BotReadyCycle, false);
    BotCand.SU = nullptr;
  }

protected:
  virtual bool tryCandidate(SchedCandidate &Cand, SchedCandidate &TryCand);

  void pickNodeFromQueue(SchedBoundary &Zone, SchedCandidate &Cand);
};

/// Create the standard converging machine scheduler. This will be used as the
/// default scheduler if the target does not set a default.
/// Adds default DAG mutations.
ScheduleDAGMILive *createGenericSchedLive(MachineSchedContext *C);

/// Create a generic scheduler with no vreg liveness or DAG mutation passes.
ScheduleDAGMI *createGenericSchedPostRA(MachineSchedContext *C);

/// If ReorderWhileClustering is set to true, no attempt will be made to
/// reduce reordering due to store clustering.
std::unique_ptr<ScheduleDAGMutation>
createLoadClusterDAGMutation(const TargetInstrInfo *TII,
                             const TargetRegisterInfo *TRI,
                             bool ReorderWhileClustering = false);

/// If ReorderWhileClustering is set to true, no attempt will be made to
/// reduce reordering due to store clustering.
std::unique_ptr<ScheduleDAGMutation>
createStoreClusterDAGMutation(const TargetInstrInfo *TII,
                              const TargetRegisterInfo *TRI,
                              bool ReorderWhileClustering = false);

std::unique_ptr<ScheduleDAGMutation>
createCopyConstrainDAGMutation(const TargetInstrInfo *TII,
                               const TargetRegisterInfo *TRI);

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINESCHEDULER_H
