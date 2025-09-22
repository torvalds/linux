//===- RegAllocPBQP.cpp ---- PBQP Register Allocator ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a Partitioned Boolean Quadratic Programming (PBQP) based
// register allocator for LLVM. This allocator works by constructing a PBQP
// problem representing the register allocation problem under consideration,
// solving this using a PBQP solver, and mapping the solution back to a
// register assignment. If any variables are selected for spilling then spill
// code is inserted and the process repeated.
//
// The PBQP solver (pbqp.c) provided for this allocator uses a heuristic tuned
// for register allocation. For more information on PBQP for register
// allocation, see the following papers:
//
//   (1) Hames, L. and Scholz, B. 2006. Nearly optimal register allocation with
//   PBQP. In Proceedings of the 7th Joint Modular Languages Conference
//   (JMLC'06). LNCS, vol. 4228. Springer, New York, NY, USA. 346-361.
//
//   (2) Scholz, B., Eckstein, E. 2002. Register allocation for irregular
//   architectures. In Proceedings of the Joint Conference on Languages,
//   Compilers and Tools for Embedded Systems (LCTES'02), ACM Press, New York,
//   NY, USA, 139-148.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/RegAllocPBQP.h"
#include "RegisterCoalescer.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/CodeGen/CalcSpillWeights.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/LiveStacks.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PBQP/Graph.h"
#include "llvm/CodeGen/PBQP/Math.h"
#include "llvm/CodeGen/PBQP/Solution.h"
#include "llvm/CodeGen/PBQPRAConstraint.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/Spiller.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Printable.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "regalloc"

static RegisterRegAlloc
RegisterPBQPRepAlloc("pbqp", "PBQP register allocator",
                       createDefaultPBQPRegisterAllocator);

static cl::opt<bool>
PBQPCoalescing("pbqp-coalescing",
                cl::desc("Attempt coalescing during PBQP register allocation."),
                cl::init(false), cl::Hidden);

#ifndef NDEBUG
static cl::opt<bool>
PBQPDumpGraphs("pbqp-dump-graphs",
               cl::desc("Dump graphs for each function/round in the compilation unit."),
               cl::init(false), cl::Hidden);
#endif

namespace {

///
/// PBQP based allocators solve the register allocation problem by mapping
/// register allocation problems to Partitioned Boolean Quadratic
/// Programming problems.
class RegAllocPBQP : public MachineFunctionPass {
public:
  static char ID;

  /// Construct a PBQP register allocator.
  RegAllocPBQP(char *cPassID = nullptr)
      : MachineFunctionPass(ID), customPassID(cPassID) {
    initializeSlotIndexesWrapperPassPass(*PassRegistry::getPassRegistry());
    initializeLiveIntervalsWrapperPassPass(*PassRegistry::getPassRegistry());
    initializeLiveStacksPass(*PassRegistry::getPassRegistry());
    initializeVirtRegMapPass(*PassRegistry::getPassRegistry());
  }

  /// Return the pass name.
  StringRef getPassName() const override { return "PBQP Register Allocator"; }

  /// PBQP analysis usage.
  void getAnalysisUsage(AnalysisUsage &au) const override;

  /// Perform register allocation
  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoPHIs);
  }

  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties().set(
      MachineFunctionProperties::Property::IsSSA);
  }

private:
  using RegSet = std::set<Register>;

  char *customPassID;

  RegSet VRegsToAlloc, EmptyIntervalVRegs;

  /// Inst which is a def of an original reg and whose defs are already all
  /// dead after remat is saved in DeadRemats. The deletion of such inst is
  /// postponed till all the allocations are done, so its remat expr is
  /// always available for the remat of all the siblings of the original reg.
  SmallPtrSet<MachineInstr *, 32> DeadRemats;

  /// Finds the initial set of vreg intervals to allocate.
  void findVRegIntervalsToAlloc(const MachineFunction &MF, LiveIntervals &LIS);

  /// Constructs an initial graph.
  void initializeGraph(PBQPRAGraph &G, VirtRegMap &VRM, Spiller &VRegSpiller);

  /// Spill the given VReg.
  void spillVReg(Register VReg, SmallVectorImpl<Register> &NewIntervals,
                 MachineFunction &MF, LiveIntervals &LIS, VirtRegMap &VRM,
                 Spiller &VRegSpiller);

  /// Given a solved PBQP problem maps this solution back to a register
  /// assignment.
  bool mapPBQPToRegAlloc(const PBQPRAGraph &G,
                         const PBQP::Solution &Solution,
                         VirtRegMap &VRM,
                         Spiller &VRegSpiller);

  /// Postprocessing before final spilling. Sets basic block "live in"
  /// variables.
  void finalizeAlloc(MachineFunction &MF, LiveIntervals &LIS,
                     VirtRegMap &VRM) const;

  void postOptimization(Spiller &VRegSpiller, LiveIntervals &LIS);
};

