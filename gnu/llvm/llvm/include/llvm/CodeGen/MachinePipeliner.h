//===- MachinePipeliner.h - Machine Software Pipeliner Pass -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// An implementation of the Swing Modulo Scheduling (SMS) software pipeliner.
//
// Software pipelining (SWP) is an instruction scheduling technique for loops
// that overlap loop iterations and exploits ILP via a compiler transformation.
//
// Swing Modulo Scheduling is an implementation of software pipelining
// that generates schedules that are near optimal in terms of initiation
// interval, register requirements, and stage count. See the papers:
//
// "Swing Modulo Scheduling: A Lifetime-Sensitive Approach", by J. Llosa,
// A. Gonzalez, E. Ayguade, and M. Valero. In PACT '96 Proceedings of the 1996
// Conference on Parallel Architectures and Compilation Techiniques.
//
// "Lifetime-Sensitive Modulo Scheduling in a Production Environment", by J.
// Llosa, E. Ayguade, A. Gonzalez, M. Valero, and J. Eckhardt. In IEEE
// Transactions on Computers, Vol. 50, No. 3, 2001.
//
// "An Implementation of Swing Modulo Scheduling With Extensions for
// Superblocks", by T. Lattner, Master's Thesis, University of Illinois at
// Urbana-Champaign, 2005.
//
//
// The SMS algorithm consists of three main steps after computing the minimal
// initiation interval (MII).
// 1) Analyze the dependence graph and compute information about each
//    instruction in the graph.
// 2) Order the nodes (instructions) by priority based upon the heuristics
//    described in the algorithm.
// 3) Attempt to schedule the nodes in the specified order using the MII.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CODEGEN_MACHINEPIPELINER_H
#define LLVM_CODEGEN_MACHINEPIPELINER_H

#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/DFAPacketizer.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/WindowScheduler.h"
#include "llvm/InitializePasses.h"

#include <deque>

namespace llvm {

class AAResults;
class NodeSet;
class SMSchedule;

extern cl::opt<bool> SwpEnableCopyToPhi;
extern cl::opt<int> SwpForceIssueWidth;

/// The main class in the implementation of the target independent
/// software pipeliner pass.
class MachinePipeliner : public MachineFunctionPass {
public:
  MachineFunction *MF = nullptr;
  MachineOptimizationRemarkEmitter *ORE = nullptr;
  const MachineLoopInfo *MLI = nullptr;
  const MachineDominatorTree *MDT = nullptr;
  const InstrItineraryData *InstrItins = nullptr;
  const TargetInstrInfo *TII = nullptr;
  RegisterClassInfo RegClassInfo;
  bool disabledByPragma = false;
  unsigned II_setByPragma = 0;

#ifndef NDEBUG
  static int NumTries;
#endif

  /// Cache the target analysis information about the loop.
  struct LoopInfo {
    MachineBasicBlock *TBB = nullptr;
    MachineBasicBlock *FBB = nullptr;
    SmallVector<MachineOperand, 4> BrCond;
    MachineInstr *LoopInductionVar = nullptr;
    MachineInstr *LoopCompare = nullptr;
    std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo> LoopPipelinerInfo =
        nullptr;
  };
  LoopInfo LI;

  static char ID;

  MachinePipeliner() : MachineFunctionPass(ID) {
    initializeMachinePipelinerPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  void preprocessPhiNodes(MachineBasicBlock &B);
  bool canPipelineLoop(MachineLoop &L);
  bool scheduleLoop(MachineLoop &L);
  bool swingModuloScheduler(MachineLoop &L);
  void setPragmaPipelineOptions(MachineLoop &L);
  bool runWindowScheduler(MachineLoop &L);
  bool useSwingModuloScheduler();
  bool useWindowScheduler(bool Changed);
};

/// This class builds the dependence graph for the instructions in a loop,
/// and attempts to schedule the instructions using the SMS algorithm.
class SwingSchedulerDAG : public ScheduleDAGInstrs {
  MachinePipeliner &Pass;
  /// The minimum initiation interval between iterations for this schedule.
  unsigned MII = 0;
  /// The maximum initiation interval between iterations for this schedule.
  unsigned MAX_II = 0;
  /// Set to true if a valid pipelined schedule is found for the loop.
  bool Scheduled = false;
  MachineLoop &Loop;
  LiveIntervals &LIS;
  const RegisterClassInfo &RegClassInfo;
  unsigned II_setByPragma = 0;
  TargetInstrInfo::PipelinerLoopInfo *LoopPipelinerInfo = nullptr;

