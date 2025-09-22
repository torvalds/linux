//===--- AMDGPUIGroupLP.cpp - AMDGPU IGroupLP  ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file This file defines a set of schedule DAG mutations that can be used to
// override default scheduler behavior to enforce specific scheduling patterns.
// They should be used in cases where runtime performance considerations such as
// inter-wavefront interactions, mean that compile-time heuristics cannot
// predict the optimal instruction ordering, or in kernels where optimum
// instruction scheduling is important enough to warrant manual intervention.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUIGroupLP.h"
#include "AMDGPUTargetMachine.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIInstrInfo.h"
#include "SIMachineFunctionInfo.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/TargetOpcodes.h"

using namespace llvm;

#define DEBUG_TYPE "igrouplp"

namespace {

static cl::opt<bool> EnableExactSolver(
    "amdgpu-igrouplp-exact-solver", cl::Hidden,
    cl::desc("Whether to use the exponential time solver to fit "
             "the instructions to the pipeline as closely as "
             "possible."),
    cl::init(false));

static cl::opt<unsigned> CutoffForExact(
    "amdgpu-igrouplp-exact-solver-cutoff", cl::init(0), cl::Hidden,
    cl::desc("The maximum number of scheduling group conflicts "
             "which we attempt to solve with the exponential time "
             "exact solver. Problem sizes greater than this will"
             "be solved by the less accurate greedy algorithm. Selecting "
             "solver by size is superseded by manually selecting "
             "the solver (e.g. by amdgpu-igrouplp-exact-solver"));

static cl::opt<uint64_t> MaxBranchesExplored(
    "amdgpu-igrouplp-exact-solver-max-branches", cl::init(0), cl::Hidden,
    cl::desc("The amount of branches that we are willing to explore with"
             "the exact algorithm before giving up."));

static cl::opt<bool> UseCostHeur(
    "amdgpu-igrouplp-exact-solver-cost-heur", cl::init(true), cl::Hidden,
    cl::desc("Whether to use the cost heuristic to make choices as we "
             "traverse the search space using the exact solver. Defaulted "
             "to on, and if turned off, we will use the node order -- "
             "attempting to put the later nodes in the later sched groups. "
             "Experimentally, results are mixed, so this should be set on a "
             "case-by-case basis."));

// Components of the mask that determines which instruction types may be may be
// classified into a SchedGroup.
enum class SchedGroupMask {
  NONE = 0u,
  ALU = 1u << 0,
  VALU = 1u << 1,
  SALU = 1u << 2,
  MFMA = 1u << 3,
  VMEM = 1u << 4,
  VMEM_READ = 1u << 5,
  VMEM_WRITE = 1u << 6,
  DS = 1u << 7,
  DS_READ = 1u << 8,
  DS_WRITE = 1u << 9,
  TRANS = 1u << 10,
  ALL = ALU | VALU | SALU | MFMA | VMEM | VMEM_READ | VMEM_WRITE | DS |
        DS_READ | DS_WRITE | TRANS,
  LLVM_MARK_AS_BITMASK_ENUM(/* LargestFlag = */ ALL)
};

class SchedGroup;

// InstructionRule class is used to enact a filter which determines whether or
// not an SU maps to a given SchedGroup. It contains complementary data
// structures (e.g Cache) to help those filters.
class InstructionRule {
protected:
  const SIInstrInfo *TII;
  unsigned SGID;
  // A cache made available to the Filter to store SUnits for subsequent
  // invocations of the Filter
  std::optional<SmallVector<SUnit *, 4>> Cache;

public:
  virtual bool
  apply(const SUnit *, const ArrayRef<SUnit *>,
        SmallVectorImpl<SchedGroup> &) {
    return true;
  };

  InstructionRule(const SIInstrInfo *TII, unsigned SGID,
                  bool NeedsCache = false)
      : TII(TII), SGID(SGID) {
    if (NeedsCache) {
      Cache = SmallVector<SUnit *, 4>();
    }
  }

  virtual ~InstructionRule() = default;
};

using SUnitsToCandidateSGsMap = DenseMap<SUnit *, SmallVector<int, 4>>;

// Classify instructions into groups to enable fine tuned control over the
// scheduler. These groups may be more specific than current SchedModel
// instruction classes.
class SchedGroup {
private:
  // Mask that defines which instruction types can be classified into this
  // SchedGroup. The instruction types correspond to the mask from SCHED_BARRIER
  // and SCHED_GROUP_BARRIER.
  SchedGroupMask SGMask;

  // Maximum number of SUnits that can be added to this group.
  std::optional<unsigned> MaxSize;

  // SchedGroups will only synchronize with other SchedGroups that have the same
  // SyncID.
  int SyncID = 0;

  // SGID is used to map instructions to candidate SchedGroups
  unsigned SGID;

  // The different rules each instruction in this SchedGroup must conform to
  SmallVector<std::shared_ptr<InstructionRule>, 4> Rules;

  // Count of the number of created SchedGroups, used to initialize SGID.
  static unsigned NumSchedGroups;

  // Try to add and edge from SU A to SU B.
  bool tryAddEdge(SUnit *A, SUnit *B);

  // Use SGMask to determine whether we can classify MI as a member of this
  // SchedGroup object.
  bool canAddMI(const MachineInstr &MI) const;

public:
  // Collection of SUnits that are classified as members of this group.
  SmallVector<SUnit *, 32> Collection;

  ScheduleDAGInstrs *DAG;
  const SIInstrInfo *TII;

  // Returns true if SU can be added to this SchedGroup.
  bool canAddSU(SUnit &SU) const;

  // Add DAG dependencies from all SUnits in this SchedGroup and this SU. If
  // MakePred is true, SU will be a predecessor of the SUnits in this
  // SchedGroup, otherwise SU will be a successor.
  void link(SUnit &SU, bool MakePred = false);

  // Add DAG dependencies and track which edges are added, and the count of
  // missed edges
  int link(SUnit &SU, bool MakePred,
           std::vector<std::pair<SUnit *, SUnit *>> &AddedEdges);

  // Add DAG dependencies from all SUnits in this SchedGroup and this SU.
  // Use the predicate to determine whether SU should be a predecessor (P =
  // true) or a successor (P = false) of this SchedGroup.
  void link(SUnit &SU, function_ref<bool(const SUnit *A, const SUnit *B)> P);

  // Add DAG dependencies such that SUnits in this group shall be ordered
  // before SUnits in OtherGroup.
  void link(SchedGroup &OtherGroup);

  // Returns true if no more instructions may be added to this group.
  bool isFull() const { return MaxSize && Collection.size() >= *MaxSize; }

  // Append a constraint that SUs must meet in order to fit into this
  // SchedGroup. Since many rules involve the relationship between a SchedGroup
  // and the SUnits in other SchedGroups, rules are checked at Pipeline Solve
  // time (rather than SchedGroup init time.)
  void addRule(std::shared_ptr<InstructionRule> NewRule) {
    Rules.push_back(NewRule);
  }

  // Returns true if the SU matches all rules
  bool allowedByRules(const SUnit *SU,
                      SmallVectorImpl<SchedGroup> &SyncPipe) const {
    for (auto &Rule : Rules) {
      if (!Rule.get()->apply(SU, Collection, SyncPipe))
        return false;
    }
    return true;
  }

  // Add SU to the SchedGroup.
  void add(SUnit &SU) {
    LLVM_DEBUG(dbgs() << "For SchedGroup with mask "
                      << format_hex((int)SGMask, 10, true) << " adding "
                      << *SU.getInstr());
    Collection.push_back(&SU);
  }

  // Remove last element in the SchedGroup
  void pop() { Collection.pop_back(); }

  // Identify and add all relevant SUs from the DAG to this SchedGroup.
  void initSchedGroup();

  // Add instructions to the SchedGroup bottom up starting from RIter.
  // PipelineInstrs is a set of instructions that should not be added to the
  // SchedGroup even when the other conditions for adding it are satisfied.
  // RIter will be added to the SchedGroup as well, and dependencies will be
  // added so that RIter will always be scheduled at the end of the group.
  void initSchedGroup(std::vector<SUnit>::reverse_iterator RIter,
                      SUnitsToCandidateSGsMap &SyncedInstrs);

  void initSchedGroup(SUnitsToCandidateSGsMap &SyncedInstrs);

  int getSyncID() { return SyncID; }

  int getSGID() { return SGID; }

  SchedGroupMask getMask() { return SGMask; }

  SchedGroup(SchedGroupMask SGMask, std::optional<unsigned> MaxSize,
             ScheduleDAGInstrs *DAG, const SIInstrInfo *TII)
      : SGMask(SGMask), MaxSize(MaxSize), DAG(DAG), TII(TII) {
    SGID = NumSchedGroups++;
  }

  SchedGroup(SchedGroupMask SGMask, std::optional<unsigned> MaxSize, int SyncID,
             ScheduleDAGInstrs *DAG, const SIInstrInfo *TII)
      : SGMask(SGMask), MaxSize(MaxSize), SyncID(SyncID), DAG(DAG), TII(TII) {
    SGID = NumSchedGroups++;
  }
};

// Remove all existing edges from a SCHED_BARRIER or SCHED_GROUP_BARRIER.
static void resetEdges(SUnit &SU, ScheduleDAGInstrs *DAG) {
  assert(SU.getInstr()->getOpcode() == AMDGPU::SCHED_BARRIER ||
         SU.getInstr()->getOpcode() == AMDGPU::SCHED_GROUP_BARRIER ||
         SU.getInstr()->getOpcode() == AMDGPU::IGLP_OPT);

  while (!SU.Preds.empty())
    for (auto &P : SU.Preds)
      SU.removePred(P);

  while (!SU.Succs.empty())
    for (auto &S : SU.Succs)
      for (auto &SP : S.getSUnit()->Preds)
        if (SP.getSUnit() == &SU)
          S.getSUnit()->removePred(SP);
}

using SUToCandSGsPair = std::pair<SUnit *, SmallVector<int, 4>>;
using SUsToCandSGsVec = SmallVector<SUToCandSGsPair, 4>;

// The PipelineSolver is used to assign SUnits to SchedGroups in a pipeline
// in non-trivial cases. For example, if the requested pipeline is
// {VMEM_READ, VALU, MFMA, VMEM_READ} and we encounter a VMEM_READ instruction
// in the DAG, then we will have an instruction that can not be trivially
// assigned to a SchedGroup. The PipelineSolver class implements two algorithms
// to find a good solution to the pipeline -- a greedy algorithm and an exact
// algorithm. The exact algorithm has an exponential time complexity and should
// only be used for small sized problems or medium sized problems where an exact
// solution is highly desired.
class PipelineSolver {
  ScheduleDAGMI *DAG;

  // Instructions that can be assigned to multiple SchedGroups
  DenseMap<int, SUnitsToCandidateSGsMap> SyncedInstrs;
  SmallVector<SUsToCandSGsVec, 4> PipelineInstrs;
  DenseMap<int, SmallVector<SchedGroup, 4>> SyncedSchedGroups;
  // The current working pipeline
  SmallVector<SmallVector<SchedGroup, 4>, 4> CurrPipeline;
  // The pipeline that has the best solution found so far
  SmallVector<SmallVector<SchedGroup, 4>, 4> BestPipeline;

  // Whether or not we actually have any SyncedInstrs to try to solve.
  bool NeedsSolver = false;

  // Compute an estimate of the size of search tree -- the true size is
  // the product of each conflictedInst.Matches.size() across all SyncPipelines
  unsigned computeProblemSize();

  // The cost penalty of not assigning a SU to a SchedGroup
  int MissPenalty = 0;

  // Costs in terms of the number of edges we are unable to add
  int BestCost = -1;
  int CurrCost = 0;

  // Index pointing to the conflicting instruction that is currently being
  // fitted
  int CurrConflInstNo = 0;
  // Index to the pipeline that is currently being fitted
  int CurrSyncGroupIdx = 0;
  // The first non trivial pipeline
  int BeginSyncGroupIdx = 0;

  // How many branches we have explored
  uint64_t BranchesExplored = 0;

  // The direction in which we process the candidate SchedGroups per SU
  bool IsBottomUp = true;

  // Update indices to fit next conflicting instruction
  void advancePosition();
  // Recede indices to attempt to find better fit for previous conflicting
  // instruction
  void retreatPosition();