char RegAllocPBQP::ID = 0;

/// Set spill costs for each node in the PBQP reg-alloc graph.
class SpillCosts : public PBQPRAConstraint {
public:
  void apply(PBQPRAGraph &G) override {
    LiveIntervals &LIS = G.getMetadata().LIS;

    // A minimum spill costs, so that register constraints can be set
    // without normalization in the [0.0:MinSpillCost( interval.
    const PBQP::PBQPNum MinSpillCost = 10.0;

    for (auto NId : G.nodeIds()) {
      PBQP::PBQPNum SpillCost =
          LIS.getInterval(G.getNodeMetadata(NId).getVReg()).weight();
      if (SpillCost == 0.0)
        SpillCost = std::numeric_limits<PBQP::PBQPNum>::min();
      else
        SpillCost += MinSpillCost;
      PBQPRAGraph::RawVector NodeCosts(G.getNodeCosts(NId));
      NodeCosts[PBQP::RegAlloc::getSpillOptionIdx()] = SpillCost;
      G.setNodeCosts(NId, std::move(NodeCosts));
    }
  }
};

/// Add interference edges between overlapping vregs.
class Interference : public PBQPRAConstraint {
private:
  using AllowedRegVecPtr = const PBQP::RegAlloc::AllowedRegVector *;
  using IKey = std::pair<AllowedRegVecPtr, AllowedRegVecPtr>;
  using IMatrixCache = DenseMap<IKey, PBQPRAGraph::MatrixPtr>;
  using DisjointAllowedRegsCache = DenseSet<IKey>;
  using IEdgeKey = std::pair<PBQP::GraphBase::NodeId, PBQP::GraphBase::NodeId>;
  using IEdgeCache = DenseSet<IEdgeKey>;

  bool haveDisjointAllowedRegs(const PBQPRAGraph &G, PBQPRAGraph::NodeId NId,
                               PBQPRAGraph::NodeId MId,
                               const DisjointAllowedRegsCache &D) const {
    const auto *NRegs = &G.getNodeMetadata(NId).getAllowedRegs();
    const auto *MRegs = &G.getNodeMetadata(MId).getAllowedRegs();

    if (NRegs == MRegs)
      return false;

    if (NRegs < MRegs)
      return D.contains(IKey(NRegs, MRegs));

    return D.contains(IKey(MRegs, NRegs));
  }

  void setDisjointAllowedRegs(const PBQPRAGraph &G, PBQPRAGraph::NodeId NId,
                              PBQPRAGraph::NodeId MId,
                              DisjointAllowedRegsCache &D) {
    const auto *NRegs = &G.getNodeMetadata(NId).getAllowedRegs();
    const auto *MRegs = &G.getNodeMetadata(MId).getAllowedRegs();

    assert(NRegs != MRegs && "AllowedRegs can not be disjoint with itself");

    if (NRegs < MRegs)
      D.insert(IKey(NRegs, MRegs));
    else
      D.insert(IKey(MRegs, NRegs));
  }

  // Holds (Interval, CurrentSegmentID, and NodeId). The first two are required
  // for the fast interference graph construction algorithm. The last is there
  // to save us from looking up node ids via the VRegToNode map in the graph
  // metadata.
  using IntervalInfo =
      std::tuple<LiveInterval*, size_t, PBQP::GraphBase::NodeId>;

  static SlotIndex getStartPoint(const IntervalInfo &I) {
    return std::get<0>(I)->segments[std::get<1>(I)].start;
  }

  static SlotIndex getEndPoint(const IntervalInfo &I) {
    return std::get<0>(I)->segments[std::get<1>(I)].end;
  }

  static PBQP::GraphBase::NodeId getNodeId(const IntervalInfo &I) {
    return std::get<2>(I);
  }

  static bool lowestStartPoint(const IntervalInfo &I1,
                               const IntervalInfo &I2) {
    // Condition reversed because priority queue has the *highest* element at
    // the front, rather than the lowest.
    return getStartPoint(I1) > getStartPoint(I2);
  }

  static bool lowestEndPoint(const IntervalInfo &I1,
                             const IntervalInfo &I2) {
    SlotIndex E1 = getEndPoint(I1);
    SlotIndex E2 = getEndPoint(I2);

    if (E1 < E2)
      return true;

    if (E1 > E2)
      return false;

    // If two intervals end at the same point, we need a way to break the tie or
    // the set will assume they're actually equal and refuse to insert a
    // "duplicate". Just compare the vregs - fast and guaranteed unique.
    return std::get<0>(I1)->reg() < std::get<0>(I2)->reg();
  }