  /// A toplogical ordering of the SUnits, which is needed for changing
  /// dependences and iterating over the SUnits.
  ScheduleDAGTopologicalSort Topo;

  struct NodeInfo {
    int ASAP = 0;
    int ALAP = 0;
    int ZeroLatencyDepth = 0;
    int ZeroLatencyHeight = 0;

    NodeInfo() = default;
  };
  /// Computed properties for each node in the graph.
  std::vector<NodeInfo> ScheduleInfo;

  enum OrderKind { BottomUp = 0, TopDown = 1 };
  /// Computed node ordering for scheduling.
  SetVector<SUnit *> NodeOrder;

  using NodeSetType = SmallVector<NodeSet, 8>;
  using ValueMapTy = DenseMap<unsigned, unsigned>;
  using MBBVectorTy = SmallVectorImpl<MachineBasicBlock *>;
  using InstrMapTy = DenseMap<MachineInstr *, MachineInstr *>;

  /// Instructions to change when emitting the final schedule.
  DenseMap<SUnit *, std::pair<unsigned, int64_t>> InstrChanges;

  /// We may create a new instruction, so remember it because it
  /// must be deleted when the pass is finished.
  DenseMap<MachineInstr*, MachineInstr *> NewMIs;

  /// Ordered list of DAG postprocessing steps.
  std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;

  /// Helper class to implement Johnson's circuit finding algorithm.
  class Circuits {
    std::vector<SUnit> &SUnits;
    SetVector<SUnit *> Stack;
    BitVector Blocked;
    SmallVector<SmallPtrSet<SUnit *, 4>, 10> B;
    SmallVector<SmallVector<int, 4>, 16> AdjK;
    // Node to Index from ScheduleDAGTopologicalSort
    std::vector<int> *Node2Idx;
    unsigned NumPaths = 0u;
    static unsigned MaxPaths;

  public:
    Circuits(std::vector<SUnit> &SUs, ScheduleDAGTopologicalSort &Topo)
        : SUnits(SUs), Blocked(SUs.size()), B(SUs.size()), AdjK(SUs.size()) {
      Node2Idx = new std::vector<int>(SUs.size());
      unsigned Idx = 0;
      for (const auto &NodeNum : Topo)
        Node2Idx->at(NodeNum) = Idx++;
    }
    Circuits &operator=(const Circuits &other) = delete;
    Circuits(const Circuits &other) = delete;
    ~Circuits() { delete Node2Idx; }

    /// Reset the data structures used in the circuit algorithm.
    void reset() {
      Stack.clear();
      Blocked.reset();
      B.assign(SUnits.size(), SmallPtrSet<SUnit *, 4>());
      NumPaths = 0;
    }

    void createAdjacencyStructure(SwingSchedulerDAG *DAG);
    bool circuit(int V, int S, NodeSetType &NodeSets, bool HasBackedge = false);
    void unblock(int U);
  };

  struct CopyToPhiMutation : public ScheduleDAGMutation {
    void apply(ScheduleDAGInstrs *DAG) override;
  };

public:
  SwingSchedulerDAG(MachinePipeliner &P, MachineLoop &L, LiveIntervals &lis,
                    const RegisterClassInfo &rci, unsigned II,
                    TargetInstrInfo::PipelinerLoopInfo *PLI)
      : ScheduleDAGInstrs(*P.MF, P.MLI, false), Pass(P), Loop(L), LIS(lis),
        RegClassInfo(rci), II_setByPragma(II), LoopPipelinerInfo(PLI),
        Topo(SUnits, &ExitSU) {
    P.MF->getSubtarget().getSMSMutations(Mutations);
    if (SwpEnableCopyToPhi)
      Mutations.push_back(std::make_unique<CopyToPhiMutation>());
  }

  void schedule() override;
  void finishBlock() override;

  /// Return true if the loop kernel has been scheduled.
  bool hasNewSchedule() { return Scheduled; }

  /// Return the earliest time an instruction may be scheduled.
  int getASAP(SUnit *Node) { return ScheduleInfo[Node->NodeNum].ASAP; }

  /// Return the latest time an instruction my be scheduled.
  int getALAP(SUnit *Node) { return ScheduleInfo[Node->NodeNum].ALAP; }