  // The exponential time algorithm which finds the provably best fit
  bool solveExact();
  // The polynomial time algorithm which attempts to find a good fit
  bool solveGreedy();
  // Find the best SchedGroup for the current SU using the heuristic given all
  // current information. One step in the greedy algorithm. Templated against
  // the SchedGroup iterator (either reverse or forward).
  template <typename T>
  void greedyFind(std::vector<std::pair<SUnit *, SUnit *>> &AddedEdges, T I,
                  T E);
  // Whether or not the current solution is optimal
  bool checkOptimal();
  // Populate the ready list, prioiritizing fewest missed edges first
  // Templated against the SchedGroup iterator (either reverse or forward).
  template <typename T>
  void populateReadyList(SmallVectorImpl<std::pair<int, int>> &ReadyList, T I,
                         T E);
  // Add edges corresponding to the SchedGroups as assigned by solver
  void makePipeline();
  // Link the SchedGroups in the best found pipeline.
  // Tmplated against the SchedGroup iterator (either reverse or forward).
  template <typename T> void linkSchedGroups(T I, T E);
  // Add the edges from the SU to the other SchedGroups in pipeline, and
  // return the number of edges missed.
  int addEdges(SmallVectorImpl<SchedGroup> &SyncPipeline, SUnit *SU, int SGID,
               std::vector<std::pair<SUnit *, SUnit *>> &AddedEdges);
  /// Link the pipeline as if \p SU was in the SchedGroup with ID \p SGID. It
  /// returns the cost (in terms of missed pipeline edges), and tracks the edges
  /// added in \p AddedEdges
  template <typename T>
  int linkSUnit(SUnit *SU, int SGID,
                std::vector<std::pair<SUnit *, SUnit *>> &AddedEdges, T I, T E);
  /// Remove the edges passed via \p AddedEdges
  void removeEdges(const std::vector<std::pair<SUnit *, SUnit *>> &AddedEdges);
  // Convert the passed in maps to arrays for bidirectional iterators
  void convertSyncMapsToArrays();

  void reset();

public:
  // Invoke the solver to map instructions to instruction groups. Heuristic &&
  // command-line-option determines to use exact or greedy algorithm.
  void solve();

  PipelineSolver(DenseMap<int, SmallVector<SchedGroup, 4>> &SyncedSchedGroups,
                 DenseMap<int, SUnitsToCandidateSGsMap> &SyncedInstrs,
                 ScheduleDAGMI *DAG, bool IsBottomUp = true)
      : DAG(DAG), SyncedInstrs(SyncedInstrs),
        SyncedSchedGroups(SyncedSchedGroups), IsBottomUp(IsBottomUp) {

    for (auto &PipelineInstrs : SyncedInstrs) {
      if (PipelineInstrs.second.size() > 0) {
        NeedsSolver = true;
        break;
      }
    }

    if (!NeedsSolver)
      return;

    convertSyncMapsToArrays();

    CurrPipeline = BestPipeline;

    while (static_cast<size_t>(BeginSyncGroupIdx) < PipelineInstrs.size() &&
           PipelineInstrs[BeginSyncGroupIdx].size() == 0)
      ++BeginSyncGroupIdx;

    if (static_cast<size_t>(BeginSyncGroupIdx) >= PipelineInstrs.size())
      return;
  }
};

void PipelineSolver::reset() {

  for (auto &SyncPipeline : CurrPipeline) {
    for (auto &SG : SyncPipeline) {
      SmallVector<SUnit *, 32> TempCollection = SG.Collection;
      SG.Collection.clear();
      auto SchedBarr = llvm::find_if(TempCollection, [](SUnit *SU) {
        return SU->getInstr()->getOpcode() == AMDGPU::SCHED_GROUP_BARRIER;
      });
      if (SchedBarr != TempCollection.end())
        SG.Collection.push_back(*SchedBarr);
    }
  }

  CurrSyncGroupIdx = BeginSyncGroupIdx;
  CurrConflInstNo = 0;
  CurrCost = 0;
}

void PipelineSolver::convertSyncMapsToArrays() {
  for (auto &SyncPipe : SyncedSchedGroups) {
    BestPipeline.insert(BestPipeline.begin(), SyncPipe.second);
  }

  int PipelineIDx = SyncedInstrs.size() - 1;
  PipelineInstrs.resize(SyncedInstrs.size());
  for (auto &SyncInstrMap : SyncedInstrs) {
    for (auto &SUsToCandSGs : SyncInstrMap.second) {
      if (PipelineInstrs[PipelineIDx].size() == 0) {
        PipelineInstrs[PipelineIDx].push_back(
            std::pair(SUsToCandSGs.first, SUsToCandSGs.second));
        continue;
      }
      auto SortPosition = PipelineInstrs[PipelineIDx].begin();
      // Insert them in sorted order -- this allows for good parsing order in
      // the greedy algorithm
      while (SortPosition != PipelineInstrs[PipelineIDx].end() &&
             SUsToCandSGs.first->NodeNum > SortPosition->first->NodeNum)
        ++SortPosition;
      PipelineInstrs[PipelineIDx].insert(
          SortPosition, std::pair(SUsToCandSGs.first, SUsToCandSGs.second));
    }
    --PipelineIDx;
  }
}

template <typename T> void PipelineSolver::linkSchedGroups(T I, T E) {
  for (; I != E; ++I) {
    auto &GroupA = *I;
    for (auto J = std::next(I); J != E; ++J) {
      auto &GroupB = *J;
      GroupA.link(GroupB);
    }
  }
}

void PipelineSolver::makePipeline() {
  // Preserve the order of barrier for subsequent SchedGroupBarrier mutations
  for (auto &SyncPipeline : BestPipeline) {
    LLVM_DEBUG(dbgs() << "Printing SchedGroups\n");
    for (auto &SG : SyncPipeline) {
      LLVM_DEBUG(dbgs() << "SchedGroup with SGID " << SG.getSGID()
                        << " has: \n");
      SUnit *SGBarr = nullptr;
      for (auto &SU : SG.Collection) {
        if (SU->getInstr()->getOpcode() == AMDGPU::SCHED_GROUP_BARRIER)
          SGBarr = SU;
        LLVM_DEBUG(dbgs() << "SU(" << SU->NodeNum << ")\n");
      }
      // Command line requested IGroupLP doesn't have SGBarr
      if (!SGBarr)
        continue;
      resetEdges(*SGBarr, DAG);
      SG.link(*SGBarr, false);
    }
  }

  for (auto &SyncPipeline : BestPipeline) {
    IsBottomUp ? linkSchedGroups(SyncPipeline.rbegin(), SyncPipeline.rend())
               : linkSchedGroups(SyncPipeline.begin(), SyncPipeline.end());
  }
}

template <typename T>
int PipelineSolver::linkSUnit(
    SUnit *SU, int SGID, std::vector<std::pair<SUnit *, SUnit *>> &AddedEdges,
    T I, T E) {
  bool MakePred = false;
  int AddedCost = 0;
  for (; I < E; ++I) {
    if (I->getSGID() == SGID) {
      MakePred = true;
      continue;
    }
    auto Group = *I;
    AddedCost += Group.link(*SU, MakePred, AddedEdges);
    assert(AddedCost >= 0);
  }
  return AddedCost;
}

int PipelineSolver::addEdges(
    SmallVectorImpl<SchedGroup> &SyncPipeline, SUnit *SU, int SGID,
    std::vector<std::pair<SUnit *, SUnit *>> &AddedEdges) {

  // For IsBottomUp, the first SchedGroup in SyncPipeline contains the
  // instructions that are the ultimate successors in the resultant mutation.
  // Therefore, in such a configuration, the SchedGroups occurring before the
  // candidate SGID are successors of the candidate SchedGroup, thus the current
  // SU should be linked as a predecessor to SUs in those SchedGroups. The
  // opposite is true if !IsBottomUp. IsBottomUp occurs in the case of multiple
  // SCHED_GROUP_BARRIERS, or if a user specifies IGLP_OPT SchedGroups using
  // IsBottomUp (in reverse).
  return IsBottomUp ? linkSUnit(SU, SGID, AddedEdges, SyncPipeline.rbegin(),
                                SyncPipeline.rend())
                    : linkSUnit(SU, SGID, AddedEdges, SyncPipeline.begin(),
                                SyncPipeline.end());
}

void PipelineSolver::removeEdges(
    const std::vector<std::pair<SUnit *, SUnit *>> &EdgesToRemove) {
  // Only remove the edges that we have added when testing
  // the fit.
  for (auto &PredSuccPair : EdgesToRemove) {
    SUnit *Pred = PredSuccPair.first;
    SUnit *Succ = PredSuccPair.second;

    auto Match = llvm::find_if(
        Succ->Preds, [&Pred](SDep &P) { return P.getSUnit() == Pred; });
    if (Match != Succ->Preds.end()) {
      assert(Match->isArtificial());
      Succ->removePred(*Match);
    }
  }
}

void PipelineSolver::advancePosition() {
  ++CurrConflInstNo;

  if (static_cast<size_t>(CurrConflInstNo) >=
      PipelineInstrs[CurrSyncGroupIdx].size()) {
    CurrConflInstNo = 0;
    ++CurrSyncGroupIdx;
    // Advance to next non-trivial pipeline
    while (static_cast<size_t>(CurrSyncGroupIdx) < PipelineInstrs.size() &&
           PipelineInstrs[CurrSyncGroupIdx].size() == 0)
      ++CurrSyncGroupIdx;
  }
}

void PipelineSolver::retreatPosition() {
  assert(CurrConflInstNo >= 0);
  assert(CurrSyncGroupIdx >= 0);

  if (CurrConflInstNo > 0) {
    --CurrConflInstNo;
    return;
  }

  if (CurrConflInstNo == 0) {
    // If we return to the starting position, we have explored
    // the entire tree
    if (CurrSyncGroupIdx == BeginSyncGroupIdx)
      return;

    --CurrSyncGroupIdx;
    // Go to previous non-trivial pipeline
    while (PipelineInstrs[CurrSyncGroupIdx].size() == 0)
      --CurrSyncGroupIdx;

    CurrConflInstNo = PipelineInstrs[CurrSyncGroupIdx].size() - 1;
  }
}

bool PipelineSolver::checkOptimal() {
  if (static_cast<size_t>(CurrSyncGroupIdx) == PipelineInstrs.size()) {
    if (BestCost == -1 || CurrCost < BestCost) {
      BestPipeline = CurrPipeline;
      BestCost = CurrCost;
      LLVM_DEBUG(dbgs() << "Found Fit with cost " << BestCost << "\n");
    }
    assert(BestCost >= 0);
  }

  bool DoneExploring = false;
  if (MaxBranchesExplored > 0 && BranchesExplored >= MaxBranchesExplored)
    DoneExploring = true;

  return (DoneExploring || BestCost == 0);
}

template <typename T>
void PipelineSolver::populateReadyList(
    SmallVectorImpl<std::pair<int, int>> &ReadyList, T I, T E) {
  SUToCandSGsPair CurrSU = PipelineInstrs[CurrSyncGroupIdx][CurrConflInstNo];
  auto SyncPipeline = CurrPipeline[CurrSyncGroupIdx];
  assert(CurrSU.second.size() >= 1);

  for (; I != E; ++I) {
    std::vector<std::pair<SUnit *, SUnit *>> AddedEdges;
    int CandSGID = *I;
    SchedGroup *Match = llvm::find_if(SyncPipeline, [CandSGID](SchedGroup &SG) {
      return SG.getSGID() == CandSGID;
    });
    assert(Match);

    if (UseCostHeur) {
      if (Match->isFull()) {
        ReadyList.push_back(std::pair(*I, MissPenalty));
        continue;
      }

      int TempCost = addEdges(SyncPipeline, CurrSU.first, CandSGID, AddedEdges);
      ReadyList.push_back(std::pair(*I, TempCost));
      removeEdges(AddedEdges);
    } else
      ReadyList.push_back(std::pair(*I, -1));
  }

  if (UseCostHeur) {
    std::sort(ReadyList.begin(), ReadyList.end(),
              [](std::pair<int, int> A, std::pair<int, int> B) {
                return A.second < B.second;
              });
  }

  assert(ReadyList.size() == CurrSU.second.size());
}

bool PipelineSolver::solveExact() {
  if (checkOptimal())
    return true;

  if (static_cast<size_t>(CurrSyncGroupIdx) == PipelineInstrs.size())
    return false;

  assert(static_cast<size_t>(CurrSyncGroupIdx) < PipelineInstrs.size());
  assert(static_cast<size_t>(CurrConflInstNo) <
         PipelineInstrs[CurrSyncGroupIdx].size());
  SUToCandSGsPair CurrSU = PipelineInstrs[CurrSyncGroupIdx][CurrConflInstNo];
  LLVM_DEBUG(dbgs() << "Fitting SU(" << CurrSU.first->NodeNum
                    << ") in Pipeline # " << CurrSyncGroupIdx << "\n");

  // SchedGroup -> Cost pairs
  SmallVector<std::pair<int, int>, 4> ReadyList;
  // Prioritize the candidate sched groups in terms of lowest cost first
  IsBottomUp ? populateReadyList(ReadyList, CurrSU.second.rbegin(),
                                 CurrSU.second.rend())
             : populateReadyList(ReadyList, CurrSU.second.begin(),
                                 CurrSU.second.end());

  auto I = ReadyList.begin();
  auto E = ReadyList.end();
  for (; I != E; ++I) {
    // If we are trying SGs in least cost order, and the current SG is cost
    // infeasible, then all subsequent SGs will also be cost infeasible, so we
    // can prune.
    if (BestCost != -1 && (CurrCost + I->second > BestCost))
      return false;

    int CandSGID = I->first;
    int AddedCost = 0;
    std::vector<std::pair<SUnit *, SUnit *>> AddedEdges;
    auto &SyncPipeline = CurrPipeline[CurrSyncGroupIdx];
    SchedGroup *Match;
    for (auto &SG : SyncPipeline) {
      if (SG.getSGID() == CandSGID)
        Match = &SG;
    }

    if (Match->isFull())
      continue;

    if (!Match->allowedByRules(CurrSU.first, SyncPipeline))
      continue;

    LLVM_DEBUG(dbgs() << "Assigning to SchedGroup with Mask "
                      << (int)Match->getMask() << "and ID " << CandSGID
                      << "\n");
    Match->add(*CurrSU.first);
    AddedCost = addEdges(SyncPipeline, CurrSU.first, CandSGID, AddedEdges);
    LLVM_DEBUG(dbgs() << "Cost of Assignment: " << AddedCost << "\n");
    CurrCost += AddedCost;
    advancePosition();
    ++BranchesExplored;
    bool FinishedExploring = false;
    // If the Cost after adding edges is greater than a known solution,
    // backtrack
    if (CurrCost < BestCost || BestCost == -1) {
      if (solveExact()) {
        FinishedExploring = BestCost != 0;
        if (!FinishedExploring)
          return true;
      }
    }

    retreatPosition();
    CurrCost -= AddedCost;
    removeEdges(AddedEdges);
    Match->pop();
    CurrPipeline[CurrSyncGroupIdx] = SyncPipeline;
    if (FinishedExploring)
      return true;
  }

  // Try the pipeline where the current instruction is omitted
  // Potentially if we omit a problematic instruction from the pipeline,
  // all the other instructions can nicely fit.
  CurrCost += MissPenalty;
  advancePosition();

  LLVM_DEBUG(dbgs() << "NOT Assigned (" << CurrSU.first->NodeNum << ")\n");

  bool FinishedExploring = false;
  if (CurrCost < BestCost || BestCost == -1) {
    if (solveExact()) {
      bool FinishedExploring = BestCost != 0;
      if (!FinishedExploring)
        return true;
    }
  }

  retreatPosition();
  CurrCost -= MissPenalty;
  return FinishedExploring;
}

template <typename T>
void PipelineSolver::greedyFind(
    std::vector<std::pair<SUnit *, SUnit *>> &AddedEdges, T I, T E) {
  SUToCandSGsPair CurrSU = PipelineInstrs[CurrSyncGroupIdx][CurrConflInstNo];
  int BestNodeCost = -1;
  int TempCost;
  SchedGroup *BestGroup = nullptr;
  int BestGroupID = -1;
  auto &SyncPipeline = CurrPipeline[CurrSyncGroupIdx];
  LLVM_DEBUG(dbgs() << "Fitting SU(" << CurrSU.first->NodeNum
                    << ") in Pipeline # " << CurrSyncGroupIdx << "\n");

  // Since we have added the potential SchedGroups from bottom up, but
  // traversed the DAG from top down, parse over the groups from last to
  // first. If we fail to do this for the greedy algorithm, the solution will
  // likely not be good in more complex cases.
  for (; I != E; ++I) {
    std::vector<std::pair<SUnit *, SUnit *>> AddedEdges;
    int CandSGID = *I;
    SchedGroup *Match = llvm::find_if(SyncPipeline, [CandSGID](SchedGroup &SG) {
      return SG.getSGID() == CandSGID;
    });
    assert(Match);

    LLVM_DEBUG(dbgs() << "Trying SGID # " << CandSGID << " with Mask "
                      << (int)Match->getMask() << "\n");

    if (Match->isFull()) {
      LLVM_DEBUG(dbgs() << "SGID # " << CandSGID << " is full\n");
      continue;
    }
    if (!Match->allowedByRules(CurrSU.first, SyncPipeline)) {
      LLVM_DEBUG(dbgs() << "SGID # " << CandSGID << " has conflicting rule\n");
      continue;
    }
    TempCost = addEdges(SyncPipeline, CurrSU.first, CandSGID, AddedEdges);
    LLVM_DEBUG(dbgs() << "Cost of Group " << TempCost << "\n");
    if (TempCost < BestNodeCost || BestNodeCost == -1) {
      BestGroup = Match;
      BestNodeCost = TempCost;
      BestGroupID = CandSGID;
    }
    removeEdges(AddedEdges);
    if (BestNodeCost == 0)
      break;
  }

  if (BestGroupID != -1) {
    BestGroup->add(*CurrSU.first);
    addEdges(SyncPipeline, CurrSU.first, BestGroupID, AddedEdges);
    LLVM_DEBUG(dbgs() << "Best Group has ID: " << BestGroupID << " and Mask"
                      << (int)BestGroup->getMask() << "\n");
    BestCost += TempCost;
  } else
    BestCost += MissPenalty;

  CurrPipeline[CurrSyncGroupIdx] = SyncPipeline;
}

bool PipelineSolver::solveGreedy() {
  BestCost = 0;
  std::vector<std::pair<SUnit *, SUnit *>> AddedEdges;

  while (static_cast<size_t>(CurrSyncGroupIdx) < PipelineInstrs.size()) {
    SUToCandSGsPair CurrSU = PipelineInstrs[CurrSyncGroupIdx][CurrConflInstNo];
    IsBottomUp
        ? greedyFind(AddedEdges, CurrSU.second.rbegin(), CurrSU.second.rend())
        : greedyFind(AddedEdges, CurrSU.second.begin(), CurrSU.second.end());
    advancePosition();
  }
  BestPipeline = CurrPipeline;
  removeEdges(AddedEdges);
  return false;
}

unsigned PipelineSolver::computeProblemSize() {
  unsigned ProblemSize = 0;
  for (auto &PipeConflicts : PipelineInstrs) {
    ProblemSize += PipeConflicts.size();
  }

  return ProblemSize;
}

void PipelineSolver::solve() {
  if (!NeedsSolver)
    return;

  unsigned ProblemSize = computeProblemSize();
  assert(ProblemSize > 0);

  bool BelowCutoff = (CutoffForExact > 0) && ProblemSize <= CutoffForExact;
  MissPenalty = (ProblemSize / 2) + 1;

  LLVM_DEBUG(DAG->dump());
  if (EnableExactSolver || BelowCutoff) {
    LLVM_DEBUG(dbgs() << "Starting Greedy pipeline solver\n");
    solveGreedy();
    reset();
    LLVM_DEBUG(dbgs() << "Greedy produced best cost of " << BestCost << "\n");
    if (BestCost > 0) {
      LLVM_DEBUG(dbgs() << "Starting EXACT pipeline solver\n");
      solveExact();
      LLVM_DEBUG(dbgs() << "Exact produced best cost of " << BestCost << "\n");
    }
  } else { // Use the Greedy Algorithm by default
    LLVM_DEBUG(dbgs() << "Starting GREEDY pipeline solver\n");
    solveGreedy();
  }

  makePipeline();
  LLVM_DEBUG(dbgs() << "After applying mutation\n");
  LLVM_DEBUG(DAG->dump());
}

enum IGLPStrategyID : int {
  MFMASmallGemmOptID = 0,
  MFMASmallGemmSingleWaveOptID = 1,
  MFMAExpInterleave = 2
};

// Implement a IGLP scheduling strategy.
class IGLPStrategy {
protected:
  ScheduleDAGInstrs *DAG;