  static bool isAtLastSegment(const IntervalInfo &I) {
    return std::get<1>(I) == std::get<0>(I)->size() - 1;
  }

  static IntervalInfo nextSegment(const IntervalInfo &I) {
    return std::make_tuple(std::get<0>(I), std::get<1>(I) + 1, std::get<2>(I));
  }

public:
  void apply(PBQPRAGraph &G) override {
    // The following is loosely based on the linear scan algorithm introduced in
    // "Linear Scan Register Allocation" by Poletto and Sarkar. This version
    // isn't linear, because the size of the active set isn't bound by the
    // number of registers, but rather the size of the largest clique in the
    // graph. Still, we expect this to be better than N^2.
    LiveIntervals &LIS = G.getMetadata().LIS;

    // Interferenc matrices are incredibly regular - they're only a function of
    // the allowed sets, so we cache them to avoid the overhead of constructing
    // and uniquing them.
    IMatrixCache C;

    // Finding an edge is expensive in the worst case (O(max_clique(G))). So
    // cache locally edges we have already seen.
    IEdgeCache EC;

    // Cache known disjoint allowed registers pairs
    DisjointAllowedRegsCache D;

    using IntervalSet = std::set<IntervalInfo, decltype(&lowestEndPoint)>;
    using IntervalQueue =
        std::priority_queue<IntervalInfo, std::vector<IntervalInfo>,
                            decltype(&lowestStartPoint)>;
    IntervalSet Active(lowestEndPoint);
    IntervalQueue Inactive(lowestStartPoint);

    // Start by building the inactive set.
    for (auto NId : G.nodeIds()) {
      Register VReg = G.getNodeMetadata(NId).getVReg();
      LiveInterval &LI = LIS.getInterval(VReg);
      assert(!LI.empty() && "PBQP graph contains node for empty interval");
      Inactive.push(std::make_tuple(&LI, 0, NId));
    }

    while (!Inactive.empty()) {
      // Tentatively grab the "next" interval - this choice may be overriden
      // below.
      IntervalInfo Cur = Inactive.top();

      // Retire any active intervals that end before Cur starts.
      IntervalSet::iterator RetireItr = Active.begin();
      while (RetireItr != Active.end() &&
             (getEndPoint(*RetireItr) <= getStartPoint(Cur))) {
        // If this interval has subsequent segments, add the next one to the
        // inactive list.
        if (!isAtLastSegment(*RetireItr))
          Inactive.push(nextSegment(*RetireItr));

        ++RetireItr;
      }
      Active.erase(Active.begin(), RetireItr);

      // One of the newly retired segments may actually start before the
      // Cur segment, so re-grab the front of the inactive list.
      Cur = Inactive.top();
      Inactive.pop();

      // At this point we know that Cur overlaps all active intervals. Add the
      // interference edges.
      PBQP::GraphBase::NodeId NId = getNodeId(Cur);
      for (const auto &A : Active) {
        PBQP::GraphBase::NodeId MId = getNodeId(A);

        // Do not add an edge when the nodes' allowed registers do not
        // intersect: there is obviously no interference.
        if (haveDisjointAllowedRegs(G, NId, MId, D))
          continue;

        // Check that we haven't already added this edge
        IEdgeKey EK(std::min(NId, MId), std::max(NId, MId));
        if (EC.count(EK))
          continue;

        // This is a new edge - add it to the graph.
        if (!createInterferenceEdge(G, NId, MId, C))
          setDisjointAllowedRegs(G, NId, MId, D);
        else
          EC.insert(EK);
      }

      // Finally, add Cur to the Active set.
      Active.insert(Cur);
    }
  }

private:
  // Create an Interference edge and add it to the graph, unless it is
  // a null matrix, meaning the nodes' allowed registers do not have any
  // interference. This case occurs frequently between integer and floating
  // point registers for example.
  // return true iff both nodes interferes.
  bool createInterferenceEdge(PBQPRAGraph &G,
                              PBQPRAGraph::NodeId NId, PBQPRAGraph::NodeId MId,
                              IMatrixCache &C) {
    const TargetRegisterInfo &TRI =
        *G.getMetadata().MF.getSubtarget().getRegisterInfo();
    const auto &NRegs = G.getNodeMetadata(NId).getAllowedRegs();
    const auto &MRegs = G.getNodeMetadata(MId).getAllowedRegs();

    // Try looking the edge costs up in the IMatrixCache first.
    IKey K(&NRegs, &MRegs);
    IMatrixCache::iterator I = C.find(K);
    if (I != C.end()) {
      G.addEdgeBypassingCostAllocator(NId, MId, I->second);
      return true;
    }

    PBQPRAGraph::RawMatrix M(NRegs.size() + 1, MRegs.size() + 1, 0);
    bool NodesInterfere = false;
    for (unsigned I = 0; I != NRegs.size(); ++I) {
      MCRegister PRegN = NRegs[I];
      for (unsigned J = 0; J != MRegs.size(); ++J) {
        MCRegister PRegM = MRegs[J];
        if (TRI.regsOverlap(PRegN, PRegM)) {
          M[I + 1][J + 1] = std::numeric_limits<PBQP::PBQPNum>::infinity();
          NodesInterfere = true;
        }
      }
    }

    if (!NodesInterfere)
      return false;

    PBQPRAGraph::EdgeId EId = G.addEdge(NId, MId, std::move(M));
    C[K] = G.getEdgeCostsPtr(EId);

    return true;
  }
};