  /// The mobility function, which the number of slots in which
  /// an instruction may be scheduled.
  int getMOV(SUnit *Node) { return getALAP(Node) - getASAP(Node); }

  /// The depth, in the dependence graph, for a node.
  unsigned getDepth(SUnit *Node) { return Node->getDepth(); }

  /// The maximum unweighted length of a path from an arbitrary node to the
  /// given node in which each edge has latency 0
  int getZeroLatencyDepth(SUnit *Node) {
    return ScheduleInfo[Node->NodeNum].ZeroLatencyDepth;
  }

  /// The height, in the dependence graph, for a node.
  unsigned getHeight(SUnit *Node) { return Node->getHeight(); }

  /// The maximum unweighted length of a path from the given node to an
  /// arbitrary node in which each edge has latency 0
  int getZeroLatencyHeight(SUnit *Node) {
    return ScheduleInfo[Node->NodeNum].ZeroLatencyHeight;
  }

  /// Return true if the dependence is a back-edge in the data dependence graph.
  /// Since the DAG doesn't contain cycles, we represent a cycle in the graph
  /// using an anti dependence from a Phi to an instruction.
  bool isBackedge(SUnit *Source, const SDep &Dep) {
    if (Dep.getKind() != SDep::Anti)
      return false;
    return Source->getInstr()->isPHI() || Dep.getSUnit()->getInstr()->isPHI();
  }

  bool isLoopCarriedDep(SUnit *Source, const SDep &Dep, bool isSucc = true);

  /// The distance function, which indicates that operation V of iteration I
  /// depends on operations U of iteration I-distance.
  unsigned getDistance(SUnit *U, SUnit *V, const SDep &Dep) {
    // Instructions that feed a Phi have a distance of 1. Computing larger
    // values for arrays requires data dependence information.
    if (V->getInstr()->isPHI() && Dep.getKind() == SDep::Anti)
      return 1;
    return 0;
  }

  void applyInstrChange(MachineInstr *MI, SMSchedule &Schedule);

  void fixupRegisterOverlaps(std::deque<SUnit *> &Instrs);

  /// Return the new base register that was stored away for the changed
  /// instruction.
  unsigned getInstrBaseReg(SUnit *SU) const {
    DenseMap<SUnit *, std::pair<unsigned, int64_t>>::const_iterator It =
        InstrChanges.find(SU);
    if (It != InstrChanges.end())
      return It->second.first;
    return 0;
  }

  void addMutation(std::unique_ptr<ScheduleDAGMutation> Mutation) {
    Mutations.push_back(std::move(Mutation));
  }

  static bool classof(const ScheduleDAGInstrs *DAG) { return true; }

private:
  void addLoopCarriedDependences(AAResults *AA);
  void updatePhiDependences();
  void changeDependences();
  unsigned calculateResMII();
  unsigned calculateRecMII(NodeSetType &RecNodeSets);
  void findCircuits(NodeSetType &NodeSets);
  void fuseRecs(NodeSetType &NodeSets);
  void removeDuplicateNodes(NodeSetType &NodeSets);
  void computeNodeFunctions(NodeSetType &NodeSets);
  void registerPressureFilter(NodeSetType &NodeSets);
  void colocateNodeSets(NodeSetType &NodeSets);
  void checkNodeSets(NodeSetType &NodeSets);
  void groupRemainingNodes(NodeSetType &NodeSets);
  void addConnectedNodes(SUnit *SU, NodeSet &NewSet,
                         SetVector<SUnit *> &NodesAdded);
  void computeNodeOrder(NodeSetType &NodeSets);
  void checkValidNodeOrder(const NodeSetType &Circuits) const;
  bool schedulePipeline(SMSchedule &Schedule);
  bool computeDelta(MachineInstr &MI, unsigned &Delta);
  MachineInstr *findDefInLoop(Register Reg);
  bool canUseLastOffsetValue(MachineInstr *MI, unsigned &BasePos,
                             unsigned &OffsetPos, unsigned &NewBase,
                             int64_t &NewOffset);
  void postProcessDAG();
  /// Set the Minimum Initiation Interval for this schedule attempt.
  void setMII(unsigned ResMII, unsigned RecMII);
  /// Set the Maximum Initiation Interval for this schedule attempt.
  void setMAX_II();
};

/// A NodeSet contains a set of SUnit DAG nodes with additional information
/// that assigns a priority to the set.
class NodeSet {
  SetVector<SUnit *> Nodes;
  bool HasRecurrence = false;
  unsigned RecMII = 0;
  int MaxMOV = 0;
  unsigned MaxDepth = 0;
  unsigned Colocate = 0;
  SUnit *ExceedPressure = nullptr;
  unsigned Latency = 0;

public:
  using iterator = SetVector<SUnit *>::const_iterator;