  const SIInstrInfo *TII;

public:
  /// Add SchedGroups to \p SyncedSchedGroups to implement this Strategy.
  virtual bool applyIGLPStrategy(
      DenseMap<int, SUnitsToCandidateSGsMap> &SyncedInstrs,
      DenseMap<int, SmallVector<SchedGroup, 4>> &SyncedSchedGroups,
      AMDGPU::SchedulingPhase Phase) = 0;

  // Returns true if this strategy should be applied to a ScheduleDAG.
  virtual bool shouldApplyStrategy(ScheduleDAGInstrs *DAG,
                                   AMDGPU::SchedulingPhase Phase) = 0;

  bool IsBottomUp = true;

  IGLPStrategy(ScheduleDAGInstrs *DAG, const SIInstrInfo *TII)
      : DAG(DAG), TII(TII) {}

  virtual ~IGLPStrategy() = default;
};

class MFMASmallGemmOpt final : public IGLPStrategy {
private:
public:
  bool applyIGLPStrategy(
      DenseMap<int, SUnitsToCandidateSGsMap> &SyncedInstrs,
      DenseMap<int, SmallVector<SchedGroup, 4>> &SyncedSchedGroups,
      AMDGPU::SchedulingPhase Phase) override;

  bool shouldApplyStrategy(ScheduleDAGInstrs *DAG,
                           AMDGPU::SchedulingPhase Phase) override {
    return true;
  }

  MFMASmallGemmOpt(ScheduleDAGInstrs *DAG, const SIInstrInfo *TII)
      : IGLPStrategy(DAG, TII) {
    IsBottomUp = true;
  }
};

bool MFMASmallGemmOpt::applyIGLPStrategy(
    DenseMap<int, SUnitsToCandidateSGsMap> &SyncedInstrs,
    DenseMap<int, SmallVector<SchedGroup, 4>> &SyncedSchedGroups,
    AMDGPU::SchedulingPhase Phase) {
  // Count the number of MFMA instructions.
  unsigned MFMACount = 0;
  for (const MachineInstr &I : *DAG)
    if (TII->isMFMAorWMMA(I))
      ++MFMACount;

  const unsigned PipelineSyncID = 0;
  SchedGroup *SG = nullptr;
  for (unsigned I = 0; I < MFMACount * 3; ++I) {
    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::DS, 2, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
  }

  return true;
}

class MFMAExpInterleaveOpt final : public IGLPStrategy {
private:
  // The count of TRANS SUs involved in the interleaved pipeline
  static unsigned TransPipeCount;
  // The count of MFMA SUs involved in the interleaved pipeline
  static unsigned MFMAPipeCount;
  // The count of Add SUs involved in the interleaved pipeline
  static unsigned AddPipeCount;
  // The number of transitive MFMA successors for each TRANS SU
  static unsigned MFMAEnablement;
  // The number of transitive TRANS predecessors for each MFMA SU
  static unsigned ExpRequirement;
  // The count of independent "chains" of MFMA instructions in the pipeline
  static unsigned MFMAChains;
  // The length of each independent "chain" of MFMA instructions
  static unsigned MFMAChainLength;
  // Whether or not the pipeline has V_CVT instructions
  static bool HasCvt;
  // Whether or not there are instructions between the TRANS instruction and
  // V_CVT
  static bool HasChainBetweenCvt;
  // The first occuring DS_READ which feeds an MFMA chain
  static std::optional<unsigned> FirstPipeDSR;
  // The MFMAPipe SUs with no MFMA predecessors
  SmallVector<SUnit *, 4> MFMAChainSeeds;
  // Compute the heuristics for the pipeline, returning whether or not the DAG
  // is well formatted for the mutation
  bool analyzeDAG(const SIInstrInfo *TII);

  /// Whether or not the instruction is a transitive predecessor of an MFMA
  /// instruction
  class IsPipeExp final : public InstructionRule {
  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {

      auto DAG = SyncPipe[0].DAG;

      if (Cache->empty()) {
        auto I = DAG->SUnits.rbegin();
        auto E = DAG->SUnits.rend();
        for (; I != E; I++) {
          if (TII->isMFMAorWMMA(*I->getInstr()))
            Cache->push_back(&*I);
        }
        if (Cache->empty())
          return false;
      }

      auto Reaches = (std::any_of(
          Cache->begin(), Cache->end(), [&SU, &DAG](SUnit *TargetSU) {
            return DAG->IsReachable(TargetSU, const_cast<SUnit *>(SU));
          }));

      return Reaches;
    }
    IsPipeExp(const SIInstrInfo *TII, unsigned SGID, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache) {}
  };