class Coalescing : public PBQPRAConstraint {
public:
  void apply(PBQPRAGraph &G) override {
    MachineFunction &MF = G.getMetadata().MF;
    MachineBlockFrequencyInfo &MBFI = G.getMetadata().MBFI;
    CoalescerPair CP(*MF.getSubtarget().getRegisterInfo());

    // Scan the machine function and add a coalescing cost whenever CoalescerPair
    // gives the Ok.
    for (const auto &MBB : MF) {
      for (const auto &MI : MBB) {
        // Skip not-coalescable or already coalesced copies.
        if (!CP.setRegisters(&MI) || CP.getSrcReg() == CP.getDstReg())
          continue;

        Register DstReg = CP.getDstReg();
        Register SrcReg = CP.getSrcReg();

        PBQP::PBQPNum CBenefit = MBFI.getBlockFreqRelativeToEntryBlock(&MBB);

        if (CP.isPhys()) {
          if (!MF.getRegInfo().isAllocatable(DstReg))
            continue;

          PBQPRAGraph::NodeId NId = G.getMetadata().getNodeIdForVReg(SrcReg);

          const PBQPRAGraph::NodeMetadata::AllowedRegVector &Allowed =
            G.getNodeMetadata(NId).getAllowedRegs();

          unsigned PRegOpt = 0;
          while (PRegOpt < Allowed.size() && Allowed[PRegOpt].id() != DstReg)
            ++PRegOpt;

          if (PRegOpt < Allowed.size()) {
            PBQPRAGraph::RawVector NewCosts(G.getNodeCosts(NId));
            NewCosts[PRegOpt + 1] -= CBenefit;
            G.setNodeCosts(NId, std::move(NewCosts));
          }
        } else {
          PBQPRAGraph::NodeId N1Id = G.getMetadata().getNodeIdForVReg(DstReg);
          PBQPRAGraph::NodeId N2Id = G.getMetadata().getNodeIdForVReg(SrcReg);
          const PBQPRAGraph::NodeMetadata::AllowedRegVector *Allowed1 =
            &G.getNodeMetadata(N1Id).getAllowedRegs();
          const PBQPRAGraph::NodeMetadata::AllowedRegVector *Allowed2 =
            &G.getNodeMetadata(N2Id).getAllowedRegs();

          PBQPRAGraph::EdgeId EId = G.findEdge(N1Id, N2Id);
          if (EId == G.invalidEdgeId()) {
            PBQPRAGraph::RawMatrix Costs(Allowed1->size() + 1,
                                         Allowed2->size() + 1, 0);
            addVirtRegCoalesce(Costs, *Allowed1, *Allowed2, CBenefit);
            G.addEdge(N1Id, N2Id, std::move(Costs));
          } else {
            if (G.getEdgeNode1Id(EId) == N2Id) {
              std::swap(N1Id, N2Id);
              std::swap(Allowed1, Allowed2);
            }
            PBQPRAGraph::RawMatrix Costs(G.getEdgeCosts(EId));
            addVirtRegCoalesce(Costs, *Allowed1, *Allowed2, CBenefit);
            G.updateEdgeCosts(EId, std::move(Costs));
          }
        }
      }
    }
  }

private:
  void addVirtRegCoalesce(
                    PBQPRAGraph::RawMatrix &CostMat,
                    const PBQPRAGraph::NodeMetadata::AllowedRegVector &Allowed1,
                    const PBQPRAGraph::NodeMetadata::AllowedRegVector &Allowed2,
                    PBQP::PBQPNum Benefit) {
    assert(CostMat.getRows() == Allowed1.size() + 1 && "Size mismatch.");
    assert(CostMat.getCols() == Allowed2.size() + 1 && "Size mismatch.");
    for (unsigned I = 0; I != Allowed1.size(); ++I) {
      MCRegister PReg1 = Allowed1[I];
      for (unsigned J = 0; J != Allowed2.size(); ++J) {
        MCRegister PReg2 = Allowed2[J];
        if (PReg1 == PReg2)
          CostMat[I + 1][J + 1] -= Benefit;
      }
    }
  }
};