  NodeSet() = default;
  NodeSet(iterator S, iterator E) : Nodes(S, E), HasRecurrence(true) {
    Latency = 0;
    for (const SUnit *Node : Nodes) {
      DenseMap<SUnit *, unsigned> SuccSUnitLatency;
      for (const SDep &Succ : Node->Succs) {
        auto SuccSUnit = Succ.getSUnit();
        if (!Nodes.count(SuccSUnit))
          continue;
        unsigned CurLatency = Succ.getLatency();
        unsigned MaxLatency = 0;
        if (SuccSUnitLatency.count(SuccSUnit))
          MaxLatency = SuccSUnitLatency[SuccSUnit];
        if (CurLatency > MaxLatency)
          SuccSUnitLatency[SuccSUnit] = CurLatency;
      }
      for (auto SUnitLatency : SuccSUnitLatency)
        Latency += SUnitLatency.second;
    }
  }

  bool insert(SUnit *SU) { return Nodes.insert(SU); }

  void insert(iterator S, iterator E) { Nodes.insert(S, E); }

  template <typename UnaryPredicate> bool remove_if(UnaryPredicate P) {
    return Nodes.remove_if(P);
  }

  unsigned count(SUnit *SU) const { return Nodes.count(SU); }

  bool hasRecurrence() { return HasRecurrence; };

  unsigned size() const { return Nodes.size(); }

  bool empty() const { return Nodes.empty(); }

  SUnit *getNode(unsigned i) const { return Nodes[i]; };

  void setRecMII(unsigned mii) { RecMII = mii; };

  void setColocate(unsigned c) { Colocate = c; };

  void setExceedPressure(SUnit *SU) { ExceedPressure = SU; }

  bool isExceedSU(SUnit *SU) { return ExceedPressure == SU; }

  int compareRecMII(NodeSet &RHS) { return RecMII - RHS.RecMII; }

  int getRecMII() { return RecMII; }

  /// Summarize node functions for the entire node set.
  void computeNodeSetInfo(SwingSchedulerDAG *SSD) {
    for (SUnit *SU : *this) {
      MaxMOV = std::max(MaxMOV, SSD->getMOV(SU));
      MaxDepth = std::max(MaxDepth, SSD->getDepth(SU));
    }
  }

  unsigned getLatency() { return Latency; }

  unsigned getMaxDepth() { return MaxDepth; }

  void clear() {
    Nodes.clear();
    RecMII = 0;
    HasRecurrence = false;
    MaxMOV = 0;
    MaxDepth = 0;
    Colocate = 0;
    ExceedPressure = nullptr;
  }

  operator SetVector<SUnit *> &() { return Nodes; }

  /// Sort the node sets by importance. First, rank them by recurrence MII,
  /// then by mobility (least mobile done first), and finally by depth.
  /// Each node set may contain a colocate value which is used as the first
  /// tie breaker, if it's set.
  bool operator>(const NodeSet &RHS) const {
    if (RecMII == RHS.RecMII) {
      if (Colocate != 0 && RHS.Colocate != 0 && Colocate != RHS.Colocate)
        return Colocate < RHS.Colocate;
      if (MaxMOV == RHS.MaxMOV)
        return MaxDepth > RHS.MaxDepth;
      return MaxMOV < RHS.MaxMOV;
    }
    return RecMII > RHS.RecMII;
  }

  bool operator==(const NodeSet &RHS) const {
    return RecMII == RHS.RecMII && MaxMOV == RHS.MaxMOV &&
           MaxDepth == RHS.MaxDepth;
  }

  bool operator!=(const NodeSet &RHS) const { return !operator==(RHS); }