  /// Whether or not the instruction is a transitive predecessor of the
  /// \p Number th MFMA of the MFMAs occuring after a TRANS instruction
  class EnablesNthMFMA final : public InstructionRule {
  private:
    unsigned Number = 1;

  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      bool FoundTrans = false;
      unsigned Counter = 1;
      auto DAG = SyncPipe[0].DAG;

      if (Cache->empty()) {
        SmallVector<SUnit *, 8> Worklist;

        auto I = DAG->SUnits.begin();
        auto E = DAG->SUnits.end();
        for (; I != E; I++) {
          if (FoundTrans && TII->isMFMAorWMMA(*I->getInstr())) {
            if (Counter == Number) {
              Cache->push_back(&*I);
              break;
            }
            ++Counter;
          }
          if (!FoundTrans && TII->isTRANS(I->getInstr()->getOpcode()))
            FoundTrans = true;
        }
        if (Cache->empty())
          return false;
      }

      return DAG->IsReachable((*Cache)[0], const_cast<SUnit *>(SU));
    }

    EnablesNthMFMA(unsigned Number, const SIInstrInfo *TII, unsigned SGID,
                   bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache), Number(Number) {}
  };

  /// Whether or not the instruction enables the exact MFMA that is the \p
  /// Number th MFMA in the chain starting with \p ChainSeed
  class EnablesNthMFMAInChain final : public InstructionRule {
  private:
    unsigned Number = 1;
    SUnit *ChainSeed;

  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      auto DAG = SyncPipe[0].DAG;

      if (!SU || !TII->isMFMAorWMMA(*ChainSeed->getInstr()))
        return false;

      if (Cache->empty()) {
        auto TempSU = ChainSeed;
        auto Depth = Number;
        while (Depth > 0) {
          --Depth;
          bool Found = false;
          for (auto &Succ : TempSU->Succs) {
            if (TII->isMFMAorWMMA(*Succ.getSUnit()->getInstr())) {
              TempSU = Succ.getSUnit();
              Found = true;
              break;
            }
          }
          if (!Found)
            return false;
        }

        Cache->push_back(TempSU);
      }
      // If we failed to find the instruction to be placed into the cache, we
      // would have already exited.
      assert(!Cache->empty());

      return DAG->IsReachable((*Cache)[0], const_cast<SUnit *>(SU));
    }

    EnablesNthMFMAInChain(unsigned Number, SUnit *ChainSeed,
                          const SIInstrInfo *TII, unsigned SGID,
                          bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache), Number(Number),
          ChainSeed(ChainSeed) {}
  };

  /// Whether or not the instruction has less than \p Size immediate successors.
  /// If \p HasIntermediary is true, this tests also whether all successors of
  /// the SUnit have less than \p Size successors.
  class LessThanNSuccs final : public InstructionRule {
  private:
    unsigned Size = 1;
    bool HasIntermediary = false;

  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      if (!SyncPipe.size())
        return false;

      auto SuccSize = std::count_if(
          SU->Succs.begin(), SU->Succs.end(),
          [](const SDep &Succ) { return Succ.getKind() == SDep::Data; });
      if (SuccSize >= Size)
        return false;

      if (HasIntermediary) {
        for (auto Succ : SU->Succs) {
          auto SuccSize = std::count_if(
              Succ.getSUnit()->Succs.begin(), Succ.getSUnit()->Succs.end(),
              [](const SDep &SuccSucc) {
                return SuccSucc.getKind() == SDep::Data;
              });
          if (SuccSize >= Size)
            return false;
        }
      }

      return true;
    }
    LessThanNSuccs(unsigned Size, const SIInstrInfo *TII, unsigned SGID,
                   bool HasIntermediary = false, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache), Size(Size),
          HasIntermediary(HasIntermediary) {}
  };

  /// Whether or not the instruction has greater than or equal to \p Size
  /// immediate successors. If \p HasIntermediary is true, this tests also
  /// whether all successors of the SUnit have greater than or equal to \p Size
  /// successors.
  class GreaterThanOrEqualToNSuccs final : public InstructionRule {
  private:
    unsigned Size = 1;
    bool HasIntermediary = false;

  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      if (!SyncPipe.size())
        return false;

      auto SuccSize = std::count_if(
          SU->Succs.begin(), SU->Succs.end(),
          [](const SDep &Succ) { return Succ.getKind() == SDep::Data; });
      if (SuccSize >= Size)
        return true;

      if (HasIntermediary) {
        for (auto Succ : SU->Succs) {
          auto SuccSize = std::count_if(
              Succ.getSUnit()->Succs.begin(), Succ.getSUnit()->Succs.end(),
              [](const SDep &SuccSucc) {
                return SuccSucc.getKind() == SDep::Data;
              });
          if (SuccSize >= Size)
            return true;
        }
      }

      return false;
    }
    GreaterThanOrEqualToNSuccs(unsigned Size, const SIInstrInfo *TII,
                               unsigned SGID, bool HasIntermediary = false,
                               bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache), Size(Size),
          HasIntermediary(HasIntermediary) {}
  };

  // Whether or not the instruction is a relevant V_CVT instruction.
  class IsCvt final : public InstructionRule {
  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      auto Opc = SU->getInstr()->getOpcode();
      return Opc == AMDGPU::V_CVT_F16_F32_e32 ||
             Opc == AMDGPU::V_CVT_I32_F32_e32;
    }
    IsCvt(const SIInstrInfo *TII, unsigned SGID, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache) {}
  };

  // Whether or not the instruction is FMA_F32.
  class IsFMA final : public InstructionRule {
  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      return SU->getInstr()->getOpcode() == AMDGPU::V_FMA_F32_e64 ||
             SU->getInstr()->getOpcode() == AMDGPU::V_PK_FMA_F32;
    }
    IsFMA(const SIInstrInfo *TII, unsigned SGID, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache) {}
  };

  // Whether or not the instruction is a V_ADD_F32 instruction.
  class IsPipeAdd final : public InstructionRule {
  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      return SU->getInstr()->getOpcode() == AMDGPU::V_ADD_F32_e32;
    }
    IsPipeAdd(const SIInstrInfo *TII, unsigned SGID, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache) {}
  };

  /// Whether or not the instruction is an immediate RAW successor
  /// of the SchedGroup \p Distance steps before.
  class IsSuccOfPrevNthGroup final : public InstructionRule {
  private:
    unsigned Distance = 1;

  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      SchedGroup *OtherGroup = nullptr;
      if (!SyncPipe.size())
        return false;

      for (auto &PipeSG : SyncPipe) {
        if ((unsigned)PipeSG.getSGID() == SGID - Distance)
          OtherGroup = &PipeSG;
      }

      if (!OtherGroup)
        return false;
      if (!OtherGroup->Collection.size())
        return true;

      for (auto &OtherEle : OtherGroup->Collection) {
        for (auto &Succ : OtherEle->Succs) {
          if (Succ.getSUnit() == SU && Succ.getKind() == SDep::Data)
            return true;
        }
      }

      return false;
    }
    IsSuccOfPrevNthGroup(unsigned Distance, const SIInstrInfo *TII,
                         unsigned SGID, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache), Distance(Distance) {}
  };

  /// Whether or not the instruction is a transitive successor of any
  /// instruction the the SchedGroup \p Distance steps before.
  class IsReachableFromPrevNthGroup final : public InstructionRule {
  private:
    unsigned Distance = 1;

  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      SchedGroup *OtherGroup = nullptr;
      if (!SyncPipe.size())
        return false;

      for (auto &PipeSG : SyncPipe) {
        if ((unsigned)PipeSG.getSGID() == SGID - Distance)
          OtherGroup = &PipeSG;
      }

      if (!OtherGroup)
        return false;
      if (!OtherGroup->Collection.size())
        return true;

      auto DAG = SyncPipe[0].DAG;

      for (auto &OtherEle : OtherGroup->Collection)
        if (DAG->IsReachable(const_cast<SUnit *>(SU), OtherEle))
          return true;

      return false;
    }
    IsReachableFromPrevNthGroup(unsigned Distance, const SIInstrInfo *TII,
                                unsigned SGID, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache), Distance(Distance) {}
  };

  /// Whether or not the instruction occurs after the SU with NodeNUm \p Number
  class OccursAtOrAfterNode final : public InstructionRule {
  private:
    unsigned Number = 1;

  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {

      return SU->NodeNum >= Number;
    }
    OccursAtOrAfterNode(unsigned Number, const SIInstrInfo *TII, unsigned SGID,
                        bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache), Number(Number) {}
  };

  /// Whether or not the SU is exactly the \p Number th MFMA in the chain
  /// starting with \p ChainSeed
  class IsExactMFMA final : public InstructionRule {
  private:
    unsigned Number = 1;
    SUnit *ChainSeed;

  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      if (!SU || !TII->isMFMAorWMMA(*ChainSeed->getInstr()))
        return false;

      if (Cache->empty()) {
        auto TempSU = ChainSeed;
        auto Depth = Number;
        while (Depth > 0) {
          --Depth;
          bool Found = false;
          for (auto &Succ : TempSU->Succs) {
            if (TII->isMFMAorWMMA(*Succ.getSUnit()->getInstr())) {
              TempSU = Succ.getSUnit();
              Found = true;
              break;
            }
          }
          if (!Found) {
            return false;
          }
        }
        Cache->push_back(TempSU);
      }
      // If we failed to find the instruction to be placed into the cache, we
      // would have already exited.
      assert(!Cache->empty());

      return (*Cache)[0] == SU;
    }

    IsExactMFMA(unsigned Number, SUnit *ChainSeed, const SIInstrInfo *TII,
                unsigned SGID, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache), Number(Number),
          ChainSeed(ChainSeed) {}
  };

  // Whether the instruction occurs after the first TRANS instruction. This
  // implies the instruction can not be a predecessor of the first TRANS
  // insruction
  class OccursAfterExp final : public InstructionRule {
  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {

      SmallVector<SUnit *, 12> Worklist;
      auto DAG = SyncPipe[0].DAG;
      if (Cache->empty()) {
        for (auto &SU : DAG->SUnits)
          if (TII->isTRANS(SU.getInstr()->getOpcode())) {
            Cache->push_back(&SU);
            break;
          }
        if (Cache->empty())
          return false;
      }

      return SU->NodeNum > (*Cache)[0]->NodeNum;
    }

    OccursAfterExp(const SIInstrInfo *TII, unsigned SGID,
                   bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache) {}
  };

public:
  bool applyIGLPStrategy(
      DenseMap<int, SUnitsToCandidateSGsMap> &SyncedInstrs,
      DenseMap<int, SmallVector<SchedGroup, 4>> &SyncedSchedGroups,
      AMDGPU::SchedulingPhase Phase) override;

  bool shouldApplyStrategy(ScheduleDAGInstrs *DAG,
                           AMDGPU::SchedulingPhase Phase) override;

  MFMAExpInterleaveOpt(ScheduleDAGInstrs *DAG, const SIInstrInfo *TII)
      : IGLPStrategy(DAG, TII) {
    IsBottomUp = false;
  }
};

unsigned MFMAExpInterleaveOpt::TransPipeCount = 0;
unsigned MFMAExpInterleaveOpt::MFMAPipeCount = 0;
unsigned MFMAExpInterleaveOpt::AddPipeCount = 0;
unsigned MFMAExpInterleaveOpt::MFMAEnablement = 0;
unsigned MFMAExpInterleaveOpt::ExpRequirement = 0;
unsigned MFMAExpInterleaveOpt::MFMAChains = 0;
unsigned MFMAExpInterleaveOpt::MFMAChainLength = 0;
bool MFMAExpInterleaveOpt::HasCvt = false;
bool MFMAExpInterleaveOpt::HasChainBetweenCvt = false;
std::optional<unsigned> MFMAExpInterleaveOpt::FirstPipeDSR = std::nullopt;