/// PBQP-specific implementation of weight normalization.
class PBQPVirtRegAuxInfo final : public VirtRegAuxInfo {
  float normalize(float UseDefFreq, unsigned Size, unsigned NumInstr) override {
    // All intervals have a spill weight that is mostly proportional to the
    // number of uses, with uses in loops having a bigger weight.
    return NumInstr * VirtRegAuxInfo::normalize(UseDefFreq, Size, 1);
  }

public:
  PBQPVirtRegAuxInfo(MachineFunction &MF, LiveIntervals &LIS, VirtRegMap &VRM,
                     const MachineLoopInfo &Loops,
                     const MachineBlockFrequencyInfo &MBFI)
      : VirtRegAuxInfo(MF, LIS, VRM, Loops, MBFI) {}
};
} // end anonymous namespace

// Out-of-line destructor/anchor for PBQPRAConstraint.
PBQPRAConstraint::~PBQPRAConstraint() = default;

void PBQPRAConstraint::anchor() {}

void PBQPRAConstraintList::anchor() {}

void RegAllocPBQP::getAnalysisUsage(AnalysisUsage &au) const {
  au.setPreservesCFG();
  au.addRequired<AAResultsWrapperPass>();
  au.addPreserved<AAResultsWrapperPass>();
  au.addRequired<SlotIndexesWrapperPass>();
  au.addPreserved<SlotIndexesWrapperPass>();
  au.addRequired<LiveIntervalsWrapperPass>();
  au.addPreserved<LiveIntervalsWrapperPass>();
  //au.addRequiredID(SplitCriticalEdgesID);
  if (customPassID)
    au.addRequiredID(*customPassID);
  au.addRequired<LiveStacks>();
  au.addPreserved<LiveStacks>();
  au.addRequired<MachineBlockFrequencyInfoWrapperPass>();
  au.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
  au.addRequired<MachineLoopInfoWrapperPass>();
  au.addPreserved<MachineLoopInfoWrapperPass>();
  au.addRequired<MachineDominatorTreeWrapperPass>();
  au.addPreserved<MachineDominatorTreeWrapperPass>();
  au.addRequired<VirtRegMap>();
  au.addPreserved<VirtRegMap>();
  MachineFunctionPass::getAnalysisUsage(au);
}

void RegAllocPBQP::findVRegIntervalsToAlloc(const MachineFunction &MF,
                                            LiveIntervals &LIS) {
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  // Iterate over all live ranges.
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    Register Reg = Register::index2VirtReg(I);
    if (MRI.reg_nodbg_empty(Reg))
      continue;
    VRegsToAlloc.insert(Reg);
  }
}

static bool isACalleeSavedRegister(MCRegister Reg,
                                   const TargetRegisterInfo &TRI,
                                   const MachineFunction &MF) {
  const MCPhysReg *CSR = MF.getRegInfo().getCalleeSavedRegs();
  for (unsigned i = 0; CSR[i] != 0; ++i)
    if (TRI.regsOverlap(Reg, CSR[i]))
      return true;
  return false;
}