  iterator begin() { return Nodes.begin(); }
  iterator end() { return Nodes.end(); }
  void print(raw_ostream &os) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dump() const;
#endif
};

// 16 was selected based on the number of ProcResource kinds for all
// existing Subtargets, so that SmallVector don't need to resize too often.
static const int DefaultProcResSize = 16;

class ResourceManager {
private:
  const MCSubtargetInfo *STI;
  const MCSchedModel &SM;
  const TargetSubtargetInfo *ST;
  const TargetInstrInfo *TII;
  ScheduleDAGInstrs *DAG;
  const bool UseDFA;
  /// DFA resources for each slot
  llvm::SmallVector<std::unique_ptr<DFAPacketizer>> DFAResources;
  /// Modulo Reservation Table. When a resource with ID R is consumed in cycle
  /// C, it is counted in MRT[C mod II][R]. (Used when UseDFA == F)
  llvm::SmallVector<llvm::SmallVector<uint64_t, DefaultProcResSize>> MRT;
  /// The number of scheduled micro operations for each slot. Micro operations
  /// are assumed to be scheduled one per cycle, starting with the cycle in
  /// which the instruction is scheduled.
  llvm::SmallVector<int> NumScheduledMops;
  /// Each processor resource is associated with a so-called processor resource
  /// mask. This vector allows to correlate processor resource IDs with
  /// processor resource masks. There is exactly one element per each processor
  /// resource declared by the scheduling model.
  llvm::SmallVector<uint64_t, DefaultProcResSize> ProcResourceMasks;
  int InitiationInterval = 0;
  /// The number of micro operations that can be scheduled at a cycle.
  int IssueWidth;

  int calculateResMIIDFA() const;
  /// Check if MRT is overbooked
  bool isOverbooked() const;
  /// Reserve resources on MRT
  void reserveResources(const MCSchedClassDesc *SCDesc, int Cycle);
  /// Unreserve resources on MRT
  void unreserveResources(const MCSchedClassDesc *SCDesc, int Cycle);

  /// Return M satisfying Dividend = Divisor * X + M, 0 < M < Divisor.
  /// The slot on MRT to reserve a resource for the cycle C is positiveModulo(C,
  /// II).
  int positiveModulo(int Dividend, int Divisor) const {
    assert(Divisor > 0);
    int R = Dividend % Divisor;
    if (R < 0)
      R += Divisor;
    return R;
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVM_DUMP_METHOD void dumpMRT() const;
#endif

public:
  ResourceManager(const TargetSubtargetInfo *ST, ScheduleDAGInstrs *DAG)
      : STI(ST), SM(ST->getSchedModel()), ST(ST), TII(ST->getInstrInfo()),
        DAG(DAG), UseDFA(ST->useDFAforSMS()),
        ProcResourceMasks(SM.getNumProcResourceKinds(), 0),
        IssueWidth(SM.IssueWidth) {
    initProcResourceVectors(SM, ProcResourceMasks);
    if (IssueWidth <= 0)
      // If IssueWidth is not specified, set a sufficiently large value
      IssueWidth = 100;
    if (SwpForceIssueWidth > 0)
      IssueWidth = SwpForceIssueWidth;
  }

  void initProcResourceVectors(const MCSchedModel &SM,
                               SmallVectorImpl<uint64_t> &Masks);

  /// Check if the resources occupied by a machine instruction are available
  /// in the current state.
  bool canReserveResources(SUnit &SU, int Cycle);

  /// Reserve the resources occupied by a machine instruction and change the
  /// current state to reflect that change.
  void reserveResources(SUnit &SU, int Cycle);

  int calculateResMII() const;

  /// Initialize resources with the initiation interval II.
  void init(int II);
};

/// This class represents the scheduled code.  The main data structure is a
/// map from scheduled cycle to instructions.  During scheduling, the
/// data structure explicitly represents all stages/iterations.   When
/// the algorithm finshes, the schedule is collapsed into a single stage,
/// which represents instructions from different loop iterations.
///
/// The SMS algorithm allows negative values for cycles, so the first cycle
/// in the schedule is the smallest cycle value.
class SMSchedule {
private:
  /// Map from execution cycle to instructions.
  DenseMap<int, std::deque<SUnit *>> ScheduledInstrs;

  /// Map from instruction to execution cycle.
  std::map<SUnit *, int> InstrToCycle;

  /// Keep track of the first cycle value in the schedule.  It starts
  /// as zero, but the algorithm allows negative values.
  int FirstCycle = 0;

  /// Keep track of the last cycle value in the schedule.
  int LastCycle = 0;

  /// The initiation interval (II) for the schedule.
  int InitiationInterval = 0;

  /// Target machine information.
  const TargetSubtargetInfo &ST;