bool MFMAExpInterleaveOpt::analyzeDAG(const SIInstrInfo *TII) {
  SmallVector<SUnit *, 10> ExpPipeCands;
  SmallVector<SUnit *, 10> MFMAPipeCands;
  SmallVector<SUnit *, 10> MFMAPipeSUs;
  SmallVector<SUnit *, 10> PackSUs;
  SmallVector<SUnit *, 10> CvtSUs;

  auto isBitPack = [](unsigned Opc) {
    return Opc == AMDGPU::V_PACK_B32_F16_e64 || Opc == AMDGPU::V_PERM_B32_e64;
  };

  auto isCvt = [](unsigned Opc) {
    return Opc == AMDGPU::V_CVT_F16_F32_e32 || Opc == AMDGPU::V_CVT_I32_F32_e32;
  };

  auto isAdd = [](unsigned Opc) { return Opc == AMDGPU::V_ADD_F32_e32; };

  AddPipeCount = 0;
  for (SUnit &SU : DAG->SUnits) {
    auto Opc = SU.getInstr()->getOpcode();
    if (TII->isTRANS(Opc)) {
      // Avoid counting a potential bonus V_EXP which all the MFMA depend on
      if (SU.Succs.size() >= 7)
        continue;
      for (auto &Succ : SU.Succs) {
        if (Succ.getSUnit()->Succs.size() >= 7)
          continue;
      }
      ExpPipeCands.push_back(&SU);
    }

    if (TII->isMFMAorWMMA(*SU.getInstr()))
      MFMAPipeCands.push_back(&SU);

    if (isBitPack(Opc))
      PackSUs.push_back(&SU);

    if (isCvt(Opc))
      CvtSUs.push_back(&SU);

    if (isAdd(Opc))
      ++AddPipeCount;
  }

  if (!(PackSUs.size() && MFMAPipeCands.size() && ExpPipeCands.size()))
    return false;

  TransPipeCount = 0;

  std::optional<SUnit *> TempMFMA;
  std::optional<SUnit *> TempExp;
  // Count the number of EXPs that reach an MFMA
  for (auto &PredSU : ExpPipeCands) {
    for (auto &SuccSU : MFMAPipeCands) {
      if (DAG->IsReachable(SuccSU, PredSU)) {
        if (!TempExp) {
          TempExp = PredSU;
          TempMFMA = SuccSU;
        }
        MFMAPipeSUs.push_back(SuccSU);
        ++TransPipeCount;
        break;
      }
    }
  }

  if (!(TempExp && TempMFMA))
    return false;

  HasChainBetweenCvt =
      std::find_if((*TempExp)->Succs.begin(), (*TempExp)->Succs.end(),
                   [&isCvt](SDep &Succ) {
                     return isCvt(Succ.getSUnit()->getInstr()->getOpcode());
                   }) == (*TempExp)->Succs.end();

  // Count the number of MFMAs that are reached by an EXP
  for (auto &SuccSU : MFMAPipeCands) {
    if (MFMAPipeSUs.size() &&
        std::find_if(MFMAPipeSUs.begin(), MFMAPipeSUs.end(),
                     [&SuccSU](SUnit *PotentialMatch) {
                       return PotentialMatch->NodeNum == SuccSU->NodeNum;
                     }) != MFMAPipeSUs.end())
      continue;

    for (auto &PredSU : ExpPipeCands) {
      if (DAG->IsReachable(SuccSU, PredSU)) {
        MFMAPipeSUs.push_back(SuccSU);
        break;
      }
    }
  }

  MFMAPipeCount = MFMAPipeSUs.size();

  assert(TempExp && TempMFMA);
  assert(MFMAPipeCount > 0);

  std::optional<SUnit *> TempCvt;
  for (auto &SuccSU : CvtSUs) {
    if (DAG->IsReachable(SuccSU, *TempExp)) {
      TempCvt = SuccSU;
      break;
    }
  }

  HasCvt = false;
  if (TempCvt.has_value()) {
    for (auto &SuccSU : MFMAPipeSUs) {
      if (DAG->IsReachable(SuccSU, *TempCvt)) {
        HasCvt = true;
        break;
      }
    }
  }

  MFMAChains = 0;
  for (auto &MFMAPipeSU : MFMAPipeSUs) {
    if (is_contained(MFMAChainSeeds, MFMAPipeSU))
      continue;
    if (!std::any_of(MFMAPipeSU->Preds.begin(), MFMAPipeSU->Preds.end(),
                     [&TII](SDep &Succ) {
                       return TII->isMFMAorWMMA(*Succ.getSUnit()->getInstr());
                     })) {
      MFMAChainSeeds.push_back(MFMAPipeSU);
      ++MFMAChains;
    }
  }

  if (!MFMAChains)
    return false;

  for (auto Pred : MFMAChainSeeds[0]->Preds) {
    if (TII->isDS(Pred.getSUnit()->getInstr()->getOpcode()) &&
        Pred.getSUnit()->getInstr()->mayLoad())
      FirstPipeDSR = Pred.getSUnit()->NodeNum;
  }

  MFMAChainLength = MFMAPipeCount / MFMAChains;

  // The number of bit pack operations that depend on a single V_EXP
  unsigned PackSuccCount = std::count_if(
      PackSUs.begin(), PackSUs.end(), [this, &TempExp](SUnit *VPack) {
        return DAG->IsReachable(VPack, *TempExp);
      });

  // The number of bit pack operations an MFMA depends on
  unsigned PackPredCount =
      std::count_if((*TempMFMA)->Preds.begin(), (*TempMFMA)->Preds.end(),
                    [&isBitPack](SDep &Pred) {
                      auto Opc = Pred.getSUnit()->getInstr()->getOpcode();
                      return isBitPack(Opc);
                    });

  auto PackPred =
      std::find_if((*TempMFMA)->Preds.begin(), (*TempMFMA)->Preds.end(),
                   [&isBitPack](SDep &Pred) {
                     auto Opc = Pred.getSUnit()->getInstr()->getOpcode();
                     return isBitPack(Opc);
                   });

  if (PackPred == (*TempMFMA)->Preds.end())
    return false;

  MFMAEnablement = 0;
  ExpRequirement = 0;
  // How many MFMAs depend on a single bit pack operation
  MFMAEnablement =
      std::count_if(PackPred->getSUnit()->Succs.begin(),
                    PackPred->getSUnit()->Succs.end(), [&TII](SDep &Succ) {
                      return TII->isMFMAorWMMA(*Succ.getSUnit()->getInstr());
                    });

  // The number of MFMAs that depend on a single V_EXP
  MFMAEnablement *= PackSuccCount;

  // The number of V_EXPs required to resolve all dependencies for an MFMA
  ExpRequirement =
      std::count_if(ExpPipeCands.begin(), ExpPipeCands.end(),
                    [this, &PackPred](SUnit *ExpBase) {
                      return DAG->IsReachable(PackPred->getSUnit(), ExpBase);
                    });

  ExpRequirement *= PackPredCount;
  return true;
}

bool MFMAExpInterleaveOpt::shouldApplyStrategy(ScheduleDAGInstrs *DAG,
                                               AMDGPU::SchedulingPhase Phase) {
  const GCNSubtarget &ST = DAG->MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();

  if (Phase != AMDGPU::SchedulingPhase::PostRA)
    MFMAChainSeeds.clear();
  if (Phase != AMDGPU::SchedulingPhase::PostRA && !analyzeDAG(TII))
    return false;

  return true;
}