void RegAllocPBQP::initializeGraph(PBQPRAGraph &G, VirtRegMap &VRM,
                                   Spiller &VRegSpiller) {
  MachineFunction &MF = G.getMetadata().MF;

  LiveIntervals &LIS = G.getMetadata().LIS;
  const MachineRegisterInfo &MRI = G.getMetadata().MF.getRegInfo();
  const TargetRegisterInfo &TRI =
      *G.getMetadata().MF.getSubtarget().getRegisterInfo();

  std::vector<Register> Worklist(VRegsToAlloc.begin(), VRegsToAlloc.end());

  std::map<Register, std::vector<MCRegister>> VRegAllowedMap;

  while (!Worklist.empty()) {
    Register VReg = Worklist.back();
    Worklist.pop_back();

    LiveInterval &VRegLI = LIS.getInterval(VReg);

    // If this is an empty interval move it to the EmptyIntervalVRegs set then
    // continue.
    if (VRegLI.empty()) {
      EmptyIntervalVRegs.insert(VRegLI.reg());
      VRegsToAlloc.erase(VRegLI.reg());
      continue;
    }

    const TargetRegisterClass *TRC = MRI.getRegClass(VReg);

    // Record any overlaps with regmask operands.
    BitVector RegMaskOverlaps;
    LIS.checkRegMaskInterference(VRegLI, RegMaskOverlaps);

    // Compute an initial allowed set for the current vreg.
    std::vector<MCRegister> VRegAllowed;
    ArrayRef<MCPhysReg> RawPRegOrder = TRC->getRawAllocationOrder(MF);
    for (MCPhysReg R : RawPRegOrder) {
      MCRegister PReg(R);
      if (MRI.isReserved(PReg))
        continue;

      // vregLI crosses a regmask operand that clobbers preg.
      if (!RegMaskOverlaps.empty() && !RegMaskOverlaps.test(PReg))
        continue;

      // vregLI overlaps fixed regunit interference.
      bool Interference = false;
      for (MCRegUnit Unit : TRI.regunits(PReg)) {
        if (VRegLI.overlaps(LIS.getRegUnit(Unit))) {
          Interference = true;
          break;
        }
      }
      if (Interference)
        continue;

      // preg is usable for this virtual register.
      VRegAllowed.push_back(PReg);
    }

    // Check for vregs that have no allowed registers. These should be
    // pre-spilled and the new vregs added to the worklist.
    if (VRegAllowed.empty()) {
      SmallVector<Register, 8> NewVRegs;
      spillVReg(VReg, NewVRegs, MF, LIS, VRM, VRegSpiller);
      llvm::append_range(Worklist, NewVRegs);
      continue;
    }

    VRegAllowedMap[VReg.id()] = std::move(VRegAllowed);
  }

  for (auto &KV : VRegAllowedMap) {
    auto VReg = KV.first;

    // Move empty intervals to the EmptyIntervalVReg set.
    if (LIS.getInterval(VReg).empty()) {
      EmptyIntervalVRegs.insert(VReg);
      VRegsToAlloc.erase(VReg);
      continue;
    }

    auto &VRegAllowed = KV.second;

    PBQPRAGraph::RawVector NodeCosts(VRegAllowed.size() + 1, 0);

    // Tweak cost of callee saved registers, as using then force spilling and
    // restoring them. This would only happen in the prologue / epilogue though.
    for (unsigned i = 0; i != VRegAllowed.size(); ++i)
      if (isACalleeSavedRegister(VRegAllowed[i], TRI, MF))
        NodeCosts[1 + i] += 1.0;

    PBQPRAGraph::NodeId NId = G.addNode(std::move(NodeCosts));
    G.getNodeMetadata(NId).setVReg(VReg);
    G.getNodeMetadata(NId).setAllowedRegs(
      G.getMetadata().getAllowedRegs(std::move(VRegAllowed)));
    G.getMetadata().setNodeIdForVReg(VReg, NId);
  }
}

void RegAllocPBQP::spillVReg(Register VReg,
                             SmallVectorImpl<Register> &NewIntervals,
                             MachineFunction &MF, LiveIntervals &LIS,
                             VirtRegMap &VRM, Spiller &VRegSpiller) {
  VRegsToAlloc.erase(VReg);
  LiveRangeEdit LRE(&LIS.getInterval(VReg), NewIntervals, MF, LIS, &VRM,
                    nullptr, &DeadRemats);
  VRegSpiller.spill(LRE);

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  (void)TRI;
  LLVM_DEBUG(dbgs() << "VREG " << printReg(VReg, &TRI) << " -> SPILLED (Cost: "
                    << LRE.getParent().weight() << ", New vregs: ");

  // Copy any newly inserted live intervals into the list of regs to
  // allocate.
  for (const Register &R : LRE) {
    const LiveInterval &LI = LIS.getInterval(R);
    assert(!LI.empty() && "Empty spill range.");
    LLVM_DEBUG(dbgs() << printReg(LI.reg(), &TRI) << " ");
    VRegsToAlloc.insert(LI.reg());
  }

  LLVM_DEBUG(dbgs() << ")\n");
}

bool RegAllocPBQP::mapPBQPToRegAlloc(const PBQPRAGraph &G,
                                     const PBQP::Solution &Solution,
                                     VirtRegMap &VRM,
                                     Spiller &VRegSpiller) {
  MachineFunction &MF = G.getMetadata().MF;
  LiveIntervals &LIS = G.getMetadata().LIS;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  (void)TRI;

  // Set to true if we have any spills
  bool AnotherRoundNeeded = false;

  // Clear the existing allocation.
  VRM.clearAllVirt();

  // Iterate over the nodes mapping the PBQP solution to a register
  // assignment.
  for (auto NId : G.nodeIds()) {
    Register VReg = G.getNodeMetadata(NId).getVReg();
    unsigned AllocOpt = Solution.getSelection(NId);

    if (AllocOpt != PBQP::RegAlloc::getSpillOptionIdx()) {
      MCRegister PReg = G.getNodeMetadata(NId).getAllowedRegs()[AllocOpt - 1];
      LLVM_DEBUG(dbgs() << "VREG " << printReg(VReg, &TRI) << " -> "
                        << TRI.getName(PReg) << "\n");
      assert(PReg != 0 && "Invalid preg selected.");
      VRM.assignVirt2Phys(VReg, PReg);
    } else {
      // Spill VReg. If this introduces new intervals we'll need another round
      // of allocation.
      SmallVector<Register, 8> NewVRegs;
      spillVReg(VReg, NewVRegs, MF, LIS, VRM, VRegSpiller);
      AnotherRoundNeeded |= !NewVRegs.empty();
    }
  }

  return !AnotherRoundNeeded;
}