  /// Virtual register information.
  MachineRegisterInfo &MRI;

  ResourceManager ProcItinResources;

public:
  SMSchedule(MachineFunction *mf, SwingSchedulerDAG *DAG)
      : ST(mf->getSubtarget()), MRI(mf->getRegInfo()),
        ProcItinResources(&ST, DAG) {}

  void reset() {
    ScheduledInstrs.clear();
    InstrToCycle.clear();
    FirstCycle = 0;
    LastCycle = 0;
    InitiationInterval = 0;
  }

  /// Set the initiation interval for this schedule.
  void setInitiationInterval(int ii) {
    InitiationInterval = ii;
    ProcItinResources.init(ii);
  }

  /// Return the initiation interval for this schedule.
  int getInitiationInterval() const { return InitiationInterval; }

  /// Return the first cycle in the completed schedule.  This
  /// can be a negative value.
  int getFirstCycle() const { return FirstCycle; }

  /// Return the last cycle in the finalized schedule.
  int getFinalCycle() const { return FirstCycle + InitiationInterval - 1; }

  /// Return the cycle of the earliest scheduled instruction in the dependence
  /// chain.
  int earliestCycleInChain(const SDep &Dep);

  /// Return the cycle of the latest scheduled instruction in the dependence
  /// chain.
  int latestCycleInChain(const SDep &Dep);

  void computeStart(SUnit *SU, int *MaxEarlyStart, int *MinLateStart, int II,
                    SwingSchedulerDAG *DAG);
  bool insert(SUnit *SU, int StartCycle, int EndCycle, int II);

  /// Iterators for the cycle to instruction map.
  using sched_iterator = DenseMap<int, std::deque<SUnit *>>::iterator;
  using const_sched_iterator =
      DenseMap<int, std::deque<SUnit *>>::const_iterator;

  /// Return true if the instruction is scheduled at the specified stage.
  bool isScheduledAtStage(SUnit *SU, unsigned StageNum) {
    return (stageScheduled(SU) == (int)StageNum);
  }

  /// Return the stage for a scheduled instruction.  Return -1 if
  /// the instruction has not been scheduled.
  int stageScheduled(SUnit *SU) const {
    std::map<SUnit *, int>::const_iterator it = InstrToCycle.find(SU);
    if (it == InstrToCycle.end())
      return -1;
    return (it->second - FirstCycle) / InitiationInterval;
  }

  /// Return the cycle for a scheduled instruction. This function normalizes
  /// the first cycle to be 0.
  unsigned cycleScheduled(SUnit *SU) const {
    std::map<SUnit *, int>::const_iterator it = InstrToCycle.find(SU);
    assert(it != InstrToCycle.end() && "Instruction hasn't been scheduled.");
    return (it->second - FirstCycle) % InitiationInterval;
  }

  /// Return the maximum stage count needed for this schedule.
  unsigned getMaxStageCount() {
    return (LastCycle - FirstCycle) / InitiationInterval;
  }

  /// Return the instructions that are scheduled at the specified cycle.
  std::deque<SUnit *> &getInstructions(int cycle) {
    return ScheduledInstrs[cycle];
  }

  SmallSet<SUnit *, 8>
  computeUnpipelineableNodes(SwingSchedulerDAG *SSD,
                             TargetInstrInfo::PipelinerLoopInfo *PLI);

  std::deque<SUnit *>
  reorderInstructions(const SwingSchedulerDAG *SSD,
                      const std::deque<SUnit *> &Instrs) const;

  bool
  normalizeNonPipelinedInstructions(SwingSchedulerDAG *SSD,
                                    TargetInstrInfo::PipelinerLoopInfo *PLI);
  bool isValidSchedule(SwingSchedulerDAG *SSD);
  void finalizeSchedule(SwingSchedulerDAG *SSD);
  void orderDependence(const SwingSchedulerDAG *SSD, SUnit *SU,
                       std::deque<SUnit *> &Insts) const;
  bool isLoopCarried(const SwingSchedulerDAG *SSD, MachineInstr &Phi) const;
  bool isLoopCarriedDefOfUse(const SwingSchedulerDAG *SSD, MachineInstr *Def,
                             MachineOperand &MO) const;

  bool onlyHasLoopCarriedOutputOrOrderPreds(SUnit *SU,
                                            SwingSchedulerDAG *DAG) const;
  void print(raw_ostream &os) const;
  void dump() const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEPIPELINER_H