bool MFMAExpInterleaveOpt::applyIGLPStrategy(
    DenseMap<int, SUnitsToCandidateSGsMap> &SyncedInstrs,
    DenseMap<int, SmallVector<SchedGroup, 4>> &SyncedSchedGroups,
    AMDGPU::SchedulingPhase Phase) {

  bool IsSmallKernelType =
      MFMAEnablement == 2 && ExpRequirement == 4 && TransPipeCount == 32;
  bool IsLargeKernelType =
      MFMAEnablement == 4 && ExpRequirement == 4 && TransPipeCount == 64;

  if (!(IsSmallKernelType || IsLargeKernelType))
    return false;

  const GCNSubtarget &ST = DAG->MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();

  unsigned PipelineSyncID = 0;
  SchedGroup *SG = nullptr;

  unsigned MFMAChain = 0;
  unsigned PositionInChain = 0;
  unsigned CurrMFMAForTransPosition = 0;

  auto incrementTransPosition = [&MFMAChain, &PositionInChain,
                                 &CurrMFMAForTransPosition]() {
    CurrMFMAForTransPosition += MFMAEnablement;
    PositionInChain = (CurrMFMAForTransPosition / MFMAChains);
    MFMAChain = CurrMFMAForTransPosition % MFMAChains;
  };

  auto getNextTransPositionInChain = [&CurrMFMAForTransPosition]() {
    auto TempMFMAForTrans = CurrMFMAForTransPosition + MFMAEnablement;
    return (TempMFMAForTrans / MFMAChains);
  };

  auto getNextTransMFMAChain = [&CurrMFMAForTransPosition]() {
    auto TempMFMAForTrans = CurrMFMAForTransPosition + MFMAEnablement;
    return TempMFMAForTrans % MFMAChains;
  };

  unsigned CurrMFMAPosition = 0;
  unsigned MFMAChainForMFMA = 0;
  unsigned PositionInChainForMFMA = 0;

  auto incrementMFMAPosition = [&CurrMFMAPosition, &MFMAChainForMFMA,
                                &PositionInChainForMFMA]() {
    ++CurrMFMAPosition;
    MFMAChainForMFMA = CurrMFMAPosition % MFMAChains;
    PositionInChainForMFMA = CurrMFMAPosition / MFMAChains;
  };

  bool IsPostRA = Phase == AMDGPU::SchedulingPhase::PostRA;
  assert(IsPostRA || MFMAChainSeeds.size() == MFMAChains);

  bool UsesFMA = IsSmallKernelType || !IsPostRA;
  bool UsesDSRead = IsLargeKernelType && !IsPostRA && FirstPipeDSR;
  bool UsesCvt = HasCvt && (IsSmallKernelType || !IsPostRA);
  bool UsesVALU = IsSmallKernelType;

  // PHASE 1: "Prefetch"
  if (UsesFMA) {
    // First Round FMA
    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::VALU, ExpRequirement, PipelineSyncID, DAG, TII);
    if (!IsPostRA && MFMAChains) {
      SG->addRule(std::make_shared<EnablesNthMFMAInChain>(
          PositionInChain, MFMAChainSeeds[MFMAChain], TII, SG->getSGID(),
          true));
    } else
      SG->addRule(
          std::make_shared<EnablesNthMFMA>(1, TII, SG->getSGID(), true));
    SG->addRule(std::make_shared<IsFMA>(TII, SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    // Second Round FMA
    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::VALU, ExpRequirement, PipelineSyncID, DAG, TII);
    if (!IsPostRA && MFMAChains) {
      SG->addRule(std::make_shared<EnablesNthMFMAInChain>(
          getNextTransPositionInChain(),
          MFMAChainSeeds[getNextTransMFMAChain()], TII, SG->getSGID(), true));
    } else
      SG->addRule(std::make_shared<EnablesNthMFMA>(MFMAEnablement + 1, TII,
                                                   SG->getSGID(), true));
    SG->addRule(std::make_shared<IsFMA>(TII, SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
  }

  if (UsesDSRead) {
    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::DS_READ, 2, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<OccursAtOrAfterNode>(*FirstPipeDSR, TII,
                                                      SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
  }

  // First Round EXP
  SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
      SchedGroupMask::TRANS, ExpRequirement, PipelineSyncID, DAG, TII);
  if (!IsPostRA && MFMAChains)
    SG->addRule(std::make_shared<EnablesNthMFMAInChain>(
        PositionInChain, MFMAChainSeeds[MFMAChain], TII, SG->getSGID(), true));
  else
    SG->addRule(std::make_shared<EnablesNthMFMA>(1, TII, SG->getSGID(), true));
  SG->addRule(std::make_shared<IsPipeExp>(TII, SG->getSGID(), true));
  SG->addRule(std::make_shared<LessThanNSuccs>(8, TII, SG->getSGID(),
                                               HasChainBetweenCvt));
  SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

  incrementTransPosition();

  // First Round CVT, Third Round FMA, Second Round EXP; interleaved
  for (unsigned I = 0; I < ExpRequirement; I++) {
    // First Round CVT
    if (UsesCvt) {
      SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
          SchedGroupMask::VALU, 1, PipelineSyncID, DAG, TII);
      SG->addRule(std::make_shared<IsCvt>(TII, SG->getSGID()));
      if (HasChainBetweenCvt)
        SG->addRule(std::make_shared<IsReachableFromPrevNthGroup>(
            1 + (2 + UsesFMA) * I, TII, SG->getSGID()));
      else
        SG->addRule(std::make_shared<IsSuccOfPrevNthGroup>(
            1 + (2 + UsesFMA) * I, TII, SG->getSGID()));
      SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
    }

    // Third Round FMA
    if (UsesFMA) {
      SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
          SchedGroupMask::VALU, 1, PipelineSyncID, DAG, TII);
      if (!IsPostRA && MFMAChains) {
        SG->addRule(std::make_shared<EnablesNthMFMAInChain>(
            getNextTransPositionInChain(),
            MFMAChainSeeds[getNextTransMFMAChain()], TII, SG->getSGID(), true));
      } else
        SG->addRule(std::make_shared<EnablesNthMFMA>(2 * MFMAEnablement + 1,
                                                     TII, SG->getSGID(), true));
      SG->addRule(std::make_shared<IsFMA>(TII, SG->getSGID()));
      SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
    }

    // Second Round EXP
    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::TRANS, 1, PipelineSyncID, DAG, TII);
    if (!IsPostRA && MFMAChains)
      SG->addRule(std::make_shared<EnablesNthMFMAInChain>(
          PositionInChain, MFMAChainSeeds[MFMAChain], TII, SG->getSGID(),
          true));
    else
      SG->addRule(std::make_shared<EnablesNthMFMA>(MFMAEnablement + 1, TII,
                                                   SG->getSGID(), true));
    SG->addRule(std::make_shared<IsPipeExp>(TII, SG->getSGID(), true));
    SG->addRule(std::make_shared<LessThanNSuccs>(8, TII, SG->getSGID(),
                                                 HasChainBetweenCvt));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
  }

  // The "extra" EXP which enables all MFMA
  // TODO: UsesExtraExp
  SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
      SchedGroupMask::TRANS, 1, PipelineSyncID, DAG, TII);
  SG->addRule(std::make_shared<IsPipeExp>(TII, SG->getSGID(), true));
  SG->addRule(std::make_shared<GreaterThanOrEqualToNSuccs>(
      8, TII, SG->getSGID(), HasChainBetweenCvt));
  SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

  // PHASE 2: Main Interleave Loop

  // The number of MFMAs per iteration
  unsigned MFMARatio =
      MFMAEnablement > ExpRequirement ? MFMAEnablement / ExpRequirement : 1;
  // The number of Exps per iteration
  unsigned ExpRatio =
      MFMAEnablement > ExpRequirement ? 1 : ExpRequirement / MFMAEnablement;
  // The reamaining Exps
  unsigned RemainingExp = TransPipeCount > (2 * ExpRequirement)
                              ? TransPipeCount - (2 * ExpRequirement)
                              : 0;
  unsigned ExpLoopCount = RemainingExp / ExpRatio;
  // In loop MFMAs
  unsigned MFMAInLoop = MFMAPipeCount > (MFMAEnablement * 2)
                            ? MFMAPipeCount - (MFMAEnablement * 2)
                            : 0;
  unsigned MFMALoopCount = MFMAInLoop / MFMARatio;
  unsigned VALUOps =
      AddPipeCount < MFMAPipeCount ? 1 : AddPipeCount / MFMAPipeCount;
  unsigned LoopSize = std::min(ExpLoopCount, MFMALoopCount);

  for (unsigned I = 0; I < LoopSize; I++) {
    if (!(I * ExpRatio % ExpRequirement))
      incrementTransPosition();

    // Round N MFMA
    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::MFMA, MFMARatio, PipelineSyncID, DAG, TII);
    if (!IsPostRA && MFMAChains)
      SG->addRule(std::make_shared<IsExactMFMA>(
          PositionInChainForMFMA, MFMAChainSeeds[MFMAChainForMFMA], TII,
          SG->getSGID(), true));
    else
      SG->addRule(std::make_shared<OccursAfterExp>(TII, SG->getSGID(), true));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
    incrementMFMAPosition();

    if (UsesVALU) {
      SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
          SchedGroupMask::VALU, VALUOps, PipelineSyncID, DAG, TII);
      SG->addRule(std::make_shared<IsPipeAdd>(TII, SG->getSGID()));
      SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
    }

    if (UsesDSRead && !(I % 4)) {
      SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
          SchedGroupMask::DS_READ, 2, PipelineSyncID, DAG, TII);
      SG->addRule(std::make_shared<OccursAtOrAfterNode>(*FirstPipeDSR, TII,
                                                        SG->getSGID()));
      SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
    }

    // CVT, EXP, FMA Interleaving
    for (unsigned J = 0; J < ExpRatio; J++) {
      auto MFMAOffset = (1 + UsesVALU) * MFMARatio * (I + 1);
      auto MaxMFMAOffset =
          (1 + UsesVALU) * ExpRequirement * MFMARatio / ExpRatio;

      // Round N + 1 CVT
      if (UsesCvt) {
        SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
            SchedGroupMask::VALU, 1, PipelineSyncID, DAG, TII);
        SG->addRule(std::make_shared<IsCvt>(TII, SG->getSGID()));
        auto BaseDiff = (2 + UsesFMA) * (ExpRequirement - 1) + 1;
        auto DSROffset = I / 4 + 1;
        auto MaxDSROffset = MaxMFMAOffset / 4;
        // TODO: UsesExtraExp
        auto ExpOffset = I * ExpRatio + J >= ExpRequirement ? 0 : 1;
        auto CurrentOffset = UsesDSRead * std::min(MaxDSROffset, DSROffset) +
                             std::min(MaxMFMAOffset, MFMAOffset) + BaseDiff +
                             ExpOffset;
        if (HasChainBetweenCvt)
          SG->addRule(std::make_shared<IsReachableFromPrevNthGroup>(
              CurrentOffset, TII, SG->getSGID()));
        else
          SG->addRule(std::make_shared<IsSuccOfPrevNthGroup>(CurrentOffset, TII,
                                                             SG->getSGID()));
        SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
      }

      // Round N + 3 FMA
      if (UsesFMA) {
        SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
            SchedGroupMask::VALU, 1, PipelineSyncID, DAG, TII);
        if (!IsPostRA && MFMAChains)
          SG->addRule(std::make_shared<EnablesNthMFMAInChain>(
              getNextTransPositionInChain(),
              MFMAChainSeeds[getNextTransMFMAChain()], TII, SG->getSGID(),
              true));
        else
          SG->addRule(std::make_shared<EnablesNthMFMA>(
              (((I * ExpRatio + J) / ExpRequirement) + 3) * MFMAEnablement + 1,
              TII, SG->getSGID(), true));
        SG->addRule(std::make_shared<IsFMA>(TII, SG->getSGID()));
        SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
      }

      // Round N + 2 Exp
      SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
          SchedGroupMask::TRANS, 1, PipelineSyncID, DAG, TII);
      if (!IsPostRA && MFMAChains)
        SG->addRule(std::make_shared<EnablesNthMFMAInChain>(
            PositionInChain, MFMAChainSeeds[MFMAChain], TII, SG->getSGID(),
            true));
      else
        SG->addRule(std::make_shared<EnablesNthMFMA>(
            (((I * ExpRatio + J) / ExpRequirement) + 2) * MFMAEnablement + 1,
            TII, SG->getSGID(), true));
      SG->addRule(std::make_shared<IsPipeExp>(TII, SG->getSGID(), true));
      SG->addRule(std::make_shared<LessThanNSuccs>(8, TII, SG->getSGID(),
                                                   HasChainBetweenCvt));
      SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
    }
  }

  // PHASE 3: Remaining MFMAs
  SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
      SchedGroupMask::MFMA, MFMAEnablement * 2, PipelineSyncID, DAG, TII);
  SG->addRule(std::make_shared<OccursAfterExp>(TII, SG->getSGID(), true));
  SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
  return true;
}

class MFMASmallGemmSingleWaveOpt final : public IGLPStrategy {
private:
  // Whether the DS_READ is a predecessor of first four MFMA in region
  class EnablesInitialMFMA final : public InstructionRule {
  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      if (!SyncPipe.size())
        return false;
      int MFMAsFound = 0;
      if (!Cache->size()) {
        for (auto &Elt : SyncPipe[0].DAG->SUnits) {
          if (TII->isMFMAorWMMA(*Elt.getInstr())) {
            ++MFMAsFound;
            if (MFMAsFound > 4)
              break;
            Cache->push_back(&Elt);
          }
        }
      }

      assert(Cache->size());
      auto DAG = SyncPipe[0].DAG;
      for (auto &Elt : *Cache) {
        if (DAG->IsReachable(Elt, const_cast<SUnit *>(SU)))
          return true;
      }
      return false;
    }

    EnablesInitialMFMA(const SIInstrInfo *TII, unsigned SGID,
                       bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache) {}
  };

  // Whether the MI is a V_PERM and is a predecessor of a common DS_WRITE
  class IsPermForDSW final : public InstructionRule {
  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      auto MI = SU->getInstr();
      if (MI->getOpcode() != AMDGPU::V_PERM_B32_e64)
        return false;

      bool FitsInGroup = false;
      // Does the VALU have a DS_WRITE successor
      if (!Collection.size()) {
        for (auto &Succ : SU->Succs) {
          SUnit *SuccUnit = Succ.getSUnit();
          if (TII->isDS(*SuccUnit->getInstr()) &&
              SuccUnit->getInstr()->mayStore()) {
            Cache->push_back(SuccUnit);
            FitsInGroup = true;
          }
        }
        return FitsInGroup;
      }

      assert(Cache->size());

      // Does the VALU have a DS_WRITE successor that is the same as other
      // VALU already in the group. The V_PERMs will all share 1 DS_W succ
      return llvm::any_of(*Cache, [&SU](SUnit *Elt) {
        return llvm::any_of(SU->Succs, [&Elt](const SDep &ThisSucc) {
          return ThisSucc.getSUnit() == Elt;
        });
      });
    }

    IsPermForDSW(const SIInstrInfo *TII, unsigned SGID, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache) {}
  };

  // Whether the SU is a successor of any element in previous SchedGroup
  class IsSuccOfPrevGroup final : public InstructionRule {
  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      SchedGroup *OtherGroup = nullptr;
      for (auto &PipeSG : SyncPipe) {
        if ((unsigned)PipeSG.getSGID() == SGID - 1) {
          OtherGroup = &PipeSG;
        }
      }

      if (!OtherGroup)
        return false;
      if (!OtherGroup->Collection.size())
        return true;

      // Does the previous VALU have this DS_Write as a successor
      return (std::any_of(OtherGroup->Collection.begin(),
                          OtherGroup->Collection.end(), [&SU](SUnit *Elt) {
                            return std::any_of(Elt->Succs.begin(),
                                               Elt->Succs.end(),
                                               [&SU](SDep &Succ) {
                                                 return Succ.getSUnit() == SU;
                                               });
                          }));
    }
    IsSuccOfPrevGroup(const SIInstrInfo *TII, unsigned SGID,
                      bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache) {}
  };

  // Whether the combined load width of group is 128 bits
  class VMEMSize final : public InstructionRule {
  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      auto MI = SU->getInstr();
      if (MI->getOpcode() == TargetOpcode::BUNDLE)
        return false;
      if (!Collection.size())
        return true;

      int NumBits = 0;

      auto TRI = TII->getRegisterInfo();
      auto &MRI = MI->getParent()->getParent()->getRegInfo();
      for (auto &Elt : Collection) {
        auto Op = Elt->getInstr()->getOperand(0);
        auto Size =
            TRI.getRegSizeInBits(*TRI.getRegClassForOperandReg(MRI, Op));
        NumBits += Size;
      }

      if (NumBits < 128) {
        assert(TII->isVMEM(*MI) && MI->mayLoad());
        if (NumBits + TRI.getRegSizeInBits(*TRI.getRegClassForOperandReg(
                          MRI, MI->getOperand(0))) <=
            128)
          return true;
      }

      return false;
    }

    VMEMSize(const SIInstrInfo *TII, unsigned SGID, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache) {}
  };

  /// Whether the SU shares a V_PERM predecessor with any SU in the SchedGroup
  /// that is \p Distance steps away
  class SharesPredWithPrevNthGroup final : public InstructionRule {
  private:
    unsigned Distance = 1;

  public:
    bool apply(const SUnit *SU, const ArrayRef<SUnit *> Collection,
               SmallVectorImpl<SchedGroup> &SyncPipe) override {
      SchedGroup *OtherGroup = nullptr;
      if (!SyncPipe.size())
        return false;

      if (!Cache->size()) {

        for (auto &PipeSG : SyncPipe) {
          if ((unsigned)PipeSG.getSGID() == SGID - Distance) {
            OtherGroup = &PipeSG;
          }
        }

        if (!OtherGroup)
          return false;
        if (!OtherGroup->Collection.size())
          return true;

        for (auto &OtherEle : OtherGroup->Collection) {
          for (auto &Pred : OtherEle->Preds) {
            if (Pred.getSUnit()->getInstr()->getOpcode() ==
                AMDGPU::V_PERM_B32_e64)
              Cache->push_back(Pred.getSUnit());
          }
        }

        // If the other group has no PERM preds, then this group won't share any
        if (!Cache->size())
          return false;
      }

      auto DAG = SyncPipe[0].DAG;
      // Does the previous DS_WRITE share a V_PERM predecessor with this
      // VMEM_READ
      return llvm::any_of(*Cache, [&SU, &DAG](SUnit *Elt) {
        return DAG->IsReachable(const_cast<SUnit *>(SU), Elt);
      });
    }
    SharesPredWithPrevNthGroup(unsigned Distance, const SIInstrInfo *TII,
                               unsigned SGID, bool NeedsCache = false)
        : InstructionRule(TII, SGID, NeedsCache), Distance(Distance) {}
  };