void RegAllocPBQP::finalizeAlloc(MachineFunction &MF,
                                 LiveIntervals &LIS,
                                 VirtRegMap &VRM) const {
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // First allocate registers for the empty intervals.
  for (const Register &R : EmptyIntervalVRegs) {
    LiveInterval &LI = LIS.getInterval(R);

    Register PReg = MRI.getSimpleHint(LI.reg());

    if (PReg == 0) {
      const TargetRegisterClass &RC = *MRI.getRegClass(LI.reg());
      const ArrayRef<MCPhysReg> RawPRegOrder = RC.getRawAllocationOrder(MF);
      for (MCRegister CandidateReg : RawPRegOrder) {
        if (!VRM.getRegInfo().isReserved(CandidateReg)) {
          PReg = CandidateReg;
          break;
        }
      }
      assert(PReg &&
             "No un-reserved physical registers in this register class");
    }

    VRM.assignVirt2Phys(LI.reg(), PReg);
  }
}

void RegAllocPBQP::postOptimization(Spiller &VRegSpiller, LiveIntervals &LIS) {
  VRegSpiller.postOptimization();
  /// Remove dead defs because of rematerialization.
  for (auto *DeadInst : DeadRemats) {
    LIS.RemoveMachineInstrFromMaps(*DeadInst);
    DeadInst->eraseFromParent();
  }
  DeadRemats.clear();
}

bool RegAllocPBQP::runOnMachineFunction(MachineFunction &MF) {
  LiveIntervals &LIS = getAnalysis<LiveIntervalsWrapperPass>().getLIS();
  MachineBlockFrequencyInfo &MBFI =
      getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();

  VirtRegMap &VRM = getAnalysis<VirtRegMap>();

  PBQPVirtRegAuxInfo VRAI(
      MF, LIS, VRM, getAnalysis<MachineLoopInfoWrapperPass>().getLI(), MBFI);
  VRAI.calculateSpillWeightsAndHints();

  // FIXME: we create DefaultVRAI here to match existing behavior pre-passing
  // the VRAI through the spiller to the live range editor. However, it probably
  // makes more sense to pass the PBQP VRAI. The existing behavior had
  // LiveRangeEdit make its own VirtRegAuxInfo object.
  VirtRegAuxInfo DefaultVRAI(
      MF, LIS, VRM, getAnalysis<MachineLoopInfoWrapperPass>().getLI(), MBFI);
  std::unique_ptr<Spiller> VRegSpiller(
      createInlineSpiller(*this, MF, VRM, DefaultVRAI));

  MF.getRegInfo().freezeReservedRegs();

  LLVM_DEBUG(dbgs() << "PBQP Register Allocating for " << MF.getName() << "\n");

  // Allocator main loop:
  //
  // * Map current regalloc problem to a PBQP problem
  // * Solve the PBQP problem
  // * Map the solution back to a register allocation
  // * Spill if necessary
  //
  // This process is continued till no more spills are generated.

  // Find the vreg intervals in need of allocation.
  findVRegIntervalsToAlloc(MF, LIS);

#ifndef NDEBUG
  const Function &F = MF.getFunction();
  std::string FullyQualifiedName =
    F.getParent()->getModuleIdentifier() + "." + F.getName().str();
#endif

  // If there are non-empty intervals allocate them using pbqp.
  if (!VRegsToAlloc.empty()) {
    const TargetSubtargetInfo &Subtarget = MF.getSubtarget();
    std::unique_ptr<PBQPRAConstraintList> ConstraintsRoot =
      std::make_unique<PBQPRAConstraintList>();
    ConstraintsRoot->addConstraint(std::make_unique<SpillCosts>());
    ConstraintsRoot->addConstraint(std::make_unique<Interference>());
    if (PBQPCoalescing)
      ConstraintsRoot->addConstraint(std::make_unique<Coalescing>());
    ConstraintsRoot->addConstraint(Subtarget.getCustomPBQPConstraints());

    bool PBQPAllocComplete = false;
    unsigned Round = 0;

    while (!PBQPAllocComplete) {
      LLVM_DEBUG(dbgs() << "  PBQP Regalloc round " << Round << ":\n");
      (void) Round;

      PBQPRAGraph G(PBQPRAGraph::GraphMetadata(MF, LIS, MBFI));
      initializeGraph(G, VRM, *VRegSpiller);
      ConstraintsRoot->apply(G);

#ifndef NDEBUG
      if (PBQPDumpGraphs) {
        std::ostringstream RS;
        RS << Round;
        std::string GraphFileName = FullyQualifiedName + "." + RS.str() +
                                    ".pbqpgraph";
        std::error_code EC;
        raw_fd_ostream OS(GraphFileName, EC, sys::fs::OF_TextWithCRLF);
        LLVM_DEBUG(dbgs() << "Dumping graph for round " << Round << " to \""
                          << GraphFileName << "\"\n");
        G.dump(OS);
      }
#endif

      PBQP::Solution Solution = PBQP::RegAlloc::solve(G);
      PBQPAllocComplete = mapPBQPToRegAlloc(G, Solution, VRM, *VRegSpiller);
      ++Round;
    }
  }

  // Finalise allocation, allocate empty ranges.
  finalizeAlloc(MF, LIS, VRM);
  postOptimization(*VRegSpiller, LIS);
  VRegsToAlloc.clear();
  EmptyIntervalVRegs.clear();

  LLVM_DEBUG(dbgs() << "Post alloc VirtRegMap:\n" << VRM << "\n");

  return true;
}