public:
  bool applyIGLPStrategy(
      DenseMap<int, SUnitsToCandidateSGsMap> &SyncedInstrs,
      DenseMap<int, SmallVector<SchedGroup, 4>> &SyncedSchedGroups,
      AMDGPU::SchedulingPhase Phase) override;

  bool shouldApplyStrategy(ScheduleDAGInstrs *DAG,
                           AMDGPU::SchedulingPhase Phase) override {
    return true;
  }

  MFMASmallGemmSingleWaveOpt(ScheduleDAGInstrs *DAG, const SIInstrInfo *TII)
      : IGLPStrategy(DAG, TII) {
    IsBottomUp = false;
  }
};

static unsigned DSWCount = 0;
static unsigned DSWWithPermCount = 0;
static unsigned DSWWithSharedVMEMCount = 0;

bool MFMASmallGemmSingleWaveOpt::applyIGLPStrategy(
    DenseMap<int, SUnitsToCandidateSGsMap> &SyncedInstrs,
    DenseMap<int, SmallVector<SchedGroup, 4>> &SyncedSchedGroups,
    AMDGPU::SchedulingPhase Phase) {
  unsigned MFMACount = 0;
  unsigned DSRCount = 0;

  bool IsInitial = Phase == AMDGPU::SchedulingPhase::Initial;

  assert((!IsInitial || (DSWCount == 0 && DSWWithPermCount == 0 &&
                         DSWWithSharedVMEMCount == 0)) &&
         "DSWCounters should be zero in pre-RA scheduling!");
  SmallVector<SUnit *, 6> DSWithPerms;
  for (auto &SU : DAG->SUnits) {
    auto I = SU.getInstr();
    if (TII->isMFMAorWMMA(*I))
      ++MFMACount;
    else if (TII->isDS(*I)) {
      if (I->mayLoad())
        ++DSRCount;
      else if (I->mayStore() && IsInitial) {
        ++DSWCount;
        for (auto Pred : SU.Preds) {
          if (Pred.getSUnit()->getInstr()->getOpcode() ==
              AMDGPU::V_PERM_B32_e64) {
            DSWithPerms.push_back(&SU);
            break;
          }
        }
      }
    }
  }

  if (IsInitial) {
    DSWWithPermCount = DSWithPerms.size();
    auto I = DSWithPerms.begin();
    auto E = DSWithPerms.end();

    // Get the count of DS_WRITES with V_PERM predecessors which
    // have loop carried dependencies (WAR) on the same VMEM_READs.
    // We consider partial overlap as a miss -- in other words,
    // for a given DS_W, we only consider another DS_W as matching
    // if there is a corresponding (in terms of the VMEM_R it uses) V_PERM pred
    // for every V_PERM pred of this DS_W.
    DenseMap<MachineInstr *, SUnit *> VMEMLookup;
    SmallVector<SUnit *, 6> Counted;
    for (; I != E; I++) {
      SUnit *Cand = nullptr;
      bool MissedAny = false;
      for (auto &Pred : (*I)->Preds) {
        if (Pred.getSUnit()->getInstr()->getOpcode() != AMDGPU::V_PERM_B32_e64)
          continue;

        if (Cand && llvm::is_contained(Counted, Cand))
          break;

        for (auto &Succ : Pred.getSUnit()->Succs) {
          auto MI = Succ.getSUnit()->getInstr();
          if (!TII->isVMEM(*MI) || !MI->mayLoad())
            continue;

          if (MissedAny || !VMEMLookup.size()) {
            MissedAny = true;
            VMEMLookup[MI] = *I;
            continue;
          }

          if (!VMEMLookup.contains(MI)) {
            MissedAny = true;
            VMEMLookup[MI] = *I;
            continue;
          }

          Cand = VMEMLookup[MI];
          if (llvm::is_contained(Counted, Cand)) {
            MissedAny = true;
            break;
          }
        }
      }
      if (!MissedAny && Cand) {
        DSWWithSharedVMEMCount += 2;
        Counted.push_back(Cand);
        Counted.push_back(*I);
      }
    }
  }

  assert(DSWWithSharedVMEMCount <= DSWWithPermCount);
  SchedGroup *SG;
  unsigned PipelineSyncID = 0;
  // For kernels with V_PERM, there are enough VALU to mix in between MFMAs
  if (DSWWithPermCount) {
    for (unsigned I = 0; I < MFMACount; I++) {
      SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
          SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
      SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

      SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
          SchedGroupMask::VALU, 2, PipelineSyncID, DAG, TII);
      SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
    }
  }

  PipelineSyncID = 1;
  // Phase 1: Break up DS_READ and MFMA clusters.
  // First DS_READ to make ready initial MFMA, then interleave MFMA with DS_READ
  // prefetch

  // Make ready initial MFMA
  SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
      SchedGroupMask::DS_READ, 4, PipelineSyncID, DAG, TII);
  SG->addRule(std::make_shared<EnablesInitialMFMA>(TII, SG->getSGID(), true));
  SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

  SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
      SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
  SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

  // Interleave MFMA with DS_READ prefetch
  for (unsigned I = 0; I < DSRCount - 4; ++I) {
    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::DS_READ, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
  }

  // Phase 2a: Loop carried dependency with V_PERM
  // Schedule VPerm & DS_WRITE as closely as possible to the VMEM_READ they
  // depend on. Interleave MFMA to keep XDL unit busy throughout.
  for (unsigned I = 0; I < DSWWithPermCount - DSWWithSharedVMEMCount; ++I) {
    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::VALU, 4, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<IsPermForDSW>(TII, SG->getSGID(), true));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::DS_WRITE, 1, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<IsSuccOfPrevGroup>(TII, SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::VMEM_READ, 4, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<SharesPredWithPrevNthGroup>(
        1, TII, SG->getSGID(), true));
    SG->addRule(std::make_shared<VMEMSize>(TII, SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::VMEM_READ, 4, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<SharesPredWithPrevNthGroup>(
        3, TII, SG->getSGID(), true));
    SG->addRule(std::make_shared<VMEMSize>(TII, SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
  }

  // Phase 2b: Loop carried dependency without V_PERM
  // Schedule DS_WRITE as closely as possible to the VMEM_READ they depend on.
  // Interleave MFMA to keep XDL unit busy throughout.
  for (unsigned I = 0; I < DSWCount - DSWWithPermCount; I++) {
    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::DS_WRITE, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::VMEM_READ, 4, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<VMEMSize>(TII, SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
  }

  // Phase 2c: Loop carried dependency with V_PERM, VMEM_READs are
  // ultimately used by two DS_WRITE
  // Schedule VPerm & DS_WRITE as closely as possible to the VMEM_READ they
  // depend on. Interleave MFMA to keep XDL unit busy throughout.

  for (unsigned I = 0; I < DSWWithSharedVMEMCount; ++I) {
    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::VALU, 4, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<IsPermForDSW>(TII, SG->getSGID(), true));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::DS_WRITE, 1, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<IsSuccOfPrevGroup>(TII, SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::VALU, 4, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<IsPermForDSW>(TII, SG->getSGID(), true));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::DS_WRITE, 1, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<IsSuccOfPrevGroup>(TII, SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::VMEM_READ, 4, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<SharesPredWithPrevNthGroup>(
        2, TII, SG->getSGID(), true));
    SG->addRule(std::make_shared<VMEMSize>(TII, SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::VMEM_READ, 4, PipelineSyncID, DAG, TII);
    SG->addRule(std::make_shared<SharesPredWithPrevNthGroup>(
        4, TII, SG->getSGID(), true));
    SG->addRule(std::make_shared<VMEMSize>(TII, SG->getSGID()));
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);

    SG = &SyncedSchedGroups[PipelineSyncID].emplace_back(
        SchedGroupMask::MFMA, 1, PipelineSyncID, DAG, TII);
    SG->initSchedGroup(SyncedInstrs[SG->getSyncID()]);
  }

  return true;
}

static std::unique_ptr<IGLPStrategy>
createIGLPStrategy(IGLPStrategyID ID, ScheduleDAGInstrs *DAG,
                   const SIInstrInfo *TII) {
  switch (ID) {
  case MFMASmallGemmOptID:
    return std::make_unique<MFMASmallGemmOpt>(DAG, TII);
  case MFMASmallGemmSingleWaveOptID:
    return std::make_unique<MFMASmallGemmSingleWaveOpt>(DAG, TII);
  case MFMAExpInterleave:
    return std::make_unique<MFMAExpInterleaveOpt>(DAG, TII);
  }

  llvm_unreachable("Unknown IGLPStrategyID");
}

class IGroupLPDAGMutation : public ScheduleDAGMutation {
private:
  const SIInstrInfo *TII;

  ScheduleDAGMI *DAG;

  // Organize lists of SchedGroups by their SyncID. SchedGroups /
  // SCHED_GROUP_BARRIERs with different SyncIDs will have no edges added
  // between then.
  DenseMap<int, SmallVector<SchedGroup, 4>> SyncedSchedGroups;

  // Used to track instructions that can be mapped to multiple sched groups
  DenseMap<int, SUnitsToCandidateSGsMap> SyncedInstrs;

  // Add DAG edges that enforce SCHED_BARRIER ordering.
  void addSchedBarrierEdges(SUnit &SU);

  // Use a SCHED_BARRIER's mask to identify instruction SchedGroups that should
  // not be reordered accross the SCHED_BARRIER. This is used for the base
  // SCHED_BARRIER, and not SCHED_GROUP_BARRIER. The difference is that
  // SCHED_BARRIER will always block all instructions that can be classified
  // into a particular SchedClass, whereas SCHED_GROUP_BARRIER has a fixed size
  // and may only synchronize with some SchedGroups. Returns the inverse of
  // Mask. SCHED_BARRIER's mask describes which instruction types should be
  // allowed to be scheduled across it. Invert the mask to get the
  // SchedGroupMask of instructions that should be barred.
  SchedGroupMask invertSchedBarrierMask(SchedGroupMask Mask) const;

  // Create SchedGroups for a SCHED_GROUP_BARRIER.
  void initSchedGroupBarrierPipelineStage(
      std::vector<SUnit>::reverse_iterator RIter);

  bool initIGLPOpt(SUnit &SU);

public:
  void apply(ScheduleDAGInstrs *DAGInstrs) override;

  // The order in which the PipelineSolver should process the candidate
  // SchedGroup for a PipelineInstr. BOTTOM_UP will try to add SUs to the last
  // created SchedGroup first, and will consider that as the ultimate
  // predecessor group when linking. TOP_DOWN instead links and processes the
  // first created SchedGroup first.
  bool IsBottomUp = true;

  // The scheduling phase this application of IGLP corresponds with.
  AMDGPU::SchedulingPhase Phase = AMDGPU::SchedulingPhase::Initial;

  IGroupLPDAGMutation() = default;
  IGroupLPDAGMutation(AMDGPU::SchedulingPhase Phase) : Phase(Phase) {}
};

unsigned SchedGroup::NumSchedGroups = 0;

bool SchedGroup::tryAddEdge(SUnit *A, SUnit *B) {
  if (A != B && DAG->canAddEdge(B, A)) {
    DAG->addEdge(B, SDep(A, SDep::Artificial));
    return true;
  }
  return false;
}

bool SchedGroup::canAddMI(const MachineInstr &MI) const {
  bool Result = false;
  if (MI.isMetaInstruction())
    Result = false;

  else if (((SGMask & SchedGroupMask::ALU) != SchedGroupMask::NONE) &&
           (TII->isVALU(MI) || TII->isMFMAorWMMA(MI) || TII->isSALU(MI) ||
            TII->isTRANS(MI)))
    Result = true;

  else if (((SGMask & SchedGroupMask::VALU) != SchedGroupMask::NONE) &&
           TII->isVALU(MI) && !TII->isMFMAorWMMA(MI) && !TII->isTRANS(MI))
    Result = true;

  else if (((SGMask & SchedGroupMask::SALU) != SchedGroupMask::NONE) &&
           TII->isSALU(MI))
    Result = true;

  else if (((SGMask & SchedGroupMask::MFMA) != SchedGroupMask::NONE) &&
           TII->isMFMAorWMMA(MI))
    Result = true;

  else if (((SGMask & SchedGroupMask::VMEM) != SchedGroupMask::NONE) &&
           (TII->isVMEM(MI) || (TII->isFLAT(MI) && !TII->isDS(MI))))
    Result = true;

  else if (((SGMask & SchedGroupMask::VMEM_READ) != SchedGroupMask::NONE) &&
           MI.mayLoad() &&
           (TII->isVMEM(MI) || (TII->isFLAT(MI) && !TII->isDS(MI))))
    Result = true;

  else if (((SGMask & SchedGroupMask::VMEM_WRITE) != SchedGroupMask::NONE) &&
           MI.mayStore() &&
           (TII->isVMEM(MI) || (TII->isFLAT(MI) && !TII->isDS(MI))))
    Result = true;

  else if (((SGMask & SchedGroupMask::DS) != SchedGroupMask::NONE) &&
           TII->isDS(MI))
    Result = true;

  else if (((SGMask & SchedGroupMask::DS_READ) != SchedGroupMask::NONE) &&
           MI.mayLoad() && TII->isDS(MI))
    Result = true;

  else if (((SGMask & SchedGroupMask::DS_WRITE) != SchedGroupMask::NONE) &&
           MI.mayStore() && TII->isDS(MI))
    Result = true;

  else if (((SGMask & SchedGroupMask::TRANS) != SchedGroupMask::NONE) &&
           TII->isTRANS(MI))
    Result = true;

  LLVM_DEBUG(
      dbgs() << "For SchedGroup with mask " << format_hex((int)SGMask, 10, true)
             << (Result ? " could classify " : " unable to classify ") << MI);

  return Result;
}

int SchedGroup::link(SUnit &SU, bool MakePred,
                     std::vector<std::pair<SUnit *, SUnit *>> &AddedEdges) {
  int MissedEdges = 0;
  for (auto *A : Collection) {
    SUnit *B = &SU;
    if (A == B || A->getInstr()->getOpcode() == AMDGPU::SCHED_GROUP_BARRIER)
      continue;
    if (MakePred)
      std::swap(A, B);

    if (DAG->IsReachable(B, A))
      continue;

    // tryAddEdge returns false if there is a dependency that makes adding
    // the A->B edge impossible, otherwise it returns true;
    bool Added = tryAddEdge(A, B);
    if (Added)
      AddedEdges.emplace_back(A, B);
    else
      ++MissedEdges;
  }

  return MissedEdges;
}

void SchedGroup::link(SUnit &SU, bool MakePred) {
  for (auto *A : Collection) {
    SUnit *B = &SU;
    if (A->getInstr()->getOpcode() == AMDGPU::SCHED_GROUP_BARRIER)
      continue;
    if (MakePred)
      std::swap(A, B);

    tryAddEdge(A, B);
  }
}

void SchedGroup::link(SUnit &SU,
                      function_ref<bool(const SUnit *A, const SUnit *B)> P) {
  for (auto *A : Collection) {
    SUnit *B = &SU;
    if (P(A, B))
      std::swap(A, B);

    tryAddEdge(A, B);
  }
}

void SchedGroup::link(SchedGroup &OtherGroup) {
  for (auto *B : OtherGroup.Collection)
    link(*B);
}

bool SchedGroup::canAddSU(SUnit &SU) const {
  MachineInstr &MI = *SU.getInstr();
  if (MI.getOpcode() != TargetOpcode::BUNDLE)
    return canAddMI(MI);

  // Special case for bundled MIs.
  const MachineBasicBlock *MBB = MI.getParent();
  MachineBasicBlock::instr_iterator B = MI.getIterator(), E = ++B;
  while (E != MBB->end() && E->isBundledWithPred())
    ++E;

  // Return true if all of the bundled MIs can be added to this group.
  return std::all_of(B, E, [this](MachineInstr &MI) { return canAddMI(MI); });
}

void SchedGroup::initSchedGroup() {
  for (auto &SU : DAG->SUnits) {
    if (isFull())
      break;

    if (canAddSU(SU))
      add(SU);
  }
}

void SchedGroup::initSchedGroup(std::vector<SUnit>::reverse_iterator RIter,
                                SUnitsToCandidateSGsMap &SyncedInstrs) {
  SUnit &InitSU = *RIter;
  for (auto E = DAG->SUnits.rend(); RIter != E; ++RIter) {
    auto &SU = *RIter;
    if (isFull())
      break;

    if (canAddSU(SU))
      SyncedInstrs[&SU].push_back(SGID);
  }

  add(InitSU);
  assert(MaxSize);
  (*MaxSize)++;
}

void SchedGroup::initSchedGroup(SUnitsToCandidateSGsMap &SyncedInstrs) {
  auto I = DAG->SUnits.rbegin();
  auto E = DAG->SUnits.rend();
  for (; I != E; ++I) {
    auto &SU = *I;
    if (isFull())
      break;
    if (canAddSU(SU))
      SyncedInstrs[&SU].push_back(SGID);
  }
}

void IGroupLPDAGMutation::apply(ScheduleDAGInstrs *DAGInstrs) {
  const TargetSchedModel *TSchedModel = DAGInstrs->getSchedModel();
  if (!TSchedModel || DAGInstrs->SUnits.empty())
    return;

  LLVM_DEBUG(dbgs() << "Applying IGroupLPDAGMutation...\n");
  const GCNSubtarget &ST = DAGInstrs->MF.getSubtarget<GCNSubtarget>();
  TII = ST.getInstrInfo();
  DAG = static_cast<ScheduleDAGMI *>(DAGInstrs);
  SyncedSchedGroups.clear();
  SyncedInstrs.clear();
  bool FoundSB = false;
  bool FoundIGLP = false;
  bool ShouldApplyIGLP = false;
  for (auto R = DAG->SUnits.rbegin(), E = DAG->SUnits.rend(); R != E; ++R) {
    unsigned Opc = R->getInstr()->getOpcode();
    // SCHED_[GROUP_]BARRIER and IGLP are mutually exclusive.
    if (Opc == AMDGPU::SCHED_BARRIER) {
      addSchedBarrierEdges(*R);
      FoundSB = true;
    } else if (Opc == AMDGPU::SCHED_GROUP_BARRIER) {
      initSchedGroupBarrierPipelineStage(R);
      FoundSB = true;
    } else if (Opc == AMDGPU::IGLP_OPT) {
      resetEdges(*R, DAG);
      if (!FoundSB && !FoundIGLP) {
        FoundIGLP = true;
        ShouldApplyIGLP = initIGLPOpt(*R);
      }
    }
  }

  if (FoundSB || (FoundIGLP && ShouldApplyIGLP)) {
    PipelineSolver PS(SyncedSchedGroups, SyncedInstrs, DAG, IsBottomUp);
    // PipelineSolver performs the mutation by adding the edges it
    // determined as the best
    PS.solve();
    return;
  }
}

void IGroupLPDAGMutation::addSchedBarrierEdges(SUnit &SchedBarrier) {
  MachineInstr &MI = *SchedBarrier.getInstr();
  assert(MI.getOpcode() == AMDGPU::SCHED_BARRIER);
  // Remove all existing edges from the SCHED_BARRIER that were added due to the
  // instruction having side effects.
  resetEdges(SchedBarrier, DAG);
  LLVM_DEBUG(dbgs() << "Building SchedGroup for SchedBarrier with Mask: "
                    << MI.getOperand(0).getImm() << "\n");
  auto InvertedMask =
      invertSchedBarrierMask((SchedGroupMask)MI.getOperand(0).getImm());
  SchedGroup SG(InvertedMask, std::nullopt, DAG, TII);
  SG.initSchedGroup();

  // Preserve original instruction ordering relative to the SCHED_BARRIER.
  SG.link(
      SchedBarrier,
      (function_ref<bool(const SUnit *A, const SUnit *B)>)[](
          const SUnit *A, const SUnit *B) { return A->NodeNum > B->NodeNum; });
}

SchedGroupMask
IGroupLPDAGMutation::invertSchedBarrierMask(SchedGroupMask Mask) const {
  // Invert mask and erase bits for types of instructions that are implied to be
  // allowed past the SCHED_BARRIER.
  SchedGroupMask InvertedMask = ~Mask;

  // ALU implies VALU, SALU, MFMA, TRANS.
  if ((InvertedMask & SchedGroupMask::ALU) == SchedGroupMask::NONE)
    InvertedMask &= ~SchedGroupMask::VALU & ~SchedGroupMask::SALU &
                    ~SchedGroupMask::MFMA & ~SchedGroupMask::TRANS;
  // VALU, SALU, MFMA, TRANS implies ALU.
  else if ((InvertedMask & SchedGroupMask::VALU) == SchedGroupMask::NONE ||
           (InvertedMask & SchedGroupMask::SALU) == SchedGroupMask::NONE ||
           (InvertedMask & SchedGroupMask::MFMA) == SchedGroupMask::NONE ||
           (InvertedMask & SchedGroupMask::TRANS) == SchedGroupMask::NONE)
    InvertedMask &= ~SchedGroupMask::ALU;

  // VMEM implies VMEM_READ, VMEM_WRITE.
  if ((InvertedMask & SchedGroupMask::VMEM) == SchedGroupMask::NONE)
    InvertedMask &= ~SchedGroupMask::VMEM_READ & ~SchedGroupMask::VMEM_WRITE;
  // VMEM_READ, VMEM_WRITE implies VMEM.
  else if ((InvertedMask & SchedGroupMask::VMEM_READ) == SchedGroupMask::NONE ||
           (InvertedMask & SchedGroupMask::VMEM_WRITE) == SchedGroupMask::NONE)
    InvertedMask &= ~SchedGroupMask::VMEM;

  // DS implies DS_READ, DS_WRITE.
  if ((InvertedMask & SchedGroupMask::DS) == SchedGroupMask::NONE)
    InvertedMask &= ~SchedGroupMask::DS_READ & ~SchedGroupMask::DS_WRITE;
  // DS_READ, DS_WRITE implies DS.
  else if ((InvertedMask & SchedGroupMask::DS_READ) == SchedGroupMask::NONE ||
           (InvertedMask & SchedGroupMask::DS_WRITE) == SchedGroupMask::NONE)
    InvertedMask &= ~SchedGroupMask::DS;

  LLVM_DEBUG(dbgs() << "After Inverting, SchedGroup Mask: " << (int)InvertedMask
                    << "\n");

  return InvertedMask;
}

void IGroupLPDAGMutation::initSchedGroupBarrierPipelineStage(
    std::vector<SUnit>::reverse_iterator RIter) {
  // Remove all existing edges from the SCHED_GROUP_BARRIER that were added due
  // to the instruction having side effects.
  resetEdges(*RIter, DAG);
  MachineInstr &SGB = *RIter->getInstr();
  assert(SGB.getOpcode() == AMDGPU::SCHED_GROUP_BARRIER);
  int32_t SGMask = SGB.getOperand(0).getImm();
  int32_t Size = SGB.getOperand(1).getImm();
  int32_t SyncID = SGB.getOperand(2).getImm();

  auto &SG = SyncedSchedGroups[SyncID].emplace_back((SchedGroupMask)SGMask,
                                                    Size, SyncID, DAG, TII);

  SG.initSchedGroup(RIter, SyncedInstrs[SG.getSyncID()]);
}

bool IGroupLPDAGMutation::initIGLPOpt(SUnit &SU) {
  IGLPStrategyID StrategyID =
      (IGLPStrategyID)SU.getInstr()->getOperand(0).getImm();
  auto S = createIGLPStrategy(StrategyID, DAG, TII);
  if (!S->shouldApplyStrategy(DAG, Phase))
    return false;

  IsBottomUp = S->IsBottomUp;
  return S->applyIGLPStrategy(SyncedInstrs, SyncedSchedGroups, Phase);
}

} // namespace

namespace llvm {

/// \p Phase specifes whether or not this is a reentry into the
/// IGroupLPDAGMutation. Since there may be multiple scheduling passes on the
/// same scheduling region (e.g. pre and post-RA scheduling / multiple
/// scheduling "phases"), we can reenter this mutation framework more than once
/// for a given region.
std::unique_ptr<ScheduleDAGMutation>
createIGroupLPDAGMutation(AMDGPU::SchedulingPhase Phase) {
  return std::make_unique<IGroupLPDAGMutation>(Phase);
}

} // end namespace llvm