/// Create Printable object for node and register info.
static Printable PrintNodeInfo(PBQP::RegAlloc::PBQPRAGraph::NodeId NId,
                               const PBQP::RegAlloc::PBQPRAGraph &G) {
  return Printable([NId, &G](raw_ostream &OS) {
    const MachineRegisterInfo &MRI = G.getMetadata().MF.getRegInfo();
    const TargetRegisterInfo *TRI = MRI.getTargetRegisterInfo();
    Register VReg = G.getNodeMetadata(NId).getVReg();
    const char *RegClassName = TRI->getRegClassName(MRI.getRegClass(VReg));
    OS << NId << " (" << RegClassName << ':' << printReg(VReg, TRI) << ')';
  });
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void PBQP::RegAlloc::PBQPRAGraph::dump(raw_ostream &OS) const {
  for (auto NId : nodeIds()) {
    const Vector &Costs = getNodeCosts(NId);
    assert(Costs.getLength() != 0 && "Empty vector in graph.");
    OS << PrintNodeInfo(NId, *this) << ": " << Costs << '\n';
  }
  OS << '\n';

  for (auto EId : edgeIds()) {
    NodeId N1Id = getEdgeNode1Id(EId);
    NodeId N2Id = getEdgeNode2Id(EId);
    assert(N1Id != N2Id && "PBQP graphs should not have self-edges.");
    const Matrix &M = getEdgeCosts(EId);
    assert(M.getRows() != 0 && "No rows in matrix.");
    assert(M.getCols() != 0 && "No cols in matrix.");
    OS << PrintNodeInfo(N1Id, *this) << ' ' << M.getRows() << " rows / ";
    OS << PrintNodeInfo(N2Id, *this) << ' ' << M.getCols() << " cols:\n";
    OS << M << '\n';
  }
}

LLVM_DUMP_METHOD void PBQP::RegAlloc::PBQPRAGraph::dump() const {
  dump(dbgs());
}
#endif

void PBQP::RegAlloc::PBQPRAGraph::printDot(raw_ostream &OS) const {
  OS << "graph {\n";
  for (auto NId : nodeIds()) {
    OS << "  node" << NId << " [ label=\""
       << PrintNodeInfo(NId, *this) << "\\n"
       << getNodeCosts(NId) << "\" ]\n";
  }

  OS << "  edge [ len=" << nodeIds().size() << " ]\n";
  for (auto EId : edgeIds()) {
    OS << "  node" << getEdgeNode1Id(EId)
       << " -- node" << getEdgeNode2Id(EId)
       << " [ label=\"";
    const Matrix &EdgeCosts = getEdgeCosts(EId);
    for (unsigned i = 0; i < EdgeCosts.getRows(); ++i) {
      OS << EdgeCosts.getRowAsVector(i) << "\\n";
    }
    OS << "\" ]\n";
  }
  OS << "}\n";
}

FunctionPass *llvm::createPBQPRegisterAllocator(char *customPassID) {
  return new RegAllocPBQP(customPassID);
}

FunctionPass* llvm::createDefaultPBQPRegisterAllocator() {
  return createPBQPRegisterAllocator();
}
